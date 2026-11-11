#pragma once

// hayes_line.h
//
// Ligne téléphonique (minitel_line_t) sur un modem Hayes : PicoWiFiModemUSB
// en USB CDC sur le Neo6502, ou tout modem AT sur une liaison série.
// Indépendant de la plate-forme : `write` envoie des octets au modem,
// hayes_line_feed() lui donne ceux qui en viennent, hayes_line_tick() fait
// avancer les temporisations.
//
// Dialogue (commandes Hayes standard ; AT$SP est propre au PicoWiFiModemUSB,
// voir son README) :
//   init       ATE0V1, ATS0=0 (pas de réponse automatique : c'est TELEMON qui
//              décroche), AT$SP=port si un port d'écoute est demandé
//   « RING »   appel entrant : sonne tant que les RING se répètent (8 s)
//   answer     ATA  -> « CONNECT... » : en ligne
//   dial       ATDhôte:port -> « CONNECT... » : en ligne
//   hangup     garde 1 s, « +++ », garde 1 s, ATH
//   « NO CARRIER » (aussi en ligne) : porteuse perdue ; en ligne, seulement
//              précédé de « \r\n » (ou du « CONNECT » lui-même), et ces octets
//              ne sont pas transmis comme données
//   « BUSY », « NO ANSWER », « NO DIALTONE », « ERROR » : échec de l'appel,
//              visible par hayes_line_pending() (le Minitel n'attend pas 40 s)
//
// Réception : file de HAYES_RXQ octets. Pleine, les octets suivants sont jetés
// et comptés dans `rx.lost` ; l'appelant lit hayes_line_rx_room() avant de
// vider le CDC du modem, pour ne rien perdre. Cadence réelle du
// PicoWiFiModem : non établie (à vérifier).
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
#include <stdio.h>
#include <string.h>

#include "devices/minitel_port.h"

#define HAYES_RING_TIMEOUT_US 8000000
#define HAYES_GUARD_US        1100000
#define HAYES_RXQ             512
#define HAYES_CALL_TIMEOUT_US 60000000  // numérotation ou décrochage sans réponse : abandon (choix de ce code)
#define HAYES_NC_HOLD_US      250000    // octets retenus pendant la détection de « NO CARRIER » (choix de ce code)

typedef enum {
    HAYES_COMMAND = 0,  // modem en mode commande, ligne libre
    HAYES_DIALING,
    HAYES_ANSWERING,
    HAYES_ONLINE,
    HAYES_ESCAPE_GUARD1,  // raccrochage : silence avant +++
    HAYES_ESCAPE_GUARD2,  // silence après +++, puis ATH
} hayes_state_t;

typedef struct {
    void (*write)(void* ctx, const uint8_t* data, uint32_t len);
    void* ctx;
    hayes_state_t state;
    char dial_target[64];
    // Réponses du modem (mode commande)
    char line[48];
    int line_len;
    // Détection de « \r\nNO CARRIER » dans le flux en ligne : nc_match octets
    // du motif reconnus, dont les nc_free premiers (le « \r\n » de la ligne
    // CONNECT) ne sont pas des données à restituer si le motif échoue
    int nc_match, nc_free;
    uint32_t nc_age_us;  // temps depuis le dernier octet retenu
    bool skip_lf;  // le \n qui termine « CONNECT » n'est pas une donnée
    bool ringing;
    uint32_t ring_age_us;
    uint32_t timer_us;
    uint8_t rxq[HAYES_RXQ];
    byte_ring_t rx;  // rx.lost : octets du modem jetés, file pleine
} hayes_line_t;

static inline void hayes_puts_(hayes_line_t* h, const char* s) {
    if (h->write) h->write(h->ctx, (const uint8_t*)s, (uint32_t)strlen(s));
}

