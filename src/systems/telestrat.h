#pragma once

// telestrat.h
//
// Oric Telestrat (1986) en un en-tête C, au format des systèmes de
// reload-emulator (voir oric.h, dont la vidéo ULA, le clavier et le câblage
// VIA 1 / AY-3-8912 sont repris).
//
// Définir CHIPS_IMPL avant l'inclusion dans *un* fichier C pour obtenir
// l'implémentation. En-têtes à inclure avant celui-ci :
//
// - chips/chips_common.h
// - chips/w65c02cpu.h (PC) | chips/wdc65C02cpu.h (Neo6502 : vrai 65C02)
// - chips/mos6522via.h
// - chips/ay38910psg.h
// - chips/kbd.h
// - chips/clk.h
// - devices/wd1793.h
// - devices/telestrat_fdc.h
// - devices/mos6551acia.h
//
// ## Carte mémoire (notice « Extension RAM 64 Ko pour Oric Telestrat »,
//    F. Broche, 1987, chapitre IV ; décodage d'Oricutron, machine.c)
//
//   $0000-$02FF  RAM
//   $0300-$030F  VIA 1 (clavier, AY, imprimante : ORA, STROBE PB4, ACK CA1)
//   $0310-$0313  WD1793 ; $0314 contrôle ; $0318 DRQ (Microdisc intégré),
//                disquettes au format MFM_DISK (4 lecteurs)
//   $031C-$031F  ACIA 6551 (prise Minitel ; liaison fournie par la plate-forme)
//   $0320-$032F  VIA 2 : PA0-PA2 = banque de $C000-$FFFF, PB = joysticks
//   autres $03xx  reflet du VIA 1 (comme Oricutron)
//   $0400-$BFFF  RAM
//   $C000-$FFFF  une des 8 banques de 16 Ko ; banque 7 (TELEMON) au RESET
//
//   Banque 0 : RAM interne ; 1-4 : port droit (TELE-ASS en 2, TELEMATIC en 3
//   ou cartouche RAM 64 Ko en 1-4) ; 4-7 : port gauche (HYPER-BASIC en 6,
//   TELEMON en 7).
//
// ## Licence zlib/libpng
//
// Copyright (c) 2023 Veselin Sladkov (oric.h, dont ce fichier est dérivé)
// Copyright (c) 2026 bmarty (Telestrat : banques, VIA 2, Microdisc intégré)
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

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELESTRAT_FREQUENCY (1000000)  // 1 MHz

#define TELESTRAT_SCREEN_WIDTH     240
#define TELESTRAT_SCREEN_HEIGHT    224
#define TELESTRAT_FRAMEBUFFER_SIZE ((TELESTRAT_SCREEN_WIDTH / 2) * TELESTRAT_SCREEN_HEIGHT)

#define TELESTRAT_NUM_BANKS 8
#define TELESTRAT_BANK_SIZE 0x4000
#define TELESTRAT_BOOT_BANK 7

// Banques de RAM disponibles : banque 0 + cartouche 64 Ko (banques 1-4)
#ifndef TELESTRAT_MAX_RAM_BANKS
#define TELESTRAT_MAX_RAM_BANKS 5
#endif

#define TELESTRAT_PALETTE_BITS 3
#define TELESTRAT_PALETTE_SIZE (1 << TELESTRAT_PALETTE_BITS)

static const uint32_t telestrat_palette[TELESTRAT_PALETTE_SIZE] = {
    RGBA8(0x00, 0x00, 0x00), /* noir */
    RGBA8(0xFF, 0x00, 0x00), /* rouge */
    RGBA8(0x00, 0xFF, 0x00), /* vert */
    RGBA8(0xFF, 0xFF, 0x00), /* jaune */
    RGBA8(0x00, 0x00, 0xFF), /* bleu */
    RGBA8(0xFF, 0x00, 0xFF), /* magenta */
    RGBA8(0x00, 0xFF, 0xFF), /* cyan */
    RGBA8(0xFF, 0xFF, 0xFF), /* blanc */
};

typedef enum {
    TELESTRAT_BANK_EMPTY = 0,  // pas de cartouche : bus flottant, écritures ignorées
    TELESTRAT_BANK_RAM,
    TELESTRAT_BANK_ROM,
} telestrat_bank_type_t;

typedef struct {
    telestrat_bank_type_t type;
    const uint8_t* rom;  // TELESTRAT_BANK_SIZE octets si type == ROM
} telestrat_bank_desc_t;

// Joysticks (bits actifs à 1 ici, inversés vers le VIA 2)
#define TELESTRAT_JOY_RIGHT (1 << 0)
#define TELESTRAT_JOY_LEFT  (1 << 1)
#define TELESTRAT_JOY_FIRE  (1 << 2)
#define TELESTRAT_JOY_DOWN  (1 << 3)
#define TELESTRAT_JOY_UP    (1 << 4)

// Imprimante sur le port parallèle du VIA 1 : octet = ORA au front
// descendant de PB4 (STROBE), réponse ACK sur CA1 (comme Oricutron).
typedef void (*telestrat_printer_t)(uint8_t data, void* user_data);

typedef struct {
    chips_debug_t debug;
    chips_audio_desc_t audio;
    telestrat_bank_desc_t banks[TELESTRAT_NUM_BANKS];
    struct {
        telestrat_printer_t func;  // NULL : pas d'imprimante branchée
        void* user_data;
    } printer;
    // Liaisons de l'ACIA, aiguillées par PA4 du VIA 2 (TELEMON $DB3A/$DB5D :
    // PA4 = 0 prise Minitel, 1200 bauds 7E1 ; PA4 = 1 prise RS232) ;
    // NULL = rien de branché
    struct {
        mos6551acia_tx_t tx;
        mos6551acia_rx_t rx;
        void* user_data;
    } minitel, rs232;
} telestrat_desc_t;

