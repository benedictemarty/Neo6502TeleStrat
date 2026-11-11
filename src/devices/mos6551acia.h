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
//   - DTR (b0 de la commande) à 0 : récepteur et interruptions coupés, la
//     broche /IRQ reste inactive même avec un événement d'émission en attente
//     (fiche 6551) ; l'événement garde son bit dans l'état jusqu'à la lecture.
//   - lecture de l'état : n'efface pas la broche /IRQ tant que la donnée reçue
//     n'est pas lue (RDRF actif, IRQ de réception autorisée). Choix de ce
//     code, non vérifié sur matériel (à vérifier).
//   - commande b2-3 = 00 (émetteur coupé) : l'octet écrit est quand même
//     émis ; b2-3 = 11 (BREAK) n'est pas modélisé. Comportement de la fiche
//     non établi pour ces deux cas, TELEMON non examiné (à vérifier).
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

// Parité, trame et écrasement ne sont jamais levés : l'émulation ne reçoit que
// des octets sains et ne propose un octet que si le précédent est lu ; les bits
// restent définis pour l'effacement par les lectures et les instantanés.
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
#define MOS6551_CTL_INTCLK (0x10)  // source d'horloge : sans effet ici (débit toujours interne)
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

// Bits de stop : deux si b7 du contrôle, sauf 8 bits avec parité (un seul
// d'après la fiche) ; le cas 5 bits sans parité (1,5) est rendu par 2. Même
// règle pour la durée (char_cycles) et pour le format d'un UART réel.
static inline uint8_t mos6551acia_stop_bits(const mos6551acia_t* a) {
    const int data_bits = 8 - ((a->control & MOS6551_CTL_WLEN) >> 5);
    const bool parity = (a->command & 0x20) != 0;
    return (a->control & MOS6551_CTL_STOP2) && !(data_bits == 8 && parity) ? 2 : 1;
}

// Durée d'un caractère en cycles CPU : start + données + parité + stop
static inline int32_t mos6551acia_char_cycles(const mos6551acia_t* a) {
    int bits = 1 + (8 - ((a->control & MOS6551_CTL_WLEN) >> 5));
    if (a->command & 0x20) bits++;  // parité active
    bits += mos6551acia_stop_bits(a);
    return (int32_t)((a->cpu_freq * (uint32_t)bits) / mos6551acia_bauds[a->control & MOS6551_CTL_BAUD]);
}

// Format de liaison programmé, pour régler un UART réel (prise RS232 du
// Neo6502). Parité (bits 5-7 de la commande, fiche 6551) : 0 aucune,
// 1 impaire, 2 paire, 3 marque, 4 espace. Bits de stop : voir
// mos6551acia_stop_bits().
typedef struct {
    uint32_t baud;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
} mos6551acia_format_t;

