#pragma once

// telestrat_fdc.h
//
// Contrôleur de disquettes intégré du Telestrat : logique Microdisc autour
// d'un WD1793 (devices/wd1793.h).
//
//   $0310-$0313  registres du WD1793
//   $0314        écriture : bit 0 INTENA, bit 4 face, bits 5-6 lecteur
//                lecture  : bit 7 = /INTRQ (0 = interruption en attente)
//   $0318        lecture  : bit 7 = /DRQ
//
// Décodage d'après Oricutron (disk.c : microdisc_read/microdisc_write).
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

#include <stdint.h>
#include <stdbool.h>

#define TELESTRAT_FDC_CTRL_INTENA (0x01)
#define TELESTRAT_FDC_CTRL_SIDE   (0x10)
#define TELESTRAT_FDC_CTRL_DRIVE  (0x60)

typedef struct {
    wd1793_t wd;
    uint8_t ctrl;  // $0314 en écriture
} telestrat_fdc_t;

static inline void telestrat_fdc_reset(telestrat_fdc_t* f) {
    wd1793_reset(&f->wd);
    f->ctrl = 0;
}

// Ligne IRQ vers le 6502 (active si INTENA et INTRQ)
static inline bool telestrat_fdc_irq(const telestrat_fdc_t* f) {
    return (f->ctrl & TELESTRAT_FDC_CTRL_INTENA) && f->wd.intrq;
}

static inline void telestrat_fdc_tick(telestrat_fdc_t* f, int cycles) { wd1793_tick(&f->wd, cycles); }

// reg : 0..15 ($0310-$031F) ; retourne false si l'adresse n'est pas au FDC
static inline bool telestrat_fdc_read(telestrat_fdc_t* f, uint8_t reg, uint8_t* out) {
    if (reg < 4) {
        *out = wd1793_read(&f->wd, reg);
        return true;
    }
    switch (reg) {
        case 4: *out = (f->wd.intrq ? 0x00 : 0x80) | 0x7F; return true;
        case 8: *out = (f->wd.drq ? 0x00 : 0x80) | 0x7F; return true;
        default: return false;
    }
}

static inline bool telestrat_fdc_write(telestrat_fdc_t* f, uint8_t reg, uint8_t data) {
    if (reg < 4) {
        wd1793_write(&f->wd, reg, data);
        return true;
    }
    switch (reg) {
        case 4:
            f->ctrl = data;
            f->wd.drive = (data & TELESTRAT_FDC_CTRL_DRIVE) >> 5;
            f->wd.side = (data & TELESTRAT_FDC_CTRL_SIDE) ? 1 : 0;
            return true;
        case 8: return true;
        default: return false;
    }
}