typedef struct {
    MOS6502CPU_T cpu;
    mos6522via_t via;   // VIA 1 ($0300)
    mos6522via_t via2;  // VIA 2 ($0320)
    ay38910psg_t psg;
    kbd_t kbd;
    telestrat_fdc_t fdc;
    mos6551acia_t acia;
    bool valid;
    chips_debug_t debug;
    chips_audio_callback_t audio_callback;

    uint8_t ram[0xC000];
    uint8_t bank_ram[TELESTRAT_MAX_RAM_BANKS][TELESTRAT_BANK_SIZE];
    telestrat_bank_type_t bank_type[TELESTRAT_NUM_BANKS];
    const uint8_t* bank_rd[TELESTRAT_NUM_BANKS];
    uint8_t* bank_wr[TELESTRAT_NUM_BANKS];
    // Contenu d'origine (descripteur), pour telestrat_restore_bank
    telestrat_bank_type_t bank_type_orig[TELESTRAT_NUM_BANKS];
    const uint8_t* bank_rd_orig[TELESTRAT_NUM_BANKS];
    uint8_t* bank_wr_orig[TELESTRAT_NUM_BANKS];
    uint8_t bank;  // banque visible en $C000-$FFFF

    uint8_t joy[2];  // [0] = port droit (PB7), [1] = port gauche (PB6)

    telestrat_printer_t printer;
    void* printer_user_data;
    bool strobe;          // dernier niveau de PB4
    int32_t printer_ack;  // cycles restants de l'impulsion ACK

    bool ring;  // détecteur de sonnerie de la ligne -> CB1 du VIA 2
    bool inputs_dirty;      // entrée extérieure changée : prochain pas complet
    uint32_t quiet_until;   // pas de 4 cycles sautés tant que system_ticks < quiet_until
    uint32_t deferred;      // cycles des pas sautés, pas encore appliqués
    mos6551acia_tx_t minitel_tx, rs232_tx;
    mos6551acia_rx_t minitel_rx, rs232_rx;
    void *minitel_user_data, *rs232_user_data;

    int blink_counter;
    uint8_t pattr;
    uint8_t fb[TELESTRAT_FRAMEBUFFER_SIZE];
    bool screen_dirty;

    uint32_t system_ticks;
    uint32_t psg_next;         // prochain cycle où l'AY a quelque chose à faire
    uint32_t psg_next_sample;  // prochain échantillon (tous les 46 cycles)
    const uint8_t* rd_cur;     // banque visible : lecture (NULL = bus flottant)
    uint8_t* wr_cur;           // banque visible : écriture (NULL = ignorée)
} telestrat_t;

void telestrat_init(telestrat_t* sys, const telestrat_desc_t* desc);
void telestrat_discard(telestrat_t* sys);
void telestrat_reset(telestrat_t* sys);
// Démarrage à froid (mise sous tension) : RAM effacée puis RESET. TELEMON ne
// refait l'inventaire des banques (cartouches) qu'à froid : sur un RESET à
// chaud il affiche seulement « Logiciel ecrit par Fabrice BROCHE » (observé).
void telestrat_cold_reset(telestrat_t* sys);
void telestrat_nmi(telestrat_t* sys);
void telestrat_tick(telestrat_t* sys);
uint32_t telestrat_exec(telestrat_t* sys, uint32_t micro_seconds);
void telestrat_screen_update(telestrat_t* sys);
// Sélectionne la banque de $C000-$FFFF (0..7), comme le fait V2DRA
void telestrat_select_bank(telestrat_t* sys, uint8_t bank);
// État d'un joystick (0 = droit, 1 = gauche), bits TELESTRAT_JOY_*
bool telestrat_insert_disk(telestrat_t* sys, int drive, uint8_t* image, size_t size, bool write_protect) {
    return wd1793_insert(&sys->fdc.wd, drive, image, size, write_protect);
}

// Entrée extérieure changée : pas complet au prochain multiple de 4, même au
// milieu d'une fenêtre de repos
static inline void _telestrat_input_changed(telestrat_t* sys) {
    sys->inputs_dirty = true;
    sys->quiet_until = sys->system_ticks;
}

void telestrat_set_ring(telestrat_t* sys, bool level) {
    if (level != sys->ring) _telestrat_input_changed(sys);
    sys->ring = level;
}

void telestrat_key_down(telestrat_t* sys, int key_code) {
    kbd_key_down(&sys->kbd, key_code);
    _telestrat_input_changed(sys);
}

void telestrat_key_up(telestrat_t* sys, int key_code) {
    kbd_key_up(&sys->kbd, key_code);
    _telestrat_input_changed(sys);
}

void telestrat_kbd_update(telestrat_t* sys, uint32_t micro_seconds) {
    kbd_update(&sys->kbd, micro_seconds);
    _telestrat_input_changed(sys);
}

