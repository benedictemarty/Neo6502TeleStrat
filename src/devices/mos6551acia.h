#pragma once

// mos6551acia.h
//
// ACIA 6551 (liaison série asynchrone) du Telestrat, $031C-$031F.
//
// Écrit d'après la fiche technique du 6551 :
//   registre 0 : lecture = donnée reçue, écriture = donnée à émettre
//   registre 1 : lecture = état, écriture = RESET logiciel
//   registre 2 : commande (b0 DTR, b1 IRQ réception interdite, b2-3 émission,
//                b4 écho, b5-7 parité)
//   registre 3 : contrôle (b0-3 débit, b4 horloge interne, b5-6 longueur,
//                b7 bits de stop)
//   état : b0 parité, b1 trame, b2 écrasement, b3 réception pleine,
//          b4 émission vide, b5 /DCD, b6 /DSR, b7 IRQ
//
// Émission à double tampon : l'octet écrit passe dans le registre à décalage
// dès qu'il est libre (émission vide tout de suite), puis sort au rythme du
// débit programmé par `tx_cb`. Réception : l'octet proposé par `rx_cb` entre
// au rythme du débit, seulement si le précédent a été lu (pas d'écrasement :
// le correspondant est mis en attente, ce qui convient aux sources émulées).
// Interruptions : le bit 7 de l'état note un événement (octet reçu, registre
// d'émission vidé avec l'IRQ d'émission autorisée) et s'efface à la lecture de
// l'état ; la broche /IRQ n'est activée que pour les sources autorisées.
//   - réception : broche active tant que la donnée n'est pas lue (b1 = 0 et
//     DTR) ; le bit 7 est levé à l'arrivée même si b1 = 1. Hypothèse déduite
//     de TELEMON : pendant le service TELEMATIC la commande vaut $63/$67 (IRQ
//     de réception interdite) et les touches du correspondant ne sont lues que
//     par sa routine série ($C8C0), appelée par le timer, qui teste le bit 7 ;
//   - émission (b2-3 = 01) : événement, levé quand le registre d'émission se
//     vide, ou quand la commande est écrite avec l'IRQ d'émission autorisée et
//     le registre vide ; effacé par la lecture de l'état.
// TELEMON (routine série $C8C0) réécrit la commande à chaque octet mis en
// file et n'émet que sur IRQ ; une IRQ d'émission de niveau l'étoufferait
// (il compte 65536 appels avant de couper l'émission, tampon vide).
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

#define MOS6551_ST_PARITY  (0x01)
#define MOS6551_ST_FRAMING (0x02)
#define MOS6551_ST_OVERRUN (0x04)
#define MOS6551_ST_RXFULL  (0x08)
#define MOS6551_ST_TXEMPTY (0x10)
#define MOS6551_ST_NDCD    (0x20)
#define MOS6551_ST_NDSR    (0x40)
#define MOS6551_ST_IRQ     (0x80)

#define MOS6551_CMD_DTR    (0x01)
#define MOS6551_CMD_IRQDIS (0x02)  // IRQ de réception interdite
#define MOS6551_CMD_TXCTL  (0x0C)
#define MOS6551_CMD_ECHO   (0x10)
#define MOS6551_CMD_PARITY (0xE0)

#define MOS6551_CTL_BAUD   (0x0F)
#define MOS6551_CTL_INTCLK (0x10)
#define MOS6551_CTL_WLEN   (0x60)
#define MOS6551_CTL_STOP2  (0x80)

// Octet émis vers le correspondant
typedef void (*mos6551acia_tx_t)(uint8_t data, void* user_data);
// Octet reçu du correspondant : retourne -1 s'il n'y en a pas
typedef int (*mos6551acia_rx_t)(void* user_data);

typedef struct {
    uint8_t rx;
    uint8_t status;
    uint8_t command;
    uint8_t control;
    uint8_t tdr;       // registre d'émission
    bool tdr_full;
    uint8_t tsr;       // registre à décalage d'émission
    bool tsr_busy;
    int32_t tx_timer;  // cycles avant la fin de l'octet en cours d'émission
    int32_t rx_timer;  // cycles avant l'arrivée possible du prochain octet
    bool irq;
    bool tx_irq;  // événement d'IRQ d'émission en attente
    bool rx_event;  // octet arrivé depuis la dernière lecture de l'état
    uint32_t cpu_freq;
    mos6551acia_tx_t tx_cb;
    mos6551acia_rx_t rx_cb;
    void* user_data;
} mos6551acia_t;

// Débits du générateur interne (fiche 6551) ; 0 = horloge externe 16x (non
// câblée ici : on prend 1200 bauds pour ne pas bloquer)
static const uint16_t mos6551acia_bauds[16] = {1200, 50, 75, 110, 135, 150, 300, 600,
                                               1200, 1800, 2400, 3600, 4800, 7200, 9600, 19200};

// Durée d'un caractère en cycles CPU : start + données + parité + stop
static inline int32_t mos6551acia_char_cycles(const mos6551acia_t* a) {
    int bits = 1 + (8 - ((a->control & MOS6551_CTL_WLEN) >> 5));
    if (a->command & 0x20) bits++;  // parité active
    bits += (a->control & MOS6551_CTL_STOP2) ? 2 : 1;
    return (int32_t)((a->cpu_freq * (uint32_t)bits) / mos6551acia_bauds[a->control & MOS6551_CTL_BAUD]);
}

