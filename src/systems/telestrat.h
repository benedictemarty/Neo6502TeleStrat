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
//   $031C-$031F  ACIA 6551 (registres seuls au sprint 1, sans liaison)
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
    TELESTRAT_BANK_EMPTY = 0,  // pas de cartouche : lit $FF, écritures ignorées
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
    uint8_t bank;  // banque visible en $C000-$FFFF

    uint8_t joy[2];  // [0] = port droit (PB7), [1] = port gauche (PB6)

    telestrat_printer_t printer;
    void* printer_user_data;
    bool strobe;          // dernier niveau de PB4
    int32_t printer_ack;  // cycles restants de l'impulsion ACK

    int blink_counter;
    uint8_t pattr;
    uint8_t fb[TELESTRAT_FRAMEBUFFER_SIZE];
    bool screen_dirty;

    uint32_t system_ticks;
    uint8_t psg_sample_div;
} telestrat_t;

void telestrat_init(telestrat_t* sys, const telestrat_desc_t* desc);
void telestrat_discard(telestrat_t* sys);
void telestrat_reset(telestrat_t* sys);
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

void telestrat_set_joystick(telestrat_t* sys, int port, uint8_t state);
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

void telestrat_select_bank(telestrat_t* sys, uint8_t bank) { sys->bank = bank & 7; }

void telestrat_init(telestrat_t* sys, const telestrat_desc_t* desc) {
    CHIPS_ASSERT(sys && desc);
    if (desc->debug.callback.func) {
        CHIPS_ASSERT(desc->debug.stopped);
    }

    memset(sys, 0, sizeof(telestrat_t));
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

    telestrat_select_bank(sys, TELESTRAT_BOOT_BANK);

    _telestrat_init_key_map(sys);
    sys->joy[0] = sys->joy[1] = 0;
    sys->printer = desc->printer.func;
    sys->printer_user_data = desc->printer.user_data;
}

void telestrat_discard(telestrat_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    sys->valid = false;
}

void telestrat_nmi(telestrat_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    MOS6502CPU_NMI(&sys->cpu);
}

