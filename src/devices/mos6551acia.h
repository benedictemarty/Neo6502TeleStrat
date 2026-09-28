#pragma once

// mos6551acia.h
//
// ACIA 6551 du Telestrat ($031C-$031F), minimal : registres, valeurs au
// RESET et effets de bord des lectures/écritures d'après 6551.c d'Oricutron.
// Sprint 1 : pas de liaison série (aucun octet reçu ; un octet émis est passé
// à `tx_cb` s'il est défini puis le registre d'émission redevient vide).
// La liaison modem (Minitel, Telematic) viendra avec un vrai back-end.
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

#define MOS6551_ST_TXEMPTY (0x10)
#define MOS6551_ST_RXFULL  (0x08)
#define MOS6551_ST_OVERRUN (0x04)
#define MOS6551_ST_IRQ     (0x80)
#define MOS6551_CMD_IRQDIS (0x02)
#define MOS6551_CMD_PARITY (0xE0)

typedef void (*mos6551acia_tx_t)(uint8_t data, void* user_data);

typedef struct {
    uint8_t rx;
    uint8_t status;
    uint8_t command;
    uint8_t control;
    mos6551acia_tx_t tx_cb;
    void* user_data;
} mos6551acia_t;

static inline void mos6551acia_reset(mos6551acia_t* a) {
    a->rx = 0;
    a->status = MOS6551_ST_TXEMPTY;
    a->command = MOS6551_CMD_IRQDIS;
    a->control = 0;
}

static inline bool mos6551acia_irq(const mos6551acia_t* a) { return (a->status & MOS6551_ST_IRQ) != 0; }

static inline uint8_t mos6551acia_read(mos6551acia_t* a, uint8_t reg) {
    switch (reg & 3) {
        case 0: {
            uint8_t d = a->rx;
            a->status &= ~(MOS6551_ST_RXFULL | MOS6551_ST_OVERRUN | 0x03);
            return d;
        }
        case 1: {
            uint8_t d = a->status;
            a->status &= ~MOS6551_ST_IRQ;
            return d;
        }
        case 2: return a->command;
        default: return a->control;
    }
}

static inline void mos6551acia_write(mos6551acia_t* a, uint8_t reg, uint8_t data) {
    switch (reg & 3) {
        case 0:
            if (a->tx_cb) a->tx_cb(data, a->user_data);
            a->status |= MOS6551_ST_TXEMPTY;
            break;
        case 1:  // RESET logiciel
            a->command = (a->command & MOS6551_CMD_PARITY) | MOS6551_CMD_IRQDIS;
            a->status &= ~MOS6551_ST_OVERRUN;
            break;
        case 2: a->command = data; break;
        default: a->control = data; break;
    }
}
