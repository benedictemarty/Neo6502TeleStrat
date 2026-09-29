// replay.c — rejoue une trace du modèle de référence sur le système optimisé
//
//   replay PRÉFIXE CONFIG [disque.dsk]
//
// La trace (telestrat_headless_ref -B PRÉFIXE) fixe ce que fait le 65C02 à
// chaque cycle ; le système optimisé (systems/telestrat.h) doit y répondre
// exactement comme la référence (systems/telestrat_ref.h) : même octet placé
// sur le bus à chaque lecture, même ligne IRQ à chaque cycle, mêmes
// échantillons audio, mêmes octets série émis et reçus aux mêmes cycles.
// Code de sortie 0 si tout est identique.

#define _POSIX_C_SOURCE 200809L
#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "roms/telestrat_roms.h"

// 65C02 de relecture
typedef struct {
    uint16_t addr;
    bool rw;
    uint8_t data;
    bool irq;
} replaycpu_t;

static const uint32_t* trace;
static size_t trace_len;
static size_t pos;
static unsigned long mism_data, mism_irq;
static long first_data = -1, first_irq = -1;

static inline void replay_tick(replaycpu_t* c) {
    uint32_t e = trace[pos++];
    c->addr = (uint16_t)e;
    c->rw = (e >> 16) & 1;
    c->data = (uint8_t)(e >> 24);  // donnée écrite par le 65C02 (écritures)
}

static inline void replay_set_data(replaycpu_t* c, uint8_t d) {
    if (d != (uint8_t)(trace[pos - 1] >> 24)) {
        if (mism_data < 8) {
            fprintf(stderr, "  cycle %zu : lecture $%04X -> $%02X, référence $%02X\n", pos - 1, c->addr, d,
                    (uint8_t)(trace[pos - 1] >> 24));
        }
        if (first_data < 0) first_data = (long)pos - 1;
        mism_data++;
    }
    c->data = d;
}

#define MOS6502CPU_T                 replaycpu_t
#define MOS6502CPU_DESC_T            int
#define MOS6502CPU_INIT(c, desc)     ((void)0)
#define MOS6502CPU_RESET(c)          ((void)0)
#define MOS6502CPU_NMI(c)            ((void)0)
#define MOS6502CPU_TICK(c)           replay_tick(c)
#define MOS6502CPU_GET_ADDR(c)       ((c)->addr)
#define MOS6502CPU_GET_DATA(c)       ((c)->data)
#define MOS6502CPU_SET_DATA(c, d)    replay_set_data(c, d)
#define MOS6502CPU_SET_IRQ(c, state) ((c)->irq = (state))
#define MOS6502CPU_SET_NMI(c, state) ((void)0)
#define MOS6502CPU_SYNC(c)           (false)

#include "chips/chips_common.h"
#include "chips/mos6522via.h"
#include "chips/ay38910psg.h"
#include "chips/kbd.h"
#include "chips/clk.h"
#include "devices/wd1793.h"
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "systems/telestrat.h"

static telestrat_t sys;

static uint8_t* load(const char* path, size_t* n, int must) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        if (must) {
            perror(path);
            exit(2);
        }
        *n = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* b = malloc(len ? (size_t)len : 1);
    if (len && fread(b, 1, (size_t)len, f) != (size_t)len) exit(2);
    fclose(f);
    *n = (size_t)len;
    return b;
}

// Audio
static const uint8_t* aud;
static size_t aud_len, aud_pos;
static unsigned long mism_aud;
static long first_aud = -1;
static void audio(const uint8_t s, void* u) {
    (void)u;
    if (aud_pos >= aud_len || aud[aud_pos] != s) {
        if (first_aud < 0) first_aud = (long)aud_pos;
        mism_aud++;
    }
    aud_pos++;
}

// Série : enregistrements (cycle, sens << 8 | octet), sens 0 = émis, 1 = reçu
static const uint32_t* ser;
static size_t ser_n, ser_tx, ser_rx;
static unsigned long mism_ser;
static long first_ser = -1;

static void ser_fail(void) {
    if (first_ser < 0) first_ser = (long)sys.system_ticks;
    mism_ser++;
}

static size_t next_of(size_t i, int dir) {
    while (i < ser_n && (int)(ser[2 * i + 1] >> 8) != dir) i++;
    return i;
}

static void serial_tx(uint8_t d, void* u) {
    (void)u;
    ser_tx = next_of(ser_tx, 0);
    if (ser_tx >= ser_n || ser[2 * ser_tx] != sys.system_ticks || (uint8_t)ser[2 * ser_tx + 1] != d) ser_fail();
    ser_tx++;
}

static int serial_rx(void* u) {
    (void)u;
    ser_rx = next_of(ser_rx, 1);
    if (ser_rx >= ser_n) return -1;
    uint32_t cyc = ser[2 * ser_rx];
    if (cyc == sys.system_ticks) return (uint8_t)ser[2 * ser_rx++ + 1];
    if (cyc < sys.system_ticks) {  // la référence l'avait pris plus tôt
        ser_fail();
        return (uint8_t)ser[2 * ser_rx++ + 1];
    }
    return -1;
}