void telestrat_set_joystick(telestrat_t* sys, int port, uint8_t state);
// Niveau du détecteur de sonnerie (CB1 du VIA 2 ; TELEMON XRING attend des
// impulsions à 50 Hz en rafales)
void telestrat_set_ring(telestrat_t* sys, bool level);
// Clavier : passer par ces fonctions (et non par kbd_*) pour que le système
// voie le changement
void telestrat_key_down(telestrat_t* sys, int key_code);
void telestrat_key_up(telestrat_t* sys, int key_code);
void telestrat_kbd_update(telestrat_t* sys, uint32_t micro_seconds);
// Prise sélectionnée pour l'ACIA (PA4 du VIA 2)
bool telestrat_serial_is_rs232(telestrat_t* sys);
// Cartouche changée à chaud (menu) : ROM de 16 Ko en lecture seule, NULL =
// banque vide ; telestrat_restore_bank remet le contenu du descripteur
void telestrat_set_bank_rom(telestrat_t* sys, int bank, const uint8_t* rom);
void telestrat_restore_bank(telestrat_t* sys, int bank);
// Insère une image MFM_DISK dans le lecteur 0..3 (false si invalide)
bool telestrat_insert_disk(telestrat_t* sys, int drive, uint8_t* image, size_t size, bool write_protect);
// Lecture « système » (sans effet de bord sur les E/S) pour les tests
uint8_t telestrat_peek(telestrat_t* sys, uint16_t addr);

#ifdef __cplusplus
}  // extern "C"
#endif

/*-- IMPLEMENTATION ----------------------------------------------------------*/
#ifdef CHIPS_IMPL
#include <string.h>
#ifndef CHIPS_ASSERT
#include <assert.h>
#define CHIPS_ASSERT(c) assert(c)
#endif

// Section du code exécuté à chaque cycle (le RP2040 le place en RAM)
#ifndef TELESTRAT_HOT
#define TELESTRAT_HOT
#endif
// Chemins moins fréquents, hors de telestrat_tick pour qu'il reste court
// (peu de registres à sauver) mais en RAM eux aussi
#define TELESTRAT_SLOW TELESTRAT_HOT __attribute__((noinline))

#define TELESTRAT_PATTR_HIRES (0x04)
#define TELESTRAT_LATTR_ALT   (0x01)
#define TELESTRAT_LATTR_DSIZE (0x02)
#define TELESTRAT_LATTR_BLINK (0x04)

static void _telestrat_init_key_map(telestrat_t* sys);

// Port A de l'AY : colonnes du clavier (comme oric.h)
static void _telestrat_psg_out(int port_id, uint8_t data, void* user_data) {
    telestrat_t* sys = (telestrat_t*)user_data;
    if (port_id == AY38910PSG_PORT_A) {
        kbd_set_active_columns(&sys->kbd, data ^ 0xFF);
    }
}

static uint8_t _telestrat_psg_in(int port_id, void* user_data) {
    (void)port_id;
    (void)user_data;
    return 0xFF;
}

void telestrat_select_bank(telestrat_t* sys, uint8_t bank) {
    sys->bank = bank & 7;
    sys->rd_cur = sys->bank_rd[sys->bank];
    sys->wr_cur = sys->bank_wr[sys->bank];
}

// PA4 du VIA 2 : prise RS232 (1) ou Minitel (0)
bool telestrat_serial_is_rs232(telestrat_t* sys) { return (mos6522via_get_pa(&sys->via2) & 0x10) != 0; }

void telestrat_set_bank_rom(telestrat_t* sys, int bank, const uint8_t* rom) {
    if (bank < 0 || bank >= TELESTRAT_NUM_BANKS) return;
    sys->bank_type[bank] = rom ? TELESTRAT_BANK_ROM : TELESTRAT_BANK_EMPTY;
    sys->bank_rd[bank] = rom;
    sys->bank_wr[bank] = NULL;
    if (bank == sys->bank) telestrat_select_bank(sys, bank);
}

void telestrat_restore_bank(telestrat_t* sys, int bank) {
    if (bank < 0 || bank >= TELESTRAT_NUM_BANKS) return;
    sys->bank_type[bank] = sys->bank_type_orig[bank];
    sys->bank_rd[bank] = sys->bank_rd_orig[bank];
    sys->bank_wr[bank] = sys->bank_wr_orig[bank];
    if (bank == sys->bank) telestrat_select_bank(sys, bank);
}

static void _telestrat_serial_tx(uint8_t data, void* user_data) {
    telestrat_t* sys = (telestrat_t*)user_data;
    if (telestrat_serial_is_rs232(sys)) {
        if (sys->rs232_tx) sys->rs232_tx(data, sys->rs232_user_data);
    } else if (sys->minitel_tx) {
        sys->minitel_tx(data, sys->minitel_user_data);
    }
}

static int _telestrat_serial_rx(void* user_data) {
    telestrat_t* sys = (telestrat_t*)user_data;
    if (telestrat_serial_is_rs232(sys)) return sys->rs232_rx ? sys->rs232_rx(sys->rs232_user_data) : -1;
    return sys->minitel_rx ? sys->minitel_rx(sys->minitel_user_data) : -1;
}

