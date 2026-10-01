#pragma once

// neo_writer.h — écriture tamponnée d'un fichier neo_storage
//
// Une écriture neo_storage est durable à son retour : le pilote FatFs
// synchronise (f_sync) à chaque appel. Les sorties faites de petits morceaux
// (cassette enregistrée octet par octet, pages PNG et tracés SVG) passent par
// ce tampon, fourni par l'appelant : une écriture du pilote par tampon plein,
// au retour en arrière (seek) et à la fermeture.
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
#include <string.h>

#include "devices/neo_storage.h"

typedef struct {
    neo_file_t file;
    uint8_t* buf;
    uint32_t cap;   // taille du tampon
    uint32_t pos;   // position du début du tampon dans le fichier
    uint32_t n;     // octets en attente
    bool ok;        // toutes les écritures ont réussi
} neo_writer_t;

static inline bool neo_writer_is_open(const neo_writer_t* w) { return neo_file_is_open(&w->file); }

// Crée (ou vide) path ; false si impossible
static inline bool neo_writer_open(neo_writer_t* w, int vol, const char* path, uint8_t* buf, uint32_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->pos = w->n = 0;
    w->ok = neo_file_open(&w->file, vol, path, NEO_WRITE | NEO_CREATE);
    return w->ok;
}

static inline void neo_writer_flush(neo_writer_t* w) {
    if (!w->n) return;
    if (!neo_file_write(&w->file, w->pos, w->buf, w->n)) w->ok = false;
    w->pos += w->n;
    w->n = 0;
}

static inline void neo_writer_write(neo_writer_t* w, const void* data, uint32_t len) {
    if (!neo_writer_is_open(w)) return;
    if (w->n + len > w->cap) neo_writer_flush(w);
    if (len >= w->cap) {  // plus grand que le tampon : écrit tel quel
        if (!neo_file_write(&w->file, w->pos, (const uint8_t*)data, len)) w->ok = false;
        w->pos += len;
        return;
    }
    memcpy(w->buf + w->n, data, len);
    w->n += len;
}

// Position absolue des écritures suivantes
static inline void neo_writer_seek(neo_writer_t* w, uint32_t pos) {
    if (!neo_writer_is_open(w)) return;
    neo_writer_flush(w);
    w->pos = pos;
}

// Tampon écrit, fichier fermé ; false si une écriture a échoué
static inline bool neo_writer_close(neo_writer_t* w) {
    if (!neo_writer_is_open(w)) return false;
    neo_writer_flush(w);
    const bool ok = neo_file_close(&w->file) && w->ok;
    w->n = 0;
    return ok;
}
