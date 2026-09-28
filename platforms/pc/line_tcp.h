#pragma once

// line_tcp.h — ligne « téléphonique » du banc PC sur TCP (sockets POSIX)
//
//   listen:PORT        un client TCP qui se connecte = appel entrant (sonnerie) ;
//                      décrocher (CONNEXION du Telestrat) = porteuse
//   connect:HOTE:PORT  CONNEXION du Telestrat = connexion TCP sortante
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    int listen_fd;  // -1 en mode appel
    int fd;         // connexion en cours (-1 : aucune)
    bool answered;  // appel entrant décroché
    char host[256];
    char port[16];
} line_tcp_t;

static void _line_tcp_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

static bool line_tcp_open(line_tcp_t* t, const char* spec) {
    memset(t, 0, sizeof(*t));
    t->listen_fd = -1;
    t->fd = -1;
    if (!strncmp(spec, "listen:", 7)) {
        int port = atoi(spec + 7);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in a = {0};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t)port);
        if (fd < 0 || bind(fd, (struct sockaddr*)&a, sizeof(a)) < 0 || listen(fd, 1) < 0) {
            perror("line listen");
            return false;
        }
        _line_tcp_nonblock(fd);
        t->listen_fd = fd;
        return true;
    }
    if (!strncmp(spec, "connect:", 8)) {
        const char* hp = spec + 8;
        const char* colon = strrchr(hp, ':');
        if (!colon) return false;
        snprintf(t->host, sizeof(t->host), "%.*s", (int)(colon - hp), hp);
        snprintf(t->port, sizeof(t->port), "%s", colon + 1);
        return true;
    }
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

static bool line_tcp_dial(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->listen_fd >= 0 || t->fd >= 0) return false;
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(t->host, t->port, &hints, &res) != 0) return false;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        if (fd >= 0) close(fd);
        freeaddrinfo(res);
        return false;
    }
    freeaddrinfo(res);
    _line_tcp_nonblock(fd);
    t->fd = fd;
    t->answered = true;
    return true;
}

static void line_tcp_hangup(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->fd >= 0) close(t->fd);
    t->fd = -1;
    t->answered = false;
}

static bool line_tcp_carrier(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->fd < 0 || !t->answered) return false;
    char c;
    ssize_t n = recv(t->fd, &c, 1, MSG_PEEK);
    if (n == 0) {  // le correspondant a raccroché
        close(t->fd);
        t->fd = -1;
        t->answered = false;
        return false;
    }
    return true;
}

static int line_tcp_recv(void* ctx) {
    line_tcp_t* t = ctx;
    if (t->fd < 0 || !t->answered) return -1;
    uint8_t c;
    ssize_t n = recv(t->fd, &c, 1, 0);
    return n == 1 ? c : -1;
}

static void line_tcp_send(void* ctx, uint8_t data) {
    line_tcp_t* t = ctx;
    if (t->fd >= 0) (void)!send(t->fd, &data, 1, MSG_NOSIGNAL);
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
