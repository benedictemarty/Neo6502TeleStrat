#pragma once

// byte_fifo.h — file d'octets : remplie par l'émulation (imprimante), vidée par
// la boucle principale (écriture sur la clé), sans écriture de fichier au
// milieu d'un cycle du 65C02. Pleine : les octets suivants sont comptés perdus.
//
// Deux niveaux :
//  - byte_ring_t : curseurs d'une file circulaire dont le TAMPON appartient à
//    l'appelant (tableau d'octets et capacité, puissance de 2, passés à chaque
//    appel). Sert à hayes_line, modem_mux et minitel_port, sans RAM en plus que
//    des curseurs maison ;
//  - byte_fifo_t : tampon BYTE_FIFO_SIZE + curseurs, construit sur byte_ring_t.
//
// Indépendant de la plate-forme (testé dans tests/test_telestrat.c).
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <stdbool.h>
#include <stdint.h>

// ---- curseurs seuls (tampon fourni par l'appelant) -----------------------------

typedef struct {
    uint32_t head, tail;  // tail - head = octets en attente
    uint32_t lost;        // octets jetés, file pleine
} byte_ring_t;

// Remet la file à vide ; les pertes déjà comptées sont conservées
static inline void byte_ring_clear(byte_ring_t* r) { r->head = r->tail = 0; }

static inline uint32_t byte_ring_count(const byte_ring_t* r) { return r->tail - r->head; }

static inline uint32_t byte_ring_room(const byte_ring_t* r, uint32_t cap) { return cap - (r->tail - r->head); }

// cap : puissance de 2. Pleine : l'octet est jeté et compté dans `lost`.
static inline bool byte_ring_push(byte_ring_t* r, uint8_t* buf, uint32_t cap, uint8_t b) {
    if (byte_ring_count(r) >= cap) {
        r->lost++;
        return false;
    }
    buf[r->tail++ & (cap - 1)] = b;
    return true;
}

// Octet le plus ancien, ou -1 si la file est vide
static inline int byte_ring_pop(byte_ring_t* r, const uint8_t* buf, uint32_t cap) {
    if (!byte_ring_count(r)) return -1;
    return buf[r->head++ & (cap - 1)];
}

// Bloc contigu à lire (au plus jusqu'à la fin du tampon) ; *len = sa taille
static inline const uint8_t* byte_ring_peek(const byte_ring_t* r, const uint8_t* buf, uint32_t cap, uint32_t* len) {
    const uint32_t n = byte_ring_count(r), at = r->head & (cap - 1);
    *len = n < cap - at ? n : cap - at;
    return &buf[at];
}

static inline void byte_ring_drop(byte_ring_t* r, uint32_t n) { r->head += n; }

// ---- file avec son tampon BYTE_FIFO_SIZE ---------------------------------------

#ifndef BYTE_FIFO_SIZE
#define BYTE_FIFO_SIZE 1024  // puissance de 2 (64 avec la RAM 64 Ko : place ; l'ACK retenu évite toute perte)
#endif

typedef struct {
    uint8_t data[BYTE_FIFO_SIZE];
    byte_ring_t r;  // r.lost : octets perdus
} byte_fifo_t;

static inline void byte_fifo_init(byte_fifo_t* f) { f->r = (byte_ring_t){0}; }

static inline uint32_t byte_fifo_count(const byte_fifo_t* f) { return byte_ring_count(&f->r); }

static inline bool byte_fifo_push(byte_fifo_t* f, uint8_t b) { return byte_ring_push(&f->r, f->data, BYTE_FIFO_SIZE, b); }

// Bloc contigu à écrire (au plus jusqu'à la fin du tampon) ; *len = sa taille
static inline const uint8_t* byte_fifo_peek(const byte_fifo_t* f, uint32_t* len) {
    return byte_ring_peek(&f->r, f->data, BYTE_FIFO_SIZE, len);
}

static inline void byte_fifo_drop(byte_fifo_t* f, uint32_t n) { byte_ring_drop(&f->r, n); }