void telestrat_init(telestrat_t* sys, const telestrat_desc_t* desc) {
    CHIPS_ASSERT(sys && desc);
    if (desc->debug.callback.func) {
        CHIPS_ASSERT(desc->debug.stopped);
    }

    memset(sys, 0, sizeof(telestrat_t));
    sys->psg_next_sample = 45;  // 1er échantillon au 46e cycle (comme la référence)
    sys->valid = true;
    sys->debug = desc->debug;
    sys->audio_callback = desc->audio.callback;

    MOS6502CPU_INIT(&sys->cpu, &(MOS6502CPU_DESC_T){0});
    mos6522via_init(&sys->via);
    mos6522via_init(&sys->via2);
    ay38910psg_init(&sys->psg, &(ay38910psg_desc_t){.type = AY38910PSG_TYPE_8912,
                                                    .in_cb = _telestrat_psg_in,
                                                    .out_cb = _telestrat_psg_out,
                                                    .magnitude = CHIPS_DEFAULT(desc->audio.volume, 1.0f),
                                                    .user_data = sys});
    telestrat_fdc_reset(&sys->fdc);
    mos6551acia_reset(&sys->acia);

    // Banques
    int ram_banks = 0;
    for (int i = 0; i < TELESTRAT_NUM_BANKS; i++) {
        const telestrat_bank_desc_t* b = &desc->banks[i];
        telestrat_bank_type_t type = b->type;
        if (type == TELESTRAT_BANK_ROM && !b->rom) type = TELESTRAT_BANK_EMPTY;
        if (type == TELESTRAT_BANK_RAM && ram_banks >= TELESTRAT_MAX_RAM_BANKS) type = TELESTRAT_BANK_EMPTY;
        sys->bank_type[i] = type;
        switch (type) {
            case TELESTRAT_BANK_RAM:
                sys->bank_rd[i] = sys->bank_wr[i] = sys->bank_ram[ram_banks++];
                break;
            case TELESTRAT_BANK_ROM:
                sys->bank_rd[i] = b->rom;
                sys->bank_wr[i] = NULL;
                break;
            default:
                sys->bank_rd[i] = NULL;
                sys->bank_wr[i] = NULL;
                break;
        }
    }

    for (int i = 0; i < TELESTRAT_NUM_BANKS; i++) {
        sys->bank_type_orig[i] = sys->bank_type[i];
        sys->bank_rd_orig[i] = sys->bank_rd[i];
        sys->bank_wr_orig[i] = sys->bank_wr[i];
    }
    telestrat_select_bank(sys, TELESTRAT_BOOT_BANK);

    _telestrat_init_key_map(sys);
    sys->joy[0] = sys->joy[1] = 0;
    sys->printer = desc->printer.func;
    sys->printer_user_data = desc->printer.user_data;
    sys->minitel_tx = desc->minitel.tx;
    sys->minitel_rx = desc->minitel.rx;
    sys->minitel_user_data = desc->minitel.user_data;
    sys->rs232_tx = desc->rs232.tx;
    sys->rs232_rx = desc->rs232.rx;
    sys->rs232_user_data = desc->rs232.user_data;
    sys->acia.tx_cb = _telestrat_serial_tx;
    sys->acia.rx_cb = _telestrat_serial_rx;
    sys->acia.user_data = sys;
    sys->acia.cpu_freq = TELESTRAT_FREQUENCY;
}

void telestrat_discard(telestrat_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    sys->valid = false;
}

void telestrat_nmi(telestrat_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    MOS6502CPU_NMI(&sys->cpu);
}

void telestrat_cold_reset(telestrat_t* sys) {
    memset(sys->ram, 0, sizeof(sys->ram));
    memset(sys->bank_ram, 0, sizeof(sys->bank_ram));
    telestrat_reset(sys);
}

void telestrat_reset(telestrat_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    mos6522via_reset(&sys->via);
    mos6522via_reset(&sys->via2);
    ay38910psg_reset(&sys->psg);
    telestrat_fdc_reset(&sys->fdc);
    mos6551acia_reset(&sys->acia);
    sys->quiet_until = sys->system_ticks;
    sys->deferred = 0;
    // Au RESET, le port A du VIA 2 est en entrée (tiré à 1) : banque 7
    telestrat_select_bank(sys, TELESTRAT_BOOT_BANK);
    MOS6502CPU_RESET(&sys->cpu);
}

// Banque = PA0-PA2 du VIA 2 ; une ligne en entrée garde la valeur précédente
// (comme via_tele_w_iora d'Oricutron).
static inline void _telestrat_update_bank(telestrat_t* sys) {
    uint8_t ddr = sys->via2.pa.ddr & 7;
    uint8_t bank = (sys->bank & ~ddr) | (sys->via2.pa.outr & ddr);
    if (bank != sys->bank) {
        telestrat_select_bank(sys, bank);
    }
}

// Port cartouche vide : bus flottant. TELEMON (2.4, $C2F4) lit deux fois la
// page $FF00-$FFFF de chaque banque et déclare « invalide » ($10) une banque
// dont les valeurs changent ; une valeur fixe la ferait passer pour une ROM.
// Valeur pseudo-aléatoire, déterministe (compteur de cycles et adresse).
static inline uint8_t _telestrat_floating_bus(telestrat_t* sys, uint16_t addr) {
    uint32_t x = sys->system_ticks * 2654435761u ^ addr;
    return (uint8_t)(x >> 24);
}

static inline uint8_t _telestrat_bank_read(telestrat_t* sys, uint16_t addr) {
    const uint8_t* p = sys->rd_cur;
    return p ? p[addr - 0xC000] : _telestrat_floating_bus(sys, addr);
}

uint8_t telestrat_peek(telestrat_t* sys, uint16_t addr) {
    if (addr >= 0xC000) return _telestrat_bank_read(sys, addr);
    return sys->ram[addr];
}

