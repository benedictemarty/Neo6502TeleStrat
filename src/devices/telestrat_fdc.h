#pragma once

// telestrat_fdc.h
//
// Contrôleur de disquettes intégré du Telestrat (logique Microdisc autour
// d'un WD1793) : registres du WD en $0310-$0313, registre de contrôle en
// $0314 (écriture : bit 0 INTENA, bit 4 face, bits 5-6 lecteur ; lecture :
// bit 7 = /INTRQ), $0318 (lecture : bit 7 = /DRQ).
//
// Sprint 1 : aucun lecteur n'a de disquette. Le WD répond « non prêt » et
// lève INTRQ à la fin de chaque commande, ce qui suffit au démarrage de
// Telemon. La lecture d'images .dsk (MFM_DISK) est l'objet du sprint 2.
//
// D'après le décodage d'Oricutron (disk.c : microdisc_read/microdisc_write).
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

#define WD1793_ST_BUSY      (0x01)
#define WD1793_ST_TRACK0    (0x04)
#define WD1793_ST_RNF       (0x10)
#define WD1793_ST_NOT_READY (0x80)

typedef struct {
    uint8_t status;
    uint8_t track;
    uint8_t sector;
    uint8_t data;
    uint8_t ctrl;  // $0314 en écriture
    bool intrq;
    bool drq;
} telestrat_fdc_t;

static inline void telestrat_fdc_reset(telestrat_fdc_t* f) {
    f->status = WD1793_ST_NOT_READY | WD1793_ST_TRACK0;
    f->track = 0;
    f->sector = 1;
    f->data = 0;
    f->ctrl = 0;
    f->intrq = false;
    f->drq = false;
}

// Ligne IRQ vers le 6502 (active si INTENA et INTRQ)
static inline bool telestrat_fdc_irq(const telestrat_fdc_t* f) {
    return (f->ctrl & TELESTRAT_FDC_CTRL_INTENA) && f->intrq;
}

static inline void _telestrat_fdc_command(telestrat_fdc_t* f, uint8_t cmd) {
    if ((cmd & 0xF0) == 0xD0) {
        // Type IV : force interrupt
        f->intrq = (cmd & 0x0F) != 0;
        f->status &= ~WD1793_ST_BUSY;
        return;
    }
    if (!(cmd & 0x80)) {
        // Type I : restore / seek / step (la tête bouge même sans disquette)
        switch (cmd & 0x70) {
            case 0x00: f->track = 0; break;                              // restore
            case 0x10: f->track = f->data; break;                        // seek
            case 0x40: case 0x50: if (f->track < 255) f->track++; break;  // step in
            case 0x60: case 0x70: if (f->track > 0) f->track--; break;    // step out
            default: break;                                              // step (sens précédent)
        }
        f->status = WD1793_ST_NOT_READY | (f->track == 0 ? WD1793_ST_TRACK0 : 0);
    } else {
        // Types II et III : pas de disquette -> non prêt, secteur introuvable
        f->status = WD1793_ST_NOT_READY | WD1793_ST_RNF;
    }
    f->drq = false;
    f->intrq = true;
}

// reg : 0..15 ($0310-$031F) ; retourne false si l'adresse n'est pas au FDC
static inline bool telestrat_fdc_read(telestrat_fdc_t* f, uint8_t reg, uint8_t* out) {
    switch (reg) {
        case 0: *out = f->status; f->intrq = false; return true;
        case 1: *out = f->track; return true;
        case 2: *out = f->sector; return true;
        case 3: *out = f->data; f->drq = false; return true;
        case 4: *out = (f->intrq ? 0x00 : 0x80) | 0x7F; return true;
        case 8: *out = (f->drq ? 0x00 : 0x80) | 0x7F; return true;
        default: return false;
    }
}

static inline bool telestrat_fdc_write(telestrat_fdc_t* f, uint8_t reg, uint8_t data) {
    switch (reg) {
        case 0: _telestrat_fdc_command(f, data); return true;
        case 1: f->track = data; return true;
        case 2: f->sector = data; return true;
        case 3: f->data = data; f->drq = false; return true;
        case 4: f->ctrl = data; return true;
        case 8: return true;
        default: return false;
    }
}