// dial_target : « hôte:port » composé par ATD (vide : pas d'appel sortant) ;
// listen_port : port d'écoute du PicoWiFiModemUSB (0 : ne pas le changer)
static inline void hayes_line_init(hayes_line_t* h, void (*write)(void*, const uint8_t*, uint32_t), void* ctx,
                                   const char* dial_target, int listen_port) {
    memset(h, 0, sizeof(*h));
    h->write = write;
    h->ctx = ctx;
    snprintf(h->dial_target, sizeof(h->dial_target), "%s", dial_target ? dial_target : "");
    hayes_puts_(h, "ATE0V1\r");
    hayes_puts_(h, "ATS0=0\r");
    if (listen_port > 0) {
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "AT$SP=%d\r", listen_port);
        hayes_puts_(h, cmd);
    }
}

static inline void hayes_rx_push_(hayes_line_t* h, uint8_t b) {
    byte_ring_push(&h->rx, h->rxq, HAYES_RXQ, b);
}

// Place libre dans la file de réception : l'appelant ne doit pas lire plus
// d'octets du modem (contrôle de flux côté hôte)
static inline int hayes_line_rx_room(const hayes_line_t* h) { return (int)byte_ring_room(&h->rx, HAYES_RXQ); }

static inline bool hayes_starts_(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

static inline void hayes_result_(hayes_line_t* h, const char* r) {
    if (hayes_starts_(r, "RING")) {
        if (h->state == HAYES_COMMAND) {
            h->ringing = true;
            h->ring_age_us = 0;
        }
    } else if (hayes_starts_(r, "CONNECT")) {
        if (h->state == HAYES_DIALING || h->state == HAYES_ANSWERING) {
            h->state = HAYES_ONLINE;
            h->nc_match = h->nc_free = 2;  // la fin de ligne de CONNECT tient lieu de « \r\n »
            h->nc_age_us = 0;
            h->skip_lf = true;
        }
    } else if (hayes_starts_(r, "NO CARRIER") || hayes_starts_(r, "BUSY") || hayes_starts_(r, "NO ANSWER") ||
               hayes_starts_(r, "NO DIALTONE") || hayes_starts_(r, "ERROR")) {
        if (h->state == HAYES_DIALING || h->state == HAYES_ANSWERING) h->state = HAYES_COMMAND;
    }
}

// « NO CARRIER » en ligne : motif complet, précédé de la fin de ligne
static const char hayes_nc_[] = "\r\nNO CARRIER";

// Restitue en données les octets retenus (motif non abouti ou trop lent)
static inline void hayes_nc_flush_(hayes_line_t* h) {
    for (int i = h->nc_free; i < h->nc_match; i++) hayes_rx_push_(h, (uint8_t)hayes_nc_[i]);
    h->nc_match = h->nc_free = 0;
}

// Octet venant du modem
static inline void hayes_line_feed(hayes_line_t* h, uint8_t b) {
    if (h->state == HAYES_ONLINE) {
        if (h->skip_lf) {
            h->skip_lf = false;
            if (b == '\n') return;
        }
        if (h->nc_match && b != (uint8_t)hayes_nc_[h->nc_match]) hayes_nc_flush_(h);
        if (b == (uint8_t)hayes_nc_[h->nc_match]) {
            h->nc_age_us = 0;
            if (++h->nc_match == (int)sizeof(hayes_nc_) - 1) {
                // Le correspondant a raccroché : les octets du motif ne sont pas des données
                h->state = HAYES_COMMAND;
                h->nc_match = h->nc_free = 0;
            }
        } else {
            hayes_rx_push_(h, b);
        }
        return;
    }
    if (b == '\r' || b == '\n') {
        if (h->line_len > 0) {
            h->line[h->line_len] = 0;
            hayes_result_(h, h->line);
            h->line_len = 0;
        }
        return;
    }
    if (h->line_len < (int)sizeof(h->line) - 1) h->line[h->line_len++] = (char)b;
}

static inline void hayes_line_tick(hayes_line_t* h, uint32_t us) {
    if (h->state == HAYES_ONLINE && h->nc_match > h->nc_free) {
        // Début de motif resté sans suite : c'étaient des données
        h->nc_age_us += us;
        if (h->nc_age_us >= HAYES_NC_HOLD_US) hayes_nc_flush_(h);
    }
    if (h->ringing) {
        h->ring_age_us += us;
        if (h->ring_age_us > HAYES_RING_TIMEOUT_US) h->ringing = false;
    }
    switch (h->state) {
        case HAYES_DIALING:
        case HAYES_ANSWERING:
            h->timer_us += us;
            if (h->timer_us >= HAYES_CALL_TIMEOUT_US) {
                hayes_puts_(h, "\r");  // un caractère interrompt la numérotation
                h->state = HAYES_COMMAND;
                h->line_len = 0;
            }
            break;
        case HAYES_ESCAPE_GUARD1:
            h->timer_us += us;
            if (h->timer_us >= HAYES_GUARD_US) {
                hayes_puts_(h, "+++");
                h->state = HAYES_ESCAPE_GUARD2;
                h->timer_us = 0;
            }
            break;
        case HAYES_ESCAPE_GUARD2:
            h->timer_us += us;
            if (h->timer_us >= HAYES_GUARD_US) {
                hayes_puts_(h, "ATH\r");
                h->state = HAYES_COMMAND;
                h->line_len = 0;
            }
            break;
        default: break;
    }
}

/*-- Interface minitel_line_t ------------------------------------------------*/

static inline bool hayes_line_dial(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    if (h->state != HAYES_COMMAND || !h->dial_target[0]) return false;
    hayes_puts_(h, "ATD");
    hayes_puts_(h, h->dial_target);
    hayes_puts_(h, "\r");
    h->state = HAYES_DIALING;
    h->timer_us = 0;
    return true;
}

static inline void hayes_line_answer(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    if (h->state != HAYES_COMMAND) return;
    hayes_puts_(h, "ATA\r");
    h->ringing = false;
    h->state = HAYES_ANSWERING;
    h->timer_us = 0;
}

static inline void hayes_line_hangup(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    h->ringing = false;
    if (h->state == HAYES_ONLINE) {
        h->state = HAYES_ESCAPE_GUARD1;
        h->timer_us = 0;
    } else if (h->state == HAYES_DIALING || h->state == HAYES_ANSWERING) {
        hayes_puts_(h, "\r");  // un caractère interrompt la numérotation
        h->state = HAYES_COMMAND;
    }
}

static inline bool hayes_line_incoming(void* ctx) { return ((hayes_line_t*)ctx)->ringing; }

static inline bool hayes_line_carrier(void* ctx) { return ((hayes_line_t*)ctx)->state == HAYES_ONLINE; }

// Appel en cours d'établissement (ATD ou ATA envoyé, ni CONNECT ni échec reçu)
static inline bool hayes_line_pending(void* ctx) {
    const hayes_state_t st = ((hayes_line_t*)ctx)->state;
    return st == HAYES_DIALING || st == HAYES_ANSWERING;
}

static inline int hayes_line_recv(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    return byte_ring_pop(&h->rx, h->rxq, HAYES_RXQ);
}

static inline void hayes_line_send(void* ctx, uint8_t data) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    if (h->state == HAYES_ONLINE && h->write) h->write(h->ctx, &data, 1);
}

static inline minitel_line_t hayes_line_line(hayes_line_t* h) {
    return (minitel_line_t){
        .dial = hayes_line_dial,
        .answer = hayes_line_answer,
        .hangup = hayes_line_hangup,
        .incoming = hayes_line_incoming,
        .carrier = hayes_line_carrier,
        .pending = hayes_line_pending,
        .recv = hayes_line_recv,
        .send = hayes_line_send,
        .ctx = h,
    };
}