static inline void _telestrat_io_rw(telestrat_t* sys, uint16_t addr, bool rw) {
    uint8_t reg = addr & 0xFF;
    if (reg >= 0x20 && reg <= 0x2F) {
        // VIA 2
        if (rw) {
            MOS6502CPU_SET_DATA(&sys->cpu, mos6522via_read(&sys->via2, reg & 0xF));
        } else {
            mos6522via_write(&sys->via2, reg & 0xF, MOS6502CPU_GET_DATA(&sys->cpu));
            _telestrat_update_bank(sys);
        }
        return;
    }
    if (reg >= 0x10 && reg <= 0x1F) {
        if (reg >= 0x1C) {
            if (rw) {
                MOS6502CPU_SET_DATA(&sys->cpu, mos6551acia_read(&sys->acia, reg));
            } else {
                mos6551acia_write(&sys->acia, reg, MOS6502CPU_GET_DATA(&sys->cpu));
            }
#ifdef TELESTRAT_TRACE_ACIA
            TELESTRAT_TRACE_ACIA(sys, reg & 3, rw, MOS6502CPU_GET_DATA(&sys->cpu));
#endif
            return;
        }
        if (rw) {
            uint8_t data;
            if (telestrat_fdc_read(&sys->fdc, reg & 0xF, &data)) {
                MOS6502CPU_SET_DATA(&sys->cpu, data);
                return;
            }
        } else if (telestrat_fdc_write(&sys->fdc, reg & 0xF, MOS6502CPU_GET_DATA(&sys->cpu))) {
            return;
        }
    }
    // VIA 1 et ses reflets
    if (rw) {
        MOS6502CPU_SET_DATA(&sys->cpu, mos6522via_read(&sys->via, reg & 0xF));
    } else {
        mos6522via_write(&sys->via, reg & 0xF, MOS6502CPU_GET_DATA(&sys->cpu));
    }
}


void telestrat_set_joystick(telestrat_t* sys, int port, uint8_t state) {
    if (port >= 0 && port < 2 && sys->joy[port] != (state & 0x1F)) {
        sys->joy[port] = state & 0x1F;
        _telestrat_input_changed(sys);
    }
}

// Port B du VIA 2 : PB7 sélectionne le port droit, PB6 le gauche ; les
// directions arrivent sur PB0-PB4 actives à 0 (joystick.c d'Oricutron).
static inline void _telestrat_update_joysticks(telestrat_t* sys) {
    uint8_t sel = mos6522via_get_pb(&sys->via2);
    uint8_t mask = 0;
    if (sel & 0x80) mask |= sys->joy[0];
    if (sel & 0x40) mask |= sys->joy[1];
    mos6522via_set_pb(&sys->via2, (uint8_t)~mask);
    mos6522via_set_pa(&sys->via2, 0xFF);
}

static inline void _telestrat_update_printer(telestrat_t* sys, uint8_t pb) {
    if (!sys->printer) return;
    bool strobe = (pb & sys->via.pb.ddr & 0x10) != 0;
    if (sys->strobe && !strobe) {
        sys->printer(sys->via.pa.outr, sys->printer_user_data);
        sys->printer_ack = 40;
    } else if (sys->printer_ack > 0) {
        sys->printer_ack -= 4;
    }
    sys->strobe = strobe;
    // Niveau de CA1 redonné à chaque pas : le VIA de reload ne détecte un front
    // qu'au changement de niveau entre deux appels de mos6522via_set_ca1()
    mos6522via_set_ca1(&sys->via, sys->printer_ack > 0);
}

// Les pas de 4 cycles « au repos » ne changent que des compteurs : ils sont
// sautés (quiet_until) et leur durée cumulée (deferred) est appliquée d'un
// coup avant le prochain accès en $03xx ou le prochain pas complet. Le
// résultat est identique, cycle pour cycle, au modèle de référence
// (systems/telestrat_ref.h, vérifié par tests/replay.c).

#define TELESTRAT_QUIET_MAX 4096  // pas sautés au plus d'affilée

// Applique aux compteurs la durée des pas sautés (aucune échéance franchie :
// garanti par _telestrat_quiet_steps)
// Effet de d/4 pas au repos sur un VIA (voir _telestrat_via_quiet)
static inline void _telestrat_via_skip(mos6522via_t* c, uint32_t d) {
    c->t1.counter -= (int32_t)d;
    if (!MOS6522VIA_ACR_T2_COUNT_PB6(c)) c->t2.counter -= (int32_t)d;
    c->t1.t_out = false;
    c->t2.t_out = false;
}

static inline void _telestrat_catch_up(telestrat_t* sys) {
    uint32_t d = sys->deferred;
    if (!d) return;
    sys->deferred = 0;
    telestrat_fdc_tick(&sys->fdc, (int)d);
    mos6551acia_tick(&sys->acia, (int)d);
    _telestrat_via_skip(&sys->via, d);
    _telestrat_via_skip(&sys->via2, d);
}

// Pas au repos pour un VIA : état stable, autant de pas que les compteurs le
// permettent sans atteindre zéro. Deux états stables de mos6522via_tick :
//   - sans IRQ en attente : intr.pip = 0 (son chemin rapide) ;
//   - IRQ en attente et autorisée (65C02 sous SEI, par exemple pendant les
//     accès disque de TELEMON) : chaque pas remet intr.pip à 0x01 et laisse
//     l'IFR inchangé (bit 7 levé).
// Dans les deux cas un pas ne fait que décompter les timers de 4.
static inline uint32_t _telestrat_via_quiet(const mos6522via_t* c) {
    if ((c->pa.c1_triggered | c->pa.c2_triggered | c->pb.c1_triggered | c->pb.c2_triggered) || c->t1.pip != 0x03 ||
        c->t2.pip != 0x03) {
        return 0;
    }
    if (c->intr.ifr & c->intr.ier) {
        if (c->intr.pip != 0x01 || !(c->intr.ifr & 0x80)) return 0;
    } else if (c->intr.pip != 0) {
        return 0;
    }
    uint32_t k = c->t1.counter < 0 ? 0 : (uint32_t)c->t1.counter / 4;
    if (MOS6522VIA_ACR_T2_COUNT_PB6(c)) {
        if (c->pb6_triggered || c->t2.counter < 0) return 0;
    } else {
        uint32_t k2 = c->t2.counter < 0 ? 0 : (uint32_t)c->t2.counter / 4;
        if (k2 < k) k = k2;
    }
    return k;
}