#define RAM(n) {.type = TELESTRAT_BANK_RAM}
#define ROM(p) {.type = TELESTRAT_BANK_ROM, .rom = (p)}
#define EMPTY  {.type = TELESTRAT_BANK_EMPTY}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage : %s préfixe config [disque]\n", argv[0]);
        return 2;
    }
    char path[512];
    size_t n;
    snprintf(path, sizeof(path), "%s.trace", argv[1]);
    trace = (const uint32_t*)load(path, &n, 1);
    trace_len = n / 4;
    snprintf(path, sizeof(path), "%s.ev", argv[1]);
    const uint32_t* ev = (const uint32_t*)load(path, &n, 1);
    snprintf(path, sizeof(path), "%s.aud", argv[1]);
    aud = load(path, &aud_len, 0);
    snprintf(path, sizeof(path), "%s.ser", argv[1]);
    ser = (const uint32_t*)load(path, &n, 0);
    ser_n = n / 8;
    snprintf(path, sizeof(path), "%s.ring", argv[1]);
    const uint32_t* ring = (const uint32_t*)load(path, &n, 0);
    size_t ring_n = n / 8, ring_i = 0;
    snprintf(path, sizeof(path), "%s.fb", argv[1]);
    const uint32_t* fbh = (const uint32_t*)load(path, &n, 0);
    size_t fbh_n = n / 4;
    unsigned long mism_fb = 0;
    long first_fb = -1;

    telestrat_desc_t desc = {0};
    const telestrat_bank_desc_t standard[8] = {RAM(0), EMPTY, ROM(telestrat_teleass), ROM(telestrat_telematic),
                                               EMPTY, EMPTY, ROM(telestrat_hyperbas), ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t oricutron[8] = {RAM(0), RAM(1), RAM(2), RAM(3),
                                                RAM(4), ROM(telestrat_teleass), ROM(telestrat_hyperbas),
                                                ROM(telestrat_telemon24)};
    if (!strcmp(argv[2], "standard")) {
        memcpy(desc.banks, standard, sizeof(desc.banks));
    } else {
        memcpy(desc.banks, oricutron, sizeof(desc.banks));
    }
    desc.audio.callback.func = audio;
    desc.minitel.tx = serial_tx;
    desc.minitel.rx = serial_rx;
    telestrat_init(&sys, &desc);
    uint8_t* disk = NULL;
    if (argc > 3) {
        disk = load(argv[3], &n, 1);
        telestrat_insert_disk(&sys, 0, disk, n, false);
    }
    telestrat_reset(&sys);

    uint32_t ev_n = ev[0], ev_i = 0;
    size_t frames = trace_len / 20000;
    for (size_t frame = 0; frame < frames; frame++) {
        while (ev_i < ev_n && (ev[1 + ev_i] >> 16) == frame) {
            uint32_t v = ev[1 + ev_i++];
            if (v & 0x8000) {
                telestrat_key_down(&sys, v & 0x7FFF);
            } else {
                telestrat_key_up(&sys, v & 0x7FFF);
            }
        }
        for (int ms = 0; ms < 20; ms++) {
            for (int i = 0; i < 1000; i++) {
                telestrat_tick(&sys);
                if (sys.cpu.irq != (bool)((trace[pos - 1] >> 17) & 1)) {
                    if (first_irq < 0) first_irq = (long)pos - 1;
                    mism_irq++;
                }
            }
            while (ring_i < ring_n && ring[2 * ring_i] == frame * 20 + (size_t)ms) {
                telestrat_set_ring(&sys, ring[2 * ring_i + 1] != 0);
                ring_i++;
            }
        }
        telestrat_kbd_update(&sys, 20000);
        telestrat_screen_update(&sys);
        if (frame < fbh_n) {
            uint32_t h = 2166136261u;
            for (size_t i = 0; i < sizeof(sys.fb); i++) h = (h ^ sys.fb[i]) * 16777619u;
            if (h != fbh[frame]) {
                if (first_fb < 0) first_fb = (long)frame;
                mism_fb++;
            }
        }
    }
    // Octets série de la référence jamais produits
    if (next_of(ser_tx, 0) < ser_n) ser_fail();
    if (aud_pos != aud_len) mism_aud++;

    printf("replay %s : %zu cycles ; différences : bus %lu (1er %ld), IRQ %lu (1er %ld), audio %lu/%zu (1er %ld),"
           " série %lu (1er au cycle %ld), image %lu/%zu trames (1re %ld)\n",
           argv[1], pos, mism_data, first_data, mism_irq, first_irq, mism_aud, aud_len, first_aud, mism_ser, first_ser,
           mism_fb, fbh_n, first_fb);
    return (mism_data || mism_irq || mism_aud || mism_ser || mism_fb) ? 1 : 0;
}