static inline mos6551acia_format_t mos6551acia_format(const mos6551acia_t* a) {
    mos6551acia_format_t f;
    f.baud = mos6551acia_bauds[a->control & MOS6551_CTL_BAUD];
    f.data_bits = (uint8_t)(8 - ((a->control & MOS6551_CTL_WLEN) >> 5));
    f.parity = (a->command & 0x20) ? (uint8_t)(1 + ((a->command >> 6) & 3)) : 0;
    f.stop_bits = mos6551acia_stop_bits(a);
    return f;
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

static inline bool mos6551acia_tx_irq_enabled_(const mos6551acia_t* a) {
    return (a->command & MOS6551_CMD_TXCTL) == 0x04;
}

static inline bool mos6551acia_rx_irq_enabled_(const mos6551acia_t* a) {
    return (a->command & MOS6551_CMD_DTR) && !(a->command & MOS6551_CMD_IRQDIS);
}

// Broche /IRQ = événement d'émission, ou réception pleine et autorisée ;
// bit 7 de l'état = broche ou octet arrivé (même IRQ de réception interdite)
static inline void mos6551acia_update_irq_(mos6551acia_t* a) {
    if (!mos6551acia_tx_irq_enabled_(a)) a->tx_irq = false;
    // DTR = 0 : récepteur et interruptions coupés
    bool irq = (a->command & MOS6551_CMD_DTR) &&
               (a->tx_irq || ((a->status & MOS6551_ST_RXFULL) && mos6551acia_rx_irq_enabled_(a)));
    a->irq = irq;
    if (irq || a->tx_irq || a->rx_event) {
        a->status |= MOS6551_ST_IRQ;
    } else {
        a->status &= ~MOS6551_ST_IRQ;
    }
}

// Passe le registre d'émission dans le registre à décalage s'il est libre ;
// carry (≤ 0) : cycles écoulés au-delà de la fin du caractère précédent,
// retranchés du nouveau pour ne pas dériver en débit
static inline void mos6551acia_load_tsr_(mos6551acia_t* a, int32_t carry) {
    if (a->tsr_busy || !a->tdr_full) return;
    a->tsr = a->tdr;
    a->tdr_full = false;
    a->tsr_busy = true;
    const int32_t cc = mos6551acia_char_cycles(a);
    if (carry < -cc) carry = -cc;
    a->tx_timer = cc + carry;
    a->status |= MOS6551_ST_TXEMPTY;
    if (mos6551acia_tx_irq_enabled_(a)) a->tx_irq = true;
    mos6551acia_update_irq_(a);
}

static inline uint8_t mos6551acia_read(mos6551acia_t* a, uint8_t reg) {
    switch (reg & 3) {
        case 0: {
            uint8_t d = a->rx;
            a->status &= ~(MOS6551_ST_RXFULL | MOS6551_ST_OVERRUN | MOS6551_ST_FRAMING | MOS6551_ST_PARITY);
            mos6551acia_update_irq_(a);
            return d;
        }
        case 1: {
            uint8_t d = a->status;
            // La lecture de l'état efface les événements
            a->tx_irq = false;
            a->rx_event = false;
            mos6551acia_update_irq_(a);
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
            mos6551acia_update_irq_(a);
            mos6551acia_load_tsr_(a, 0);
            break;
        case 1:  // RESET logiciel : commande b0-4 à 0 sauf IRQ interdite, écrasement effacé
            a->command = (a->command & MOS6551_CMD_PARITY) | MOS6551_CMD_IRQDIS;
            a->status &= ~MOS6551_ST_OVERRUN;
            mos6551acia_update_irq_(a);
            break;
        case 2:
            a->command = data;
            if (mos6551acia_tx_irq_enabled_(a) && (a->status & MOS6551_ST_TXEMPTY)) a->tx_irq = true;
            mos6551acia_update_irq_(a);
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
            mos6551acia_load_tsr_(a, a->tx_timer);
        }
    }
    // Réception : le temps passe ; l'arrivée d'un octet est testée par
    // mos6551acia_poll_rx()
    if (a->rx_timer > 0) a->rx_timer -= cycles;
}

// Interroge le correspondant (récepteur actif, octet précédent lu, durée d'un
// caractère écoulée). Le système l'appelle tous les 64 cycles : assez fin
// devant la durée d'un caractère (8,3 ms à 1200 bauds), et cela laisse le
// système au repos entre deux interrogations.
static inline void mos6551acia_poll_rx(mos6551acia_t* a) {
    if (a->rx_timer <= 0 && !(a->status & MOS6551_ST_RXFULL) && (a->command & MOS6551_CMD_DTR) && a->rx_cb) {
        int c = a->rx_cb(a->user_data);
        if (c >= 0) {
            a->rx = (uint8_t)c & mos6551acia_mask(a);
            a->status |= MOS6551_ST_RXFULL;
            a->rx_event = true;
            a->rx_timer = mos6551acia_char_cycles(a);
            // Écho : l'octet reçu repart, sauf si un octet attend déjà dans le
            // registre d'émission (l'écho est alors perdu, jamais l'octet du logiciel)
            if ((a->command & MOS6551_CMD_ECHO) && !a->tdr_full) {
                a->tdr = a->rx;
                a->tdr_full = true;
                mos6551acia_load_tsr_(a, 0);
            }
            mos6551acia_update_irq_(a);
        }
    }
}

// Instantané portable : champs de taille fixe, sans les rappels ni user_data
// (pointeurs de l'hôte, qui ne se copient pas d'une machine à l'autre). Le
// chargement conserve les rappels de l'ACIA destinataire.
typedef struct {
    uint8_t rx, status, command, control, tdr, tdr_full, tsr, tsr_busy, irq, tx_irq, rx_event, reserved;
    int32_t tx_timer, rx_timer;
    uint32_t cpu_freq;
} mos6551acia_snapshot_t;

_Static_assert(sizeof(mos6551acia_snapshot_t) == 24, "instantané ACIA : taille fixe");

static inline void mos6551acia_snapshot_save(const mos6551acia_t* a, mos6551acia_snapshot_t* s) {
    s->rx = a->rx;
    s->status = a->status;
    s->command = a->command;
    s->control = a->control;
    s->tdr = a->tdr;
    s->tdr_full = a->tdr_full;
    s->tsr = a->tsr;
    s->tsr_busy = a->tsr_busy;
    s->irq = a->irq;
    s->tx_irq = a->tx_irq;
    s->rx_event = a->rx_event;
    s->reserved = 0;
    s->tx_timer = a->tx_timer;
    s->rx_timer = a->rx_timer;
    s->cpu_freq = a->cpu_freq;
}

static inline void mos6551acia_snapshot_load(mos6551acia_t* a, const mos6551acia_snapshot_t* s) {
    a->rx = s->rx;
    a->status = s->status;
    a->command = s->command;
    a->control = s->control;
    a->tdr = s->tdr;
    a->tdr_full = s->tdr_full != 0;
    a->tsr = s->tsr;
    a->tsr_busy = s->tsr_busy != 0;
    a->irq = s->irq != 0;
    a->tx_irq = s->tx_irq != 0;
    a->rx_event = s->rx_event != 0;
    a->tx_timer = s->tx_timer;
    a->rx_timer = s->rx_timer;
    a->cpu_freq = s->cpu_freq ? s->cpu_freq : 1000000;
}
