#pragma once

// byte_fifo.h — file d'octets : remplie par l'émulation (imprimante), vidée par
// la boucle principale (écriture sur la clé), sans écriture de fichier au
// milieu d'un cycle du 65C02. Pleine : les octets suivants sont comptés perdus.
//
// Indépendant de la plate-forme (testé dans tests/test_telestrat.c).
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <stdbool.h>
#include <stdint.h>

#ifndef BYTE_FIFO_SIZE
#define BYTE_FIFO_SIZE 1024  // puissance de 2 (64 avec la RAM 64 Ko : place ; l'ACK retenu évite toute perte)
#endif

typedef struct {
    uint8_t data[BYTE_FIFO_SIZE];
    uint32_t head, tail;  // tail - head = octets en attente
    uint32_t lost;
} byte_fifo_t;

static inline void byte_fifo_init(byte_fifo_t* f) {
    f->head = f->tail = 0;
    f->lost = 0;
}

static inline uint32_t byte_fifo_count(const byte_fifo_t* f) { return f->tail - f->head; }

static inline bool byte_fifo_push(byte_fifo_t* f, uint8_t b) {
    if (byte_fifo_count(f) >= BYTE_FIFO_SIZE) {
        f->lost++;
        return false;
    }
    f->data[f->tail++ & (BYTE_FIFO_SIZE - 1)] = b;
    return true;
}

// Bloc contigu à écrire (au plus jusqu'à la fin du tampon) ; *len = sa taille
static inline const uint8_t* byte_fifo_peek(const byte_fifo_t* f, uint32_t* len) {
    const uint32_t n = byte_fifo_count(f), at = f->head & (BYTE_FIFO_SIZE - 1);
    *len = n < BYTE_FIFO_SIZE - at ? n : BYTE_FIFO_SIZE - at;
    return &f->data[at];
}

static inline void byte_fifo_drop(byte_fifo_t* f, uint32_t n) { f->head += n; }
