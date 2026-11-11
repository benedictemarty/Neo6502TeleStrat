#pragma once

// modem_pc.h — « faux » PicoWiFiModemUSB du banc PC (option -H)
//
// Sur le firmware, le modem est un port série USB (CDC) : hayes_line.h et
// modem_mux.h lui parlent par « écrire des octets » et « lire un octet sans
// attente » (modem_write / modem_flush / modem_poll de telestrat.c). Sur PC il
// n'existe pas : ce fichier offre les mêmes opérations sur un socket TCP local,
// auquel se relie un correspondant (tests/modem_peer.py) qui joue le modem.
//
//   listen:PORT   le banc écoute sur 127.0.0.1 seulement ; PORT 0 : port choisi
//                 par le système, annoncé sur stderr (« line_tcp : écoute sur
//                 127.0.0.1:N », comme -L et -S). Un correspondant connecté =
//                 modem branché (modem_mux_attach : ATE0V1, ATS0=0…) ; il se
//                 déconnecte = modem débranché (modem_mux_detach) ; le suivant
//                 est accepté. Pas de mode connect: (inutile aux tests).
//
// La logique est celle du firmware : hayes_line.h, modem_mux.h et
// minitel_port.h, inchangés. Seul le « collage » de modem_pc_tick() est
// recopié de la boucle de platforms/rp2040/src/telestrat.c (bascule de prise
// d'après PA4, modem_poll, modem_mux_tick, minitel_port_tick) ; à garder
// identique si l'un change. Différences voulues : l'écriture vers le modem est
// immédiate (le firmware passe par une file vidée par modem_flush entre deux
// tranches de 1 ms), et le « CDC » est un flux TCP sans cadence propre.
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

#include "devices/modem_mux.h"
#include "devices/minitel_port.h"
#include "line_tcp.h"

#define MODEM_PC_WAIT_MS 30000  // attente du correspondant au démarrage du banc

typedef struct {
    line_tcp_t tcp;    // écoute ; tcp.fd = le « câble USB » vers le correspondant
    bool present;      // modem branché (équivalent de modem_present du firmware)
    char dial[64];     // cible composée par ATD
    hayes_line_t hayes;
    modem_mux_t mux;
    // Octets perdus, cumulés (les files sont remises à zéro à chaque réinitialisation)
    uint32_t lost_hayes, lost_mux, lost_minitel, lost_tx;
    uint32_t seen_hayes, seen_mux, seen_minitel;
} modem_pc_t;

// Écoute (listen:PORT seulement) ; faux si la spécification est invalide
static bool modem_pc_open(modem_pc_t* m, const char* spec, const char* dial) {
    memset(m, 0, sizeof(*m));
    if (strncmp(spec, "listen:", 7) != 0) {
        fprintf(stderr, "modem_pc : « listen:PORT » attendu : %s\n", spec);
        return false;
    }
    if (!line_tcp_open(&m->tcp, spec)) return false;
    snprintf(m->dial, sizeof(m->dial), "%s", dial ? dial : "");
    modem_mux_init(&m->mux, &m->hayes);
    return true;
}

// Octets vers le modem (modem_write du firmware) : un seul envoi pour tout
// l'appel, comme un paquet CDC (« +++ » arrive d'un bloc). Tampon plein : attente
// d'une place (1 s au plus), puis octets perdus et comptés
static void modem_pc_write(void* ctx, const uint8_t* data, uint32_t len) {
    modem_pc_t* m = (modem_pc_t*)ctx;
    uint32_t done = 0;
    while (m->tcp.fd >= 0 && done < len) {
        ssize_t n = send(m->tcp.fd, data + done, len - done, MSG_NOSIGNAL);
        if (n > 0) {
            done += (uint32_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pf = {.fd = m->tcp.fd, .events = POLLOUT};
            if (poll(&pf, 1, LINE_TCP_SEND_MS) == 1) continue;
            break;
        } else {
            close(m->tcp.fd);  // modem débranché : modem_pc_watch le constatera
            m->tcp.fd = -1;
            break;
        }
    }
    if (done < len && m->tcp.fd >= 0) m->lost_tx += len - done;
}

// Octet du modem sans attente (neo_cdc_read_byte), -1 s'il n'y en a pas
static int modem_pc_read_byte(modem_pc_t* m) {
    m->tcp.answered = true;  // line_tcp_recv ne lit qu'une connexion « décrochée »
    return line_tcp_recv(&m->tcp);
}

// Branchement et débranchement (modem_watch du firmware), une fois par trame
static void modem_pc_watch(modem_pc_t* m) {
    if (m->tcp.fd < 0) line_tcp_incoming(&m->tcp);  // accepte le correspondant suivant
    const bool ready = m->tcp.fd >= 0;
    if (ready == m->present) return;
    m->present = ready;
    if (ready) {
        m->tcp.answered = true;
        modem_mux_attach(&m->mux, modem_pc_write, m, m->dial, 0);
    } else {
        modem_mux_detach(&m->mux);
    }
}

// Attend le correspondant (au plus `ms` ms) et branche le modem : le banc ne
// démarre qu'une fois le modem là, sans fenêtre dépendant du temps réel. Faux au dépassement
static bool modem_pc_wait(modem_pc_t* m, int ms) {
    struct pollfd pf = {.fd = m->tcp.listen_fd, .events = POLLIN};
    if (poll(&pf, 1, ms) != 1) return false;
    modem_pc_watch(m);
    return m->present;
}

// Octets reçus du modem (modem_poll du firmware) : pas plus que les files
// n'en gardent (P-02), le reste attend dans le socket
static void modem_pc_poll(modem_pc_t* m) {
    if (!m->present) return;
    while (modem_mux_rx_room(&m->mux) > 0) {
        const int c = modem_pc_read_byte(m);
        if (c < 0) break;
        modem_mux_feed(&m->mux, (uint8_t)c);
    }
}

static void modem_pc_count_(uint32_t* total, uint32_t* seen, uint32_t now) {
    if (now > *seen) *total += now - *seen;
    *seen = now;
}

// Une tranche de 1 ms (boucle d'émulation du firmware) ; `to_rs232` : PA4 du
// VIA 2 (telestrat_serial_is_rs232). Retourne le niveau du détecteur de
// sonnerie pour telestrat_set_ring()
static bool modem_pc_tick(modem_pc_t* m, minitel_port_t* minitel, bool to_rs232) {
    // Bascule vers la RS232 : SEP et séquences Minitel périmés effacés (P-03)
    if (to_rs232 && !m->mux.rs232) minitel_port_reset(minitel);
    modem_mux_select(&m->mux, to_rs232);
    modem_pc_poll(m);
    modem_mux_tick(&m->mux, 1000);
    const bool ring = minitel_port_tick(minitel, 1000);
    modem_pc_count_(&m->lost_hayes, &m->seen_hayes, m->hayes.rx.lost);
    modem_pc_count_(&m->lost_mux, &m->seen_mux, m->mux.rx.lost);
    modem_pc_count_(&m->lost_minitel, &m->seen_minitel, minitel->q.lost);
    return ring;
}

static uint32_t modem_pc_lost(const modem_pc_t* m) {
    return m->lost_hayes + m->lost_mux + m->lost_minitel + m->lost_tx;
}
