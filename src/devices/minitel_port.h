#pragma once

// minitel_port.h
//
// Ce qui est branché sur la liaison série (ACIA) du Telestrat pour la
// télématique : un Minitel dont le modem donne accès à la ligne, plus le
// détecteur de sonnerie de la ligne (entrée CB1 du VIA 2).
//
// Comportement relevé dans TELEMON 2.4 (ROM, voir docs/ARCHITECTURE.md) :
//   - XLIGNE ($EF20) envoie au Minitel ESC 9 $6F puis ESC 9 $68 (PRO1 :
//     « opposition » puis « connexion ») ; XDECON ($EF3F) envoie ESC 9 $67
//     (« déconnexion ») ;
//   - l'attente de connexion ($EF47) guette la réponse $13 $53 du Minitel ;
//   - XRING ($EEA5) reconnaît la sonnerie : rafales d'impulsions à 50 Hz sur
//     CB1 (périodes de 19 à 21 ms mesurées au timer 2), séparées de silences.
// Hypothèses (non vérifiées sur matériel) : cadence de sonnerie française
// 1,5 s / 3,5 s ; à la perte de la porteuse, le Minitel signale $13 $54 ;
// la porteuse s'établit MINITEL_CONNECT_US après CONNEXION (négociation du
// modem). Ce délai est nécessaire : l'attente de TELEMON ($EF47) vide le
// tampon de réception puis patiente 0,1 s avant de guetter $13 $53.
//
// La ligne elle-même est fournie par la plate-forme (minitel_line_t) : TCP
// sur PC, modem Hayes (PicoWiFiModemUSB) sur le Neo6502.
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

#define MINITEL_ESC       0x1B
#define MINITEL_PRO1      0x39
#define MINITEL_PRO2      0x3A
#define MINITEL_PRO3      0x3B
#define MINITEL_CONNEXION 0x68
#define MINITEL_DECONNEX  0x67
#define MINITEL_OPPOSITION 0x6F
#define MINITEL_SEP       0x13
#define MINITEL_SEP_CONNECTED    0x53
#define MINITEL_SEP_DISCONNECTED 0x54  // hypothèse

#define MINITEL_RING_ON_US     1500000
#define MINITEL_RING_PERIOD_US 5000000
#define MINITEL_RING_HALF_US   10000  // 50 Hz
#define MINITEL_CONNECT_US     1500000

// La ligne téléphonique (ou son équivalent réseau)
typedef struct {
    bool (*dial)(void* ctx);      // appel sortant (Minitel en terminal) ; false si impossible
    void (*answer)(void* ctx);    // décroche l'appel entrant
    void (*hangup)(void* ctx);    // raccroche
    bool (*incoming)(void* ctx);  // un appel entrant sonne
    bool (*carrier)(void* ctx);   // correspondant en ligne
    int (*recv)(void* ctx);       // octet reçu ou -1
    void (*send)(void* ctx, uint8_t data);
    void* ctx;
} minitel_line_t;

typedef enum {
    MINITEL_IDLE = 0,
    MINITEL_CONNECTING,
    MINITEL_ONLINE,
} minitel_state_t;

#define MINITEL_QUEUE 16

typedef struct {
    minitel_line_t line;
    minitel_state_t state;
    // Analyse des séquences ESC venant du Telestrat
    uint8_t seq[5];
    int seq_len;   // octets reçus de la séquence en cours
    int seq_need;  // octets attendus (0 = pas de séquence)
    bool opposition;
    // Réponses du Minitel vers le Telestrat
    uint8_t queue[MINITEL_QUEUE];
    int q_head, q_count;
    uint32_t connect_us;  // temps écoulé depuis CONNEXION
    // Sonnerie
    uint32_t ring_us;
    bool ring_level;
    // Statistiques (tests)
    uint32_t pro1_count;
    uint8_t last_pro1;
} minitel_port_t;

static inline void minitel_port_init(minitel_port_t* p, const minitel_line_t* line) {
    minitel_line_t l = *line;
    *p = (minitel_port_t){0};
    p->line = l;
}

static inline void _minitel_push(minitel_port_t* p, uint8_t b) {
    if (p->q_count >= MINITEL_QUEUE) return;
    p->queue[(p->q_head + p->q_count) % MINITEL_QUEUE] = b;
    p->q_count++;
}

