#pragma once

// line_tcp.h — ligne « téléphonique » du banc PC sur TCP (sockets POSIX)
//
//   listen:PORT        un client TCP qui se connecte = appel entrant (sonnerie) ;
//                      décrocher (CONNEXION du Telestrat) = porteuse. Écoute sur
//                      127.0.0.1 seulement. PORT 0 : port choisi par le système,
//                      annoncé sur stderr (« line_tcp : écoute sur 127.0.0.1:N »)
//                      pour les tests (aucune fenêtre entre choix et ouverture)
//   connect:HOTE:PORT  CONNEXION du Telestrat = connexion TCP sortante (toutes
//                      les adresses de HOTE essayées, délai de 5 s par adresse) ;
//                      IPv6 entre crochets : connect:[::1]:PORT
//
// Un correspondant qui raccroche (fin de flux ou erreur de la connexion, ECONNRESET
// compris) lève la porteuse et libère la ligne : en mode listen, le client suivant
// est accepté.
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

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LINE_TCP_CONNECT_MS 5000  // délai de connexion sortante, par adresse
#define LINE_TCP_SEND_MS 1000     // attente maximale d'une place dans le tampon d'émission

typedef struct {
    int listen_fd;         // -1 en mode appel
    int fd;                // connexion en cours (-1 : aucune)
    bool answered;         // appel entrant décroché
    uint16_t listen_port;  // port d'écoute effectif (mode listen)
    unsigned lost;         // octets perdus à l'émission (tampon plein trop longtemps)
    char host[256];
    char port[16];
} line_tcp_t;

static void _line_tcp_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

// Port décimal 0..65535 sans reste (« abc », « 12x », « -1 », vide : refusés)
static bool _line_tcp_port(const char* s, int* port) {
    if (!*s) return false;
    long v = 0;
    for (const char* c = s; *c; c++) {
        if (*c < '0' || *c > '9') return false;
        v = v * 10 + (*c - '0');
        if (v > 65535) return false;
    }
    *port = (int)v;
    return true;
}

// Connexion perdue : porteuse retombée, ligne libre pour le client suivant
static void _line_tcp_drop(line_tcp_t* t) {
    if (t->fd >= 0) close(t->fd);
    t->fd = -1;
    t->answered = false;
}

static bool line_tcp_open(line_tcp_t* t, const char* spec) {
    memset(t, 0, sizeof(*t));
    t->listen_fd = -1;
    t->fd = -1;
    if (!strncmp(spec, "listen:", 7)) {
        int port;
        if (!_line_tcp_port(spec + 7, &port)) {
            fprintf(stderr, "line_tcp : port invalide « %s » (0 à 65535)\n", spec + 7);
            return false;
        }
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("line listen");
            return false;
        }
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t)port);
        socklen_t al = sizeof(a);
        if (bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0 || listen(fd, 1) < 0 ||
            getsockname(fd, (struct sockaddr*)&a, &al) < 0) {
            perror("line listen");
            close(fd);
            return false;
        }
        _line_tcp_nonblock(fd);
        t->listen_fd = fd;
        t->listen_port = ntohs(a.sin_port);
        fprintf(stderr, "line_tcp : écoute sur 127.0.0.1:%u\n", (unsigned)t->listen_port);
        return true;
    }
    if (!strncmp(spec, "connect:", 8)) {
        const char* hp = spec + 8;
        const char* colon = strrchr(hp, ':');
        int port;
        if (!colon || colon == hp || !_line_tcp_port(colon + 1, &port) || port == 0) {
            fprintf(stderr, "line_tcp : connect:HOTE:PORT attendu (PORT 1 à 65535) : %s\n", spec);
            return false;
        }
        const char* h = hp;
        size_t hl = (size_t)(colon - hp);
        if (hl >= 2 && h[0] == '[' && h[hl - 1] == ']') {  // [::1]:P
            h++;
            hl -= 2;
        }
        if (hl == 0 || hl >= sizeof(t->host)) return false;
        snprintf(t->host, sizeof(t->host), "%.*s", (int)hl, h);
        snprintf(t->port, sizeof(t->port), "%s", colon + 1);
        return true;
    }
    fprintf(stderr, "line_tcp : « listen:PORT » ou « connect:HOTE:PORT » attendu : %s\n", spec);
    return false;
}

static bool line_tcp_incoming(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->listen_fd < 0) return false;
    if (t->fd < 0) {
        int fd = accept(t->listen_fd, NULL, NULL);
        if (fd >= 0) {
            _line_tcp_nonblock(fd);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            t->fd = fd;
            t->answered = false;
        }
    }
    return t->fd >= 0 && !t->answered;
}

static void line_tcp_answer(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->fd >= 0) t->answered = true;
}

// Connexion avec délai : non bloquante, poll, puis SO_ERROR
static int _line_tcp_connect(const struct addrinfo* ai) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) return -1;
    _line_tcp_nonblock(fd);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
        int err = 0;
        socklen_t el = sizeof(err);
        struct pollfd pf = {.fd = fd, .events = POLLOUT};
        if (errno != EINPROGRESS || poll(&pf, 1, LINE_TCP_CONNECT_MS) != 1 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0 || err != 0) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static bool line_tcp_dial(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->listen_fd >= 0 || t->fd >= 0) return false;
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(t->host, t->port, &hints, &res) != 0) return false;
    int fd = -1;
    for (const struct addrinfo* ai = res; ai && fd < 0; ai = ai->ai_next) fd = _line_tcp_connect(ai);
    freeaddrinfo(res);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    t->fd = fd;
    t->answered = true;
    return true;
}

static void line_tcp_hangup(void* ctx) { _line_tcp_drop(ctx); }

static bool line_tcp_carrier(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->fd < 0 || !t->answered) return false;
    char c;
    ssize_t n = recv(t->fd, &c, 1, MSG_PEEK);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        _line_tcp_drop(t);  // raccroché par le correspondant, ou connexion rompue (ECONNRESET…)
        return false;
    }
    return true;
}

static int line_tcp_recv(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->fd < 0 || !t->answered) return -1;
    uint8_t c;
    ssize_t n = recv(t->fd, &c, 1, 0);
    if (n == 1) return c;
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) _line_tcp_drop(t);
    return -1;
}

// Émission : tampon plein (EAGAIN) = attente d'une place, 1 s au plus ; l'octet
// perdu est compté (lost) et signalé une fois
static void line_tcp_send(void* ctx, uint8_t data) {
    line_tcp_t* t = ctx;
    if (t->fd < 0) return;
    for (;;) {
        ssize_t n = send(t->fd, &data, 1, MSG_NOSIGNAL);
        if (n == 1) return;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pf = {.fd = t->fd, .events = POLLOUT};
            if (poll(&pf, 1, LINE_TCP_SEND_MS) == 1) continue;
            if (t->lost++ == 0) fprintf(stderr, "line_tcp : tampon d'émission plein, octet perdu\n");
            return;
        }
        _line_tcp_drop(t);  // EPIPE, ECONNRESET… : la ligne est tombée
        return;
    }
}

static minitel_line_t line_tcp_line(line_tcp_t* t) {
    return (minitel_line_t){
        .dial = line_tcp_dial,
        .answer = line_tcp_answer,
        .hangup = line_tcp_hangup,
        .incoming = line_tcp_incoming,
        .carrier = line_tcp_carrier,
        .recv = line_tcp_recv,
        .send = line_tcp_send,
        .ctx = t,
    };
}