// Pas avant qu'un compte à rebours (décrémenté de 4 par pas) n'atteigne 0
static inline uint32_t _telestrat_delay_quiet(int32_t d) { return d > 0 ? (uint32_t)(d + 3) / 4 - 1 : UINT32_MAX; }

// Nombre de pas (à partir du prochain, qui suit le cycle t) qui ne peuvent
// rien changer d'autre que les compteurs
static inline uint32_t _telestrat_quiet_steps(telestrat_t* sys, uint32_t t) {
    if (sys->inputs_dirty || sys->printer_ack > 0 || mos6522via_get_cb2(&sys->via)) return 0;
    uint32_t k = _telestrat_via_quiet(&sys->via);
    uint32_t k2 = _telestrat_via_quiet(&sys->via2);
    if (k2 < k) k = k2;
    if (!k) return 0;
    const wd1793_t* w = &sys->fdc.wd;
    uint32_t q = _telestrat_delay_quiet(w->int_delay);
    if (q < k) k = q;
    q = _telestrat_delay_quiet(w->drq_delay);
    if (q < k) k = q;
    const mos6551acia_t* a = &sys->acia;
    if (a->tsr_busy) {
        q = _telestrat_delay_quiet(a->tx_timer);
        if (q < k) k = q;
    }
    // Interrogation de la réception tous les 64 cycles, si elle peut aboutir
    if (!(a->status & MOS6551_ST_RXFULL) && (a->command & MOS6551_CMD_DTR) && a->rx_cb) {
        q = ((64 - (t & 63)) >> 2) - 1;
        if (q < k) k = q;
    }
    return k > TELESTRAT_QUIET_MAX ? TELESTRAT_QUIET_MAX : k;
}

// Pas complet de 4 cycles (identique au modèle de référence)
TELESTRAT_SLOW static void _telestrat_step(telestrat_t* sys) {
    _telestrat_catch_up(sys);
    bool irq = mos6522via_tick(&sys->via, 4);
    irq |= mos6522via_tick(&sys->via2, 4);
    telestrat_fdc_tick(&sys->fdc, 4);
    mos6551acia_tick(&sys->acia, 4);
    if ((sys->system_ticks & 63) == 0) mos6551acia_poll_rx(&sys->acia);
    // Niveau redonné à chaque pas (le VIA de reload détecte le front entre deux appels)
    mos6522via_set_cb1(&sys->via2, sys->ring);
    irq |= telestrat_fdc_irq(&sys->fdc);
    irq |= mos6551acia_irq(&sys->acia);
    MOS6502CPU_SET_IRQ(&sys->cpu, irq);

    // AY-3-8912 piloté par PA / CA2 / CB2 du VIA 1
    if (mos6522via_get_cb2(&sys->via)) {
        const uint8_t psg_data = mos6522via_get_pa(&sys->via);
        if (mos6522via_get_ca2(&sys->via)) {
            ay38910psg_latch_address(&sys->psg, psg_data);
        } else {
            ay38910psg_write(&sys->psg, psg_data);
        }
    } else {
        mos6522via_set_pa(&sys->via, ay38910psg_read(&sys->psg));
    }

    // PB0-PB2 : ligne du clavier ; PB3 : touche enfoncée
    uint8_t pb = mos6522via_get_pb(&sys->via);
    // Touche dans la ligne sélectionnée, parmi les colonnes actives : les autres
    // lignes n'y changent rien (oric.h testait l'égalité, fausse dès que deux
    // touches de lignes différentes sont enfoncées, comme SHIFT + 8 pour « * »)
    uint8_t line_mask = 1 << (pb & 7);
    if (kbd_scan_lines(&sys->kbd) & line_mask) {
        mos6522via_set_pb(&sys->via, pb | (1 << 3));
    } else {
        mos6522via_set_pb(&sys->via, pb & ~(1 << 3));
    }

    _telestrat_update_joysticks(sys);
    _telestrat_update_printer(sys, pb);

    sys->inputs_dirty = false;
    sys->quiet_until = sys->system_ticks + 4 * (_telestrat_quiet_steps(sys, sys->system_ticks) + 1);
}

// Accès en $03xx : périphériques à jour, puis pas complet au prochain multiple
// de 4 ; sauf pour le FDC et l'ACIA quand la ligne IRQ ne change pas (lecture
// de DRQ, de données...) : seul l'horizon de repos est recalculé
TELESTRAT_SLOW static void _telestrat_io_access(telestrat_t* sys, uint16_t addr) {
#ifdef TELESTRAT_IO_HOOK
    // Journal de diagnostic de la plate-forme (cycle, adresse, sens, donnée écrite)
    TELESTRAT_IO_HOOK(sys->system_ticks, addr, sys->cpu.rw, sys->cpu.rw ? 0 : MOS6502CPU_GET_DATA(&sys->cpu));
#endif
    _telestrat_catch_up(sys);
    const uint8_t reg = addr & 0xFF;
    const bool fdc_acia = (reg >= 0x10 && reg <= 0x14) || reg == 0x18 || (reg >= 0x1C && reg <= 0x1F);
    if (!fdc_acia) {
        _telestrat_io_rw(sys, addr, sys->cpu.rw);
        // Lecture d'un registre de VIA sans effet de bord (DDR, T1CH, latches,
        // T2CH, SR, ACR, PCR, IFR, IER, RA sans poignée de main) : rien ne
        // change, la fenêtre de repos reste valable (ex. : XRING qui scrute l'IFR)
        if (sys->cpu.rw && ((0xFEEC >> (reg & 15)) & 1)) return;
        sys->quiet_until = sys->system_ticks;
        return;
    }
    const bool irq_before = telestrat_fdc_irq(&sys->fdc) || mos6551acia_irq(&sys->acia);
    _telestrat_io_rw(sys, addr, sys->cpu.rw);
    const bool irq_after = telestrat_fdc_irq(&sys->fdc) || mos6551acia_irq(&sys->acia);
    const uint32_t t = sys->system_ticks;
    if (irq_after != irq_before) {
        sys->quiet_until = t;
        return;
    }
    // Prochain pas : au cycle t s'il est multiple de 4 (après cet accès), sinon au suivant
    const uint32_t next_step = (t + 3) & ~3u;
    sys->quiet_until = next_step + 4 * _telestrat_quiet_steps(sys, next_step - 4);
}

