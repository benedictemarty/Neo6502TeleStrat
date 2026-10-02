// telestrat_bench.c — mesure de charge du RP2040 sans carte (tools/rp2040_load.py)
//
// Même système que le firmware (mêmes options de compilation, même code
// chaud en RAM), mais le vrai 65C02 est remplacé par la relecture d'une trace
// de bus enregistrée par le banc PC (telestrat_headless -B) : adresse, R/W et
// donnée de chaque cycle. L'ELF produit n'est jamais flashé : il est exécuté
// dans un émulateur Cortex-M0+ qui compte les cycles. Chaque octet que le
// système place sur le bus est comparé à celui de la trace (bench_mismatch) :
// le code ARM doit se comporter exactement comme la version PC.
//
// Mémoire préparée par l'outil : trace en BENCH_TRACE_ADDR (un mot par cycle :
// adresse | R/W << 16 | donnée << 24), événements clavier en BENCH_EVENTS_ADDR,
// image disque en BENCH_DISK_ADDR.
//
// ## Licence zlib/libpng
//
// Copyright (c) 2026 bmarty
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the
// use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//     1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software in a
//     product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
//     2. Altered source versions must be plainly marked as such, and must not
//     be misrepresented as being the original software.
//     3. This notice may not be removed or altered from any source
//     distribution.

#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <pico/platform.h>
// Images en RAM, comme les emplacements de banque du firmware
#define TELESTRAT_ROM_SECTION(x) __not_in_flash(x)
#include "roms/telestrat_roms.h"

#define TELESTRAT_HOT __attribute__((section(".time_critical.telestrat")))
#define CHIPS_HOT     __attribute__((section(".time_critical.telestrat")))

#define BENCH_TRACE_ADDR  0x60000000u
#define BENCH_EVENTS_ADDR 0x5F000000u
#define BENCH_DISK_ADDR   0x70000000u

// 65C02 de relecture : même interface que chips/wdc65C02cpu.h
typedef struct {
    uint16_t addr;
    bool rw;
} benchcpu_t;

volatile uint32_t bench_pos;       // cycle courant de la trace
volatile uint32_t bench_mismatch;  // octets lus différents de la trace
volatile uint32_t bench_first_mismatch = 0xFFFFFFFFu;
volatile uint32_t bench_irq;

static inline void bench_cpu_tick(benchcpu_t* c) {
    uint32_t e = ((const uint32_t*)BENCH_TRACE_ADDR)[bench_pos++];
    c->addr = (uint16_t)e;
    c->rw = (e >> 16) & 1;
}

static inline uint8_t bench_get_data(void) { return (uint8_t)(((const uint32_t*)BENCH_TRACE_ADDR)[bench_pos - 1] >> 24); }

static inline void bench_set_data(uint8_t d) {
    if (d != bench_get_data()) {
        if (bench_first_mismatch == 0xFFFFFFFFu) bench_first_mismatch = bench_pos - 1;
        bench_mismatch++;
    }
}

#define MOS6502CPU_T                 benchcpu_t
#define MOS6502CPU_DESC_T            int
#define MOS6502CPU_INIT(c, desc)     ((void)0)
#define MOS6502CPU_RESET(c)          ((void)0)
#define MOS6502CPU_NMI(c)            ((void)0)
#define MOS6502CPU_TICK(c)           bench_cpu_tick(c)
#define MOS6502CPU_GET_ADDR(c)       ((c)->addr)
#define MOS6502CPU_GET_DATA(c)       bench_get_data()
#define MOS6502CPU_SET_DATA(c, data) bench_set_data(data)
#define MOS6502CPU_SET_IRQ(c, state) (bench_irq = (state))
#define MOS6502CPU_SET_NMI(c, state) ((void)0)
#define MOS6502CPU_SYNC(c)           (false)

#include "chips/chips_common.h"
#include "chips/via6522.h"
// AY en flash (appelé tous les 64 cycles) : en RAM, +1 Ko sans gain de charge mesuré
#define AY38910_HOT
#include "chips/ay38910psg.h"
#include "chips/kbd.h"
#include "chips/clk.h"
#include "devices/wd1793.h"
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "systems/telestrat.h"

static telestrat_t __not_in_flash("bench") sys;
static uint32_t bench_event_pos;

// Événements clavier : mots (trame << 16 | appui << 15 | code)
typedef struct {
    uint32_t count;
    uint32_t ev[];
} bench_events_t;

// Liaison série branchée mais muette : l'ACIA interroge la ligne comme sur la
// carte (tous les 64 cycles)
static int bench_rx(void* u) {
    (void)u;
    return -1;
}

static void bench_tx(uint8_t d, void* u) {
    (void)d;
    (void)u;
}

// config : 0 = « oricutron » du banc, 1 = « standard » (TELEMATIC en banque 3)
__attribute__((noinline, used)) void bench_init(uint32_t disk_size, uint32_t config) {
    telestrat_desc_t d = {0};
    d.minitel.tx = bench_tx;
    d.minitel.rx = bench_rx;
    if (config == 1) {
        d.banks[0].type = TELESTRAT_BANK_RAM;
        d.banks[2] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_teleass};
        d.banks[3] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_telematic};
    } else {
        for (int i = 0; i <= 4; i++) d.banks[i].type = TELESTRAT_BANK_RAM;
        d.banks[5] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_teleass};
    }
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_hyperbas};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_telemon24};
    telestrat_init(&sys, &d);
    if (disk_size) telestrat_insert_disk(&sys, 0, (uint8_t*)BENCH_DISK_ADDR, disk_size, false);
    telestrat_reset(&sys);
    bench_pos = 0;
    bench_event_pos = 0;
}

// Début de trame : événements clavier de la trame `frame`
__attribute__((noinline, used)) void bench_frame_begin(uint32_t frame) {
    const bench_events_t* e = (const bench_events_t*)BENCH_EVENTS_ADDR;
    while (bench_event_pos < e->count && (e->ev[bench_event_pos] >> 16) == frame) {
        uint32_t v = e->ev[bench_event_pos++];
        int code = v & 0x7FFF;
        if (v & 0x8000) {
            telestrat_key_down(&sys, code);
        } else {
            telestrat_key_up(&sys, code);
        }
    }
}

// Tranche de n cycles 6502 (le firmware en fait 20 de ~1000 par trame)
__attribute__((noinline, used)) void bench_ticks(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) telestrat_tick(&sys);
}

// Coût de la relecture seule (à retrancher des mesures de bench_ticks)
__attribute__((noinline, used)) void bench_replay_only(uint32_t n) {
    static benchcpu_t c;
    for (uint32_t i = 0; i < n; i++) {
        bench_cpu_tick(&c);
        if (c.rw) {
            bench_set_data(bench_get_data());
        } else {
            (void)bench_get_data();
        }
    }
}

// Fin de trame : comme la boucle principale du firmware
__attribute__((noinline, used)) void bench_frame_end(void) {
    telestrat_kbd_update(&sys, 20000);
    telestrat_screen_update(&sys);
}

int main(void) {
    bench_init(0, 0);
    bench_ticks(1);
    bench_replay_only(1);
    bench_frame_begin(0);
    bench_frame_end();
    return 0;
}
