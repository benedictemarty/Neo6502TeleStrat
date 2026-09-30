#pragma once

// telestrat_fdc.h
//
// Contrôleur de disquettes intégré du Telestrat : logique Microdisc autour
// du WD1793 du socle reload (devices/wd1793.h, images MFM_DISK par
// devices/oric_dsk.h : en mémoire ou en flux piste par piste). Son
// implémentation est compilée avec CHIPS_IMPL, une seule fois par programme.
//
// Le WD1793 avance par microseconde (1 µs = 1 cycle à 1 MHz) ; le Telestrat
// l'avance par pas de 4 cycles (telestrat_fdc_tick). Délais du socle
// (d'Oricutron) : 60 µs avant le premier octet, 32 µs par octet, 180 µs entre
// deux secteurs, 20 µs pour une commande de type I. Comportements d'Oricutron
// : FORCE INTERRUPT lève toujours INTRQ ; NOT READY levé pendant les
// commandes de types II et III ; sans disque, une commande de type I rend
// NOT READY + SEEK ERROR (sans TRACK 0).
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

// Mise sous tension : contrôleur vide (aucun disque), puis RESET
static inline void telestrat_fdc_init(telestrat_fdc_t* f) {
    wd1793_init(&f->wd);
    f->ctrl = 0;
}

// RESET : les disques insérés et la position des têtes restent
static inline void telestrat_fdc_reset(telestrat_fdc_t* f) {
    wd1793_reset(&f->wd);
    f->ctrl = 0;
    wd1793_select(&f->wd, 0, 0);
}

// Ligne IRQ vers le 6502 (active si INTENA et INTRQ)
static inline bool telestrat_fdc_irq(const telestrat_fdc_t* f) {
    return (f->ctrl & TELESTRAT_FDC_CTRL_INTENA) && f->wd.intrq;
}

// Avance de `cycles` cycles CPU (= µs à 1 MHz). Chemin chaud, sans appel dans
// les cas courants (sur le RP2040, wd1793_tick_n est en flash) : au repos ou
// en attente du processeur, wd1793_tick_n ne fait rien ; pendant une attente
// plus longue que `cycles`, il ne fait que la décompter
static inline void telestrat_fdc_tick(telestrat_fdc_t* f, uint32_t cycles) {
    wd1793_t* w = &f->wd;
    if (w->state == WD1793_IDLE || w->state == WD1793_CPU) return;
    if (w->state == WD1793_WAIT && w->wait_us > cycles) {
        w->wait_us -= cycles;
        return;
    }
    wd1793_tick_n(w, cycles);
}

// Pas de 4 cycles qui peuvent passer sans changement de DRQ, d'INTRQ ni du
// statut (UINT32_MAX : aucun événement attendu). Un événement à la
// microseconde n + 1 tombe dans le pas n / 4 + 1 : les n / 4 premiers sont sautés
static inline uint32_t telestrat_fdc_quiet_steps(const telestrat_fdc_t* f) {
    if (f->wd.state == WD1793_IDLE || f->wd.state == WD1793_CPU) return UINT32_MAX;
    const uint32_t us = wd1793_next_event_us(&f->wd);
    return us == WD1793_NO_EVENT ? UINT32_MAX : us / 4;
}

// Commande en cours (accès disque) : pas d'instantané
static inline bool telestrat_fdc_busy(const telestrat_fdc_t* f) { return f->wd.state != WD1793_IDLE; }

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
            wd1793_select(&f->wd, (data & TELESTRAT_FDC_CTRL_DRIVE) >> 5, (data & TELESTRAT_FDC_CTRL_SIDE) ? 1 : 0);
            return true;
        case 8: return true;
        default: return false;
    }
}
