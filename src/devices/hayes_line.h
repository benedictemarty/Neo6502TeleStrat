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
//   « NO CARRIER » (aussi en ligne) : porteuse perdue
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
    // Détection de « NO CARRIER » dans le flux en ligne
    int nc_match;
    bool skip_lf;  // le \n qui termine « CONNECT » n'est pas une donnée
    bool ringing;
    uint32_t ring_age_us;
    uint32_t timer_us;
    uint8_t rxq[HAYES_RXQ];
    int rx_head, rx_count;
} hayes_line_t;

static inline void _hayes_puts(hayes_line_t* h, const char* s) {
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
    _hayes_puts(h, "ATE0V1\r");
    _hayes_puts(h, "ATS0=0\r");
    if (listen_port > 0) {
        char cmd[24];
        snprintf(cmd, sizeof(cmd), "AT$SP=%d\r", listen_port);
        _hayes_puts(h, cmd);
    }
}

static inline void _hayes_rx_push(hayes_line_t* h, uint8_t b) {
    if (h->rx_count >= HAYES_RXQ) return;
    h->rxq[(h->rx_head + h->rx_count) % HAYES_RXQ] = b;
    h->rx_count++;
}

static inline bool _hayes_starts(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

static inline void _hayes_result(hayes_line_t* h, const char* r) {
    if (_hayes_starts(r, "RING")) {
        if (h->state == HAYES_COMMAND) {
            h->ringing = true;
            h->ring_age_us = 0;
        }
    } else if (_hayes_starts(r, "CONNECT")) {
        if (h->state == HAYES_DIALING || h->state == HAYES_ANSWERING) {
            h->state = HAYES_ONLINE;
            h->nc_match = 0;
            h->skip_lf = true;
        }
    } else if (_hayes_starts(r, "NO CARRIER") || _hayes_starts(r, "BUSY") || _hayes_starts(r, "NO ANSWER") ||
               _hayes_starts(r, "NO DIALTONE") || _hayes_starts(r, "ERROR")) {
        if (h->state == HAYES_DIALING || h->state == HAYES_ANSWERING) h->state = HAYES_COMMAND;
    }
}

// Octet venant du modem
static inline void hayes_line_feed(hayes_line_t* h, uint8_t b) {
    static const char nc[] = "NO CARRIER";
    if (h->state == HAYES_ONLINE) {
        if (h->skip_lf) {
            h->skip_lf = false;
            if (b == '\n') return;
        }
        _hayes_rx_push(h, b);
        // « NO CARRIER » en ligne : le correspondant a raccroché
        if (b == (uint8_t)nc[h->nc_match]) {
            if (++h->nc_match == (int)sizeof(nc) - 1) {
                h->state = HAYES_COMMAND;
                h->nc_match = 0;
            }
        } else {
            h->nc_match = (b == (uint8_t)nc[0]) ? 1 : 0;
        }
        return;
    }
    if (b == '\r' || b == '\n') {
        if (h->line_len > 0) {
            h->line[h->line_len] = 0;
            _hayes_result(h, h->line);
            h->line_len = 0;
        }
        return;
    }
    if (h->line_len < (int)sizeof(h->line) - 1) h->line[h->line_len++] = (char)b;
}

static inline void hayes_line_tick(hayes_line_t* h, uint32_t us) {
    if (h->ringing) {
        h->ring_age_us += us;
        if (h->ring_age_us > HAYES_RING_TIMEOUT_US) h->ringing = false;
    }
    switch (h->state) {
        case HAYES_ESCAPE_GUARD1:
            h->timer_us += us;
            if (h->timer_us >= HAYES_GUARD_US) {
                _hayes_puts(h, "+++");
                h->state = HAYES_ESCAPE_GUARD2;
                h->timer_us = 0;
            }
            break;
        case HAYES_ESCAPE_GUARD2:
            h->timer_us += us;
            if (h->timer_us >= HAYES_GUARD_US) {
                _hayes_puts(h, "ATH\r");
                h->state = HAYES_COMMAND;
                h->line_len = 0;
            }
            break;
        default: break;
    }
}

/*-- Interface minitel_line_t ------------------------------------------------*/

static bool hayes_line_dial(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    if (h->state != HAYES_COMMAND || !h->dial_target[0]) return false;
    _hayes_puts(h, "ATD");
    _hayes_puts(h, h->dial_target);
    _hayes_puts(h, "\r");
    h->state = HAYES_DIALING;
    return true;
}

static void hayes_line_answer(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    if (h->state != HAYES_COMMAND) return;
    _hayes_puts(h, "ATA\r");
    h->ringing = false;
    h->state = HAYES_ANSWERING;
}

static void hayes_line_hangup(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    h->ringing = false;
    if (h->state == HAYES_ONLINE) {
        h->state = HAYES_ESCAPE_GUARD1;
        h->timer_us = 0;
    } else if (h->state == HAYES_DIALING || h->state == HAYES_ANSWERING) {
        _hayes_puts(h, "\r");  // un caractère interrompt la numérotation
        h->state = HAYES_COMMAND;
    }
}

static bool hayes_line_incoming(void* ctx) { return ((hayes_line_t*)ctx)->ringing; }

static bool hayes_line_carrier(void* ctx) { return ((hayes_line_t*)ctx)->state == HAYES_ONLINE; }

static int hayes_line_recv(void* ctx) {
    hayes_line_t* h = (hayes_line_t*)ctx;
    if (!h->rx_count) return -1;
    uint8_t b = h->rxq[h->rx_head];
    h->rx_head = (h->rx_head + 1) % HAYES_RXQ;
    h->rx_count--;
    return b;
}

static void hayes_line_send(void* ctx, uint8_t data) {
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
        .recv = hayes_line_recv,
        .send = hayes_line_send,
        .ctx = h,
    };
}