static inline void _minitel_pro1(minitel_port_t* p, uint8_t code) {
    p->pro1_count++;
    p->last_pro1 = code;
    switch (code) {
        case MINITEL_CONNEXION:
            if (p->state != MINITEL_IDLE) break;
            if (p->line.incoming && p->line.incoming(p->line.ctx)) {
                if (p->line.answer) p->line.answer(p->line.ctx);
                p->state = MINITEL_CONNECTING;
            } else if (p->line.dial && p->line.dial(p->line.ctx)) {
                p->state = MINITEL_CONNECTING;
            }
            p->connect_us = 0;
            break;
        case MINITEL_DECONNEX:
            if (p->state != MINITEL_IDLE && p->line.hangup) p->line.hangup(p->line.ctx);
            p->state = MINITEL_IDLE;
            p->opposition = false;
            break;
        case MINITEL_OPPOSITION: p->opposition = true; break;
        default: break;  // autres commandes PRO1 : sans effet ici
    }
}

// Octet émis par l'ACIA du Telestrat
static inline void minitel_port_from_telestrat(minitel_port_t* p, uint8_t data) {
    data &= 0x7F;
    if (p->seq_need) {
        p->seq[p->seq_len++] = data;
        if (p->seq_len == 2 && p->seq_need == 2) {
            // ESC suivi d'autre chose qu'un PRO : séquence Videotex, transmise
            if (data == MINITEL_PRO1) {
                p->seq_need = 3;
            } else if (data == MINITEL_PRO2) {
                p->seq_need = 4;
            } else if (data == MINITEL_PRO3) {
                p->seq_need = 5;
            } else {
                if (p->state == MINITEL_ONLINE && p->line.send) {
                    p->line.send(p->line.ctx, MINITEL_ESC);
                    p->line.send(p->line.ctx, data);
                }
                p->seq_need = 0;
                return;
            }
        }
        if (p->seq_len >= p->seq_need) {
            if (p->seq[1] == MINITEL_PRO1) _minitel_pro1(p, p->seq[2]);
            p->seq_need = 0;
        }
        return;
    }
    if (data == MINITEL_ESC) {
        p->seq[0] = data;
        p->seq_len = 1;
        p->seq_need = 2;
        return;
    }
    if (p->state == MINITEL_ONLINE && p->line.send) p->line.send(p->line.ctx, data);
}

// Octet à présenter à l'ACIA du Telestrat, ou -1
static inline int minitel_port_to_telestrat(minitel_port_t* p) {
    if (p->q_count) {
        uint8_t b = p->queue[p->q_head];
        p->q_head = (p->q_head + 1) % MINITEL_QUEUE;
        p->q_count--;
        return b;
    }
    if (p->state == MINITEL_ONLINE && p->line.recv) return p->line.recv(p->line.ctx);
    return -1;
}

// Avance de `us` microsecondes ; retourne le niveau du détecteur de sonnerie
static inline bool minitel_port_tick(minitel_port_t* p, uint32_t us) {
    bool carrier = p->line.carrier && p->line.carrier(p->line.ctx);
    switch (p->state) {
        case MINITEL_CONNECTING:
            p->connect_us += us;
            if (carrier && p->connect_us >= MINITEL_CONNECT_US) {
                _minitel_push(p, MINITEL_SEP);
                _minitel_push(p, MINITEL_SEP_CONNECTED);
                p->state = MINITEL_ONLINE;
            }
            break;
        case MINITEL_ONLINE:
            if (!carrier) {
                _minitel_push(p, MINITEL_SEP);
                _minitel_push(p, MINITEL_SEP_DISCONNECTED);
                p->state = MINITEL_IDLE;
            }
            break;
        default: break;
    }
    // Sonnerie tant qu'un appel entrant attend et que la ligne est libre
    if (p->state == MINITEL_IDLE && p->line.incoming && p->line.incoming(p->line.ctx)) {
        p->ring_us = (p->ring_us + us) % MINITEL_RING_PERIOD_US;
        p->ring_level = p->ring_us < MINITEL_RING_ON_US && ((p->ring_us / MINITEL_RING_HALF_US) & 1);
    } else {
        p->ring_us = 0;
        p->ring_level = false;
    }
    return p->ring_level;
}