// Événements de l'AY dans l'ordre du modèle de référence : canaux (tous les
// 64 cycles), enveloppe (128), échantillon (46)
TELESTRAT_SLOW static void _telestrat_psg_events(telestrat_t* sys) {
    const uint32_t t = sys->system_ticks;
    if ((t & 63) == 0) {
        ay38910psg_tick_channels(&sys->psg);
    }
    if ((t & 127) == 0) {
        ay38910psg_tick_envelope_generator(&sys->psg);
    }
    if (t == sys->psg_next_sample) {
        ay38910psg_tick_sample_generator(&sys->psg);
        if (sys->audio_callback.func) {
            sys->audio_callback.func((uint8_t)(sys->psg.sample * 255.0f), sys->audio_callback.user_data);
        }
        sys->psg_next_sample = t + 46;
    }
    // Prochain événement : multiple de 64 suivant ou prochain échantillon
    uint32_t next = (t | 63) + 1;
    if ((int32_t)(sys->psg_next_sample - next) < 0) next = sys->psg_next_sample;
    sys->psg_next = next;
}

TELESTRAT_HOT void telestrat_tick(telestrat_t* sys) {
    MOS6502CPU_TICK(&sys->cpu);
    const uint16_t addr = MOS6502CPU_GET_ADDR(&sys->cpu);
    if ((addr & 0xFF00) != 0x0300) {
        // RAM et banques : chemin court
        if (sys->cpu.rw) {
            MOS6502CPU_SET_DATA(&sys->cpu, addr >= 0xC000 ? _telestrat_bank_read(sys, addr) : sys->ram[addr]);
        } else {
            uint8_t data = MOS6502CPU_GET_DATA(&sys->cpu);
            if (addr < 0xC000) {
                sys->ram[addr] = data;
                if (addr >= 0x9800) sys->screen_dirty = true;
            } else if (sys->wr_cur) {
                sys->wr_cur[addr - 0xC000] = data;
            }
        }
    } else {
        _telestrat_io_access(sys, addr);
    }

    const uint32_t t = sys->system_ticks;
    if (t == sys->psg_next) _telestrat_psg_events(sys);

    // Périphériques par pas de 4 cycles, sautés au repos
    if ((t & 3) == 0) {
        if ((int32_t)(t - sys->quiet_until) < 0) {
            sys->deferred += 4;
        } else {
            _telestrat_step(sys);
        }
    }

    sys->system_ticks = t + 1;
}

// Vidéo ULA : même image qu'oric_screen_update d'oric.h (vérifié contre le
// modèle de référence par tests/replay.c), avec une table pour les pixels :
// deux pixels (2 bits du motif) et les couleurs d'encre et de papier donnent
// directement l'octet du tampon (deux pixels de 4 bits).
static uint8_t telestrat_pair_lut[256];

static void _telestrat_init_pair_lut(void) {
    for (int i = 0; i < 256; i++) {
        int b = i >> 6, f = (i >> 3) & 7, g = i & 7;
        telestrat_pair_lut[i] = (uint8_t)((((b & 2) ? f : g) << 4) | ((b & 1) ? f : g));
    }
}