void telestrat_reset(telestrat_t* sys) {
    CHIPS_ASSERT(sys && sys->valid);
    mos6522via_reset(&sys->via);
    mos6522via_reset(&sys->via2);
    ay38910psg_reset(&sys->psg);
    telestrat_fdc_reset(&sys->fdc);
    mos6551acia_reset(&sys->acia);
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

static inline uint8_t _telestrat_bank_read(telestrat_t* sys, uint16_t addr) {
    const uint8_t* p = sys->bank_rd[sys->bank];
    return p ? p[addr - 0xC000] : 0xFF;
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

static inline void _telestrat_mem_rw(telestrat_t* sys, uint16_t addr, bool rw) {
    if ((addr & 0xFF00) == 0x0300) {
        _telestrat_io_rw(sys, addr, rw);
    } else if (rw) {
        MOS6502CPU_SET_DATA(&sys->cpu, addr >= 0xC000 ? _telestrat_bank_read(sys, addr) : sys->ram[addr]);
    } else {
        uint8_t data = MOS6502CPU_GET_DATA(&sys->cpu);
        if (addr < 0xC000) {
            sys->ram[addr] = data;
            if (addr >= 0x9800) sys->screen_dirty = true;
        } else if (sys->bank_wr[sys->bank]) {
            sys->bank_wr[sys->bank][addr - 0xC000] = data;
        }
    }
}

void telestrat_set_joystick(telestrat_t* sys, int port, uint8_t state) {
    if (port >= 0 && port < 2) sys->joy[port] = state & 0x1F;
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

TELESTRAT_HOT void telestrat_tick(telestrat_t* sys) {
    MOS6502CPU_TICK(&sys->cpu);
    _telestrat_mem_rw(sys, MOS6502CPU_GET_ADDR(&sys->cpu), sys->cpu.rw);

    // PSG
    if ((sys->system_ticks & 63) == 0) {
        ay38910psg_tick_channels(&sys->psg);
    }
    if ((sys->system_ticks & 127) == 0) {
        ay38910psg_tick_envelope_generator(&sys->psg);
    }
    if (++sys->psg_sample_div == 46) {
        ay38910psg_tick_sample_generator(&sys->psg);
        if (sys->audio_callback.func) {
            sys->audio_callback.func((uint8_t)(sys->psg.sample * 255.0f), sys->audio_callback.user_data);
        }
        sys->psg_sample_div = 0;
    }

    // VIA 1 et 2, par pas de 4 cycles comme oric.h
    if ((sys->system_ticks & 3) == 0) {
        bool irq = mos6522via_tick(&sys->via, 4);
        irq |= mos6522via_tick(&sys->via2, 4);
        telestrat_fdc_tick(&sys->fdc, 4);
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
        uint8_t line_mask = 1 << (pb & 7);
        if (kbd_scan_lines(&sys->kbd) == line_mask) {
            mos6522via_set_pb(&sys->via, pb | (1 << 3));
        } else {
            mos6522via_set_pb(&sys->via, pb & ~(1 << 3));
        }

        _telestrat_update_joysticks(sys);
        _telestrat_update_printer(sys, pb);
    }

    sys->system_ticks++;
}

// Vidéo ULA : reprise telle quelle d'oric.h (oric_screen_update)
void telestrat_screen_update(telestrat_t* sys) {
    bool blink_state = sys->blink_counter & 0x20;
    sys->blink_counter = (sys->blink_counter + 1) & 0x3F;
    if (!sys->screen_dirty && (sys->blink_counter & 0x1F) != 0) {
        return;
    }

    uint8_t pattr = sys->pattr;
    for (int y = 0; y < TELESTRAT_SCREEN_HEIGHT; y++) {
        uint8_t lattr = 0;
        uint8_t fgcol = 7;
        uint8_t bgcol = 0;
        uint8_t* p = &sys->fb[y * (TELESTRAT_SCREEN_WIDTH / 2)];

        for (int x = 0; x < 40; x++) {
            uint8_t ch, pat;
            if ((pattr & TELESTRAT_PATTR_HIRES) && y < 200) {
                ch = pat = sys->ram[0xA000 + y * 40 + x];
            } else {
                ch = sys->ram[0xBB80 + (y >> 3) * 40 + x];
                int off = (lattr & TELESTRAT_LATTR_DSIZE ? y >> 1 : y) & 7;
                const uint8_t* base;
                if (pattr & TELESTRAT_PATTR_HIRES) {
                    base = sys->ram + ((lattr & TELESTRAT_LATTR_ALT) ? 0x9C00 : 0x9800);
                } else {
                    base = sys->ram + ((lattr & TELESTRAT_LATTR_ALT) ? 0xB800 : 0xB400);
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

            *p = (pat & 0x20 ? c_fgcol : c_bgcol) << 4;
            *p++ |= (pat & 0x10 ? c_fgcol : c_bgcol);
            *p = (pat & 0x08 ? c_fgcol : c_bgcol) << 4;
            *p++ |= (pat & 0x04 ? c_fgcol : c_bgcol);
            *p = (pat & 0x02 ? c_fgcol : c_bgcol) << 4;
            *p++ |= (pat & 0x01 ? c_fgcol : c_bgcol);
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
    kbd_update(&sys->kbd, micro_seconds);
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
        " <>     "   // ligne 4
        "UIOP  ]["   // ligne 5
        "YHGE ASW"   // ligne 6
        "8L0/   ="   // ligne 7

        /* shift */
        "&n%v !x#"
        "jtrf  qd"
        "m^b$ z@c"
        "k(:_  |\""
        " ,.     "
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

    // CTRL + lettre (codes ASCII 1..26)
    const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; letters[i]; i++) {
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