static inline uint8_t mos6551acia_mask(const mos6551acia_t* a) {
    return (uint8_t)(0xFF >> ((a->control & MOS6551_CTL_WLEN) >> 5));
}

static inline void mos6551acia_reset(mos6551acia_t* a) {
    a->rx = 0;
    a->status = MOS6551_ST_TXEMPTY;
    a->command = MOS6551_CMD_IRQDIS;
    a->control = 0;
    a->tdr_full = false;
    a->tsr_busy = false;
    a->tx_timer = 0;
    a->rx_timer = 0;
    a->irq = false;
    a->tx_irq = false;
    a->rx_event = false;
    if (!a->cpu_freq) a->cpu_freq = 1000000;
}

static inline bool mos6551acia_irq(const mos6551acia_t* a) { return a->irq; }

static inline bool _mos6551acia_tx_irq_enabled(const mos6551acia_t* a) {
    return (a->command & MOS6551_CMD_TXCTL) == 0x04;
}

static inline bool _mos6551acia_rx_irq_enabled(const mos6551acia_t* a) {
    return (a->command & MOS6551_CMD_DTR) && !(a->command & MOS6551_CMD_IRQDIS);
}

// Broche /IRQ = événement d'émission, ou réception pleine et autorisée ;
// bit 7 de l'état = broche ou octet arrivé (même IRQ de réception interdite)
static inline void _mos6551acia_update_irq(mos6551acia_t* a) {
    if (!_mos6551acia_tx_irq_enabled(a)) a->tx_irq = false;
    bool irq = a->tx_irq || ((a->status & MOS6551_ST_RXFULL) && _mos6551acia_rx_irq_enabled(a));
    a->irq = irq;
    if (irq || a->rx_event) {
        a->status |= MOS6551_ST_IRQ;
    } else {
        a->status &= ~MOS6551_ST_IRQ;
    }
}

// Passe le registre d'émission dans le registre à décalage s'il est libre
static inline void _mos6551acia_load_tsr(mos6551acia_t* a) {
    if (a->tsr_busy || !a->tdr_full) return;
    a->tsr = a->tdr;
    a->tdr_full = false;
    a->tsr_busy = true;
    a->tx_timer = mos6551acia_char_cycles(a);
    a->status |= MOS6551_ST_TXEMPTY;
    if (_mos6551acia_tx_irq_enabled(a)) a->tx_irq = true;
    _mos6551acia_update_irq(a);
}

static inline uint8_t mos6551acia_read(mos6551acia_t* a, uint8_t reg) {
    switch (reg & 3) {
        case 0: {
            uint8_t d = a->rx;
            a->status &= ~(MOS6551_ST_RXFULL | MOS6551_ST_OVERRUN | MOS6551_ST_FRAMING | MOS6551_ST_PARITY);
            _mos6551acia_update_irq(a);
            return d;
        }
        case 1: {
            uint8_t d = a->status;
            // La lecture de l'état efface les événements
            a->tx_irq = false;
            a->rx_event = false;
            _mos6551acia_update_irq(a);
            return d;
        }
        case 2: return a->command;
        default: return a->control;
    }
}

static inline void mos6551acia_write(mos6551acia_t* a, uint8_t reg, uint8_t data) {
    switch (reg & 3) {
        case 0:
            a->tdr = data & mos6551acia_mask(a);
            a->tdr_full = true;
            a->status &= ~MOS6551_ST_TXEMPTY;
            a->tx_irq = false;
            _mos6551acia_update_irq(a);
            _mos6551acia_load_tsr(a);
            break;
        case 1:  // RESET logiciel : commande b0-4 à 0 sauf IRQ interdite, écrasement effacé
            a->command = (a->command & MOS6551_CMD_PARITY) | MOS6551_CMD_IRQDIS;
            a->status &= ~MOS6551_ST_OVERRUN;
            _mos6551acia_update_irq(a);
            break;
        case 2:
            a->command = data;
            if (_mos6551acia_tx_irq_enabled(a) && (a->status & MOS6551_ST_TXEMPTY)) a->tx_irq = true;
            _mos6551acia_update_irq(a);
            break;
        default: a->control = data; break;
    }
}

// Avance de `cycles` cycles CPU
static inline void mos6551acia_tick(mos6551acia_t* a, int cycles) {
    // Émission
    if (a->tsr_busy) {
        a->tx_timer -= cycles;
        if (a->tx_timer <= 0) {
            a->tsr_busy = false;
            if (a->tx_cb) a->tx_cb(a->tsr, a->user_data);
            _mos6551acia_load_tsr(a);
        }
    }
    // Réception (seulement récepteur actif : DTR)
    if (a->rx_timer > 0) a->rx_timer -= cycles;
    if (a->rx_timer <= 0 && !(a->status & MOS6551_ST_RXFULL) && (a->command & MOS6551_CMD_DTR) && a->rx_cb) {
        int c = a->rx_cb(a->user_data);
        if (c >= 0) {
            a->rx = (uint8_t)c & mos6551acia_mask(a);
            a->status |= MOS6551_ST_RXFULL;
            a->rx_event = true;
            a->rx_timer = mos6551acia_char_cycles(a);
            if (a->command & MOS6551_CMD_ECHO) {
                a->tdr = a->rx;
                a->tdr_full = true;
                _mos6551acia_load_tsr(a);
            }
            _mos6551acia_update_irq(a);
        }
    }
}
