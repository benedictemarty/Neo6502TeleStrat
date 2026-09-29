#pragma once

// modem_mux.h — un seul modem (PicoWiFiModemUSB) pour les deux prises de l'ACIA
//
// Prise Minitel (PA4 = 0) : le firmware pilote le modem (hayes_line.h :
// ATE0V1, ATS0=0, AT$SP, ATA, ATD…). Prise RS232 (PA4 = 1) : octets bruts dans
// les deux sens ; le logiciel du Telestrat parle Hayes lui-même, comme avec un
// modem RS232 réel. TELEMON laisse PA4 sur la dernière prise utilisée (observé
// au banc : Minitel après l'initialisation, RS232 dès un SOUT) ; le modem suit
// PA4 (modem_mux_select).
//
// Au retour sur la prise Minitel, hayes_line est réinitialisé (ses commandes
// d'initialisation repartent) ; une communication laissée ouverte par le
// logiciel RS232 les recevrait comme données : il doit raccrocher (ATH) avant.
// Un modem branché pendant que la RS232 le tient n'est pas initialisé : il
// l'est au retour sur la prise Minitel.
//
// Indépendant de la plate-forme (testé dans tests/test_telestrat.c).
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
#include <string.h>

#include "devices/hayes_line.h"

#define MODEM_MUX_RXQ 512

typedef struct {
    hayes_line_t* hayes;
    // Réglages de la ligne Minitel, pour réinitialiser hayes_line
    void (*write)(void* ctx, const uint8_t* data, uint32_t len);
    void* ctx;
    char dial[64];
    int listen;
    bool attached;  // modem présent
    bool rs232;     // le modem appartient à la prise RS232
    // Octets du modem vers la prise RS232
    uint8_t rxq[MODEM_MUX_RXQ];
    int rx_head, rx_count;
} modem_mux_t;

static inline void modem_mux_init(modem_mux_t* m, hayes_line_t* h) {
    memset(m, 0, sizeof(*m));
    m->hayes = h;
    memset(h, 0, sizeof(*h));
}

static inline void _modem_mux_hayes_init(modem_mux_t* m) {
    hayes_line_init(m->hayes, m->write, m->ctx, m->dial, m->listen);
}

// Modem branché (ou réglages relus) : initialisé tout de suite si la prise
// Minitel le tient
static inline void modem_mux_attach(modem_mux_t* m, void (*write)(void*, const uint8_t*, uint32_t), void* ctx,
                                    const char* dial, int listen) {
    m->write = write;
    m->ctx = ctx;
    snprintf(m->dial, sizeof(m->dial), "%s", dial ? dial : "");
    m->listen = listen;
    m->attached = true;
    if (!m->rs232) _modem_mux_hayes_init(m);
}

static inline void modem_mux_detach(modem_mux_t* m) {
    m->attached = false;
    memset(m->hayes, 0, sizeof(*m->hayes));
    m->rx_count = 0;
}

// Prise tenant le modem (PA4 du VIA 2)
static inline void modem_mux_select(modem_mux_t* m, bool rs232) {
    if (rs232 == m->rs232) return;
    m->rs232 = rs232;
    m->rx_count = 0;
    if (rs232) {
        // Ligne Minitel au repos pendant ce temps (plus de sonnerie ni de porteuse)
        void (*w)(void*, const uint8_t*, uint32_t) = m->hayes->write;
        void* c = m->hayes->ctx;
        memset(m->hayes, 0, sizeof(*m->hayes));
        m->hayes->write = w;
        m->hayes->ctx = c;
    } else if (m->attached) {
        _modem_mux_hayes_init(m);
    }
}

// Octet venant du modem
static inline void modem_mux_feed(modem_mux_t* m, uint8_t b) {
    if (!m->rs232) {
        hayes_line_feed(m->hayes, b);
        return;
    }
    if (m->rx_count >= MODEM_MUX_RXQ) return;
    m->rxq[(m->rx_head + m->rx_count) % MODEM_MUX_RXQ] = b;
    m->rx_count++;
}

static inline void modem_mux_tick(modem_mux_t* m, uint32_t us) {
    if (!m->rs232) hayes_line_tick(m->hayes, us);
}

// Prise RS232 : octet émis par le Telestrat
static inline void modem_mux_rs232_send(modem_mux_t* m, uint8_t data) {
    if (m->rs232 && m->attached && m->write) m->write(m->ctx, &data, 1);
}

// Prise RS232 : octet reçu du modem, -1 s'il n'y en a pas
static inline int modem_mux_rs232_recv(modem_mux_t* m) {
    if (!m->rs232 || !m->rx_count) return -1;
    uint8_t b = m->rxq[m->rx_head];
    m->rx_head = (m->rx_head + 1) % MODEM_MUX_RXQ;
    m->rx_count--;
    return b;
}