TELESTRAT_HOT void telestrat_screen_update(telestrat_t* sys) {
    bool blink_state = sys->blink_counter & 0x20;
    sys->blink_counter = (sys->blink_counter + 1) & 0x3F;
    if (!sys->screen_dirty && (sys->blink_counter & 0x1F) != 0) {
        return;
    }
    if (!telestrat_pair_lut[0xFF]) _telestrat_init_pair_lut();
    const uint8_t* lut = telestrat_pair_lut;
    const uint8_t* ram = sys->ram;

    uint8_t pattr = sys->pattr;
    uint8_t* p = sys->fb;
    for (int y = 0; y < TELESTRAT_SCREEN_HEIGHT; y++) {
        uint8_t lattr = 0;
        uint8_t fgcol = 7;
        uint8_t bgcol = 0;
        const uint8_t* text = ram + 0xBB80 + (y >> 3) * 40;
        const uint8_t* hires = ram + 0xA000 + y * 40;
        const bool hires_row = y < 200;

        for (int x = 0; x < 40; x++) {
            uint8_t ch, pat;
            if ((pattr & TELESTRAT_PATTR_HIRES) && hires_row) {
                ch = pat = hires[x];
            } else {
                ch = text[x];
                int off = (lattr & TELESTRAT_LATTR_DSIZE ? y >> 1 : y) & 7;
                const uint8_t* base;
                if (pattr & TELESTRAT_PATTR_HIRES) {
                    base = ram + ((lattr & TELESTRAT_LATTR_ALT) ? 0x9C00 : 0x9800);
                } else {
                    base = ram + ((lattr & TELESTRAT_LATTR_ALT) ? 0xB800 : 0xB400);
                }
                pat = base[((ch & 0x7F) << 3) | off];
            }

            // Attributs série
            if (!(ch & 0x60)) {
                pat = 0x00;
                switch (ch & 0x18) {
                    case 0x00: fgcol = ch & 7; break;
                    case 0x08: lattr = ch & 7; break;
                    case 0x10: bgcol = ch & 7; break;
                    case 0x18: pattr = ch & 7; break;
                }
            }

            uint8_t c_fgcol = fgcol;
            uint8_t c_bgcol = bgcol;
            if (ch & 0x80) {
                c_bgcol ^= 0x07;
                c_fgcol ^= 0x07;
            }
            if ((lattr & TELESTRAT_LATTR_BLINK) && blink_state) c_fgcol = c_bgcol;

            const uint8_t k = (uint8_t)((c_fgcol << 3) | c_bgcol);
            p[0] = lut[(((pat >> 4) & 3) << 6) | k];
            p[1] = lut[(((pat >> 2) & 3) << 6) | k];
            p[2] = lut[((pat & 3) << 6) | k];
            p += 3;
        }
    }
    sys->pattr = pattr;
    sys->screen_dirty = false;
}

uint32_t telestrat_exec(telestrat_t* sys, uint32_t micro_seconds) {
    CHIPS_ASSERT(sys && sys->valid);
    uint32_t num_ticks = clk_us_to_ticks(TELESTRAT_FREQUENCY, micro_seconds);
    if (0 == sys->debug.callback.func) {
        for (uint32_t ticks = 0; ticks < num_ticks; ticks++) {
            telestrat_tick(sys);
        }
    } else {
        for (uint32_t ticks = 0; (ticks < num_ticks) && !(*sys->debug.stopped); ticks++) {
            telestrat_tick(sys);
            sys->debug.callback.func(sys->debug.callback.user_data, 0);
        }
    }
    telestrat_kbd_update(sys, micro_seconds);
    telestrat_screen_update(sys);
    return num_ticks;
}

// Clavier : matrice de l'Atmos (oric.h ; ESC et FUNCT d'après qwktab dans
// 8912.c d'Oricutron). FUNCT reçoit le code 0x146 (Alt gauche côté hôte).
static void _telestrat_init_key_map(telestrat_t* sys) {
    kbd_init(&sys->kbd, 2);
    const char* keymap =
        // sans shift
        //   01234567 (colonne)
        "7N5V 1X3"   // ligne 0
        "JTRF  QD"   // ligne 1
        "M6B4 Z2C"   // ligne 2
        "K9;-  \\'"  // ligne 3
        " ,.     "   // ligne 4 (oric.h de reload inverse , . et < > : table qwktab d'Oricutron)
        "UIOP  ]["   // ligne 5
        "YHGE ASW"   // ligne 6
        "8L0/   ="   // ligne 7

        /* shift */
        "&n%v !x#"
        "jtrf  qd"
        "m^b$ z@c"
        "k(:_  |\""
        " <>     "
        "uiop  }{"
        "yhge asw"
        "*l)?   +";

    CHIPS_ASSERT(strlen(keymap) == 128);
    kbd_register_modifier(&sys->kbd, 0, 4, 4);  // SHIFT : colonne 4, ligne 4
    kbd_register_modifier(&sys->kbd, 1, 4, 2);  // CTRL : colonne 4, ligne 2
    for (int shift = 0; shift < 2; shift++) {
        for (int column = 0; column < 8; column++) {
            for (int line = 0; line < 8; line++) {
                int c = keymap[shift * 64 + line * 8 + column];
                if (c != 0x20) {
                    kbd_register_key(&sys->kbd, c, column, line, shift ? (1 << 0) : 0);
                }
            }
        }
    }

    kbd_register_key(&sys->kbd, 0x20, 0, 4, 0);   // Espace
    kbd_register_key(&sys->kbd, 0x150, 5, 4, 0);  // Gauche
    kbd_register_key(&sys->kbd, 0x14F, 7, 4, 0);  // Droite
    kbd_register_key(&sys->kbd, 0x151, 6, 4, 0);  // Bas
    kbd_register_key(&sys->kbd, 0x152, 3, 4, 0);  // Haut
    kbd_register_key(&sys->kbd, 0x08, 5, 5, 0);   // DEL
    kbd_register_key(&sys->kbd, 0x0D, 5, 7, 0);   // RETURN
    kbd_register_key(&sys->kbd, 0x1B, 5, 1, 0);   // ESC
    kbd_register_key(&sys->kbd, 0x146, 4, 5, 0);  // FUNCT (8912.c d'Oricutron : SDLK_LALT)

    // CTRL + lettre (codes ASCII 1..26), sauf les codes qui ont leur propre
    // touche : 8 (DEL, pas CTRL+H) et 13 (RETURN, pas CTRL+M)
    const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; letters[i]; i++) {
        if (i + 1 == 0x08 || i + 1 == 0x0D) continue;
        for (int column = 0; column < 8; column++) {
            for (int line = 0; line < 8; line++) {
                if (keymap[line * 8 + column] == letters[i]) {
                    kbd_register_key(&sys->kbd, i + 1, column, line, 2);
                }
            }
        }
    }
}

#endif  // CHIPS_IMPL
