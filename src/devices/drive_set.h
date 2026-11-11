#pragma once

// drive_set.h — images .dsk de la clé USB réparties dans les lecteurs A à D
//
// Affectation de départ des lecteurs (le menu, F1, les change ensuite). Une
// image n'est jamais dans deux lecteurs à la fois : chaque lecteur garde son
// fichier ouvert en écriture. TELESTRA.CFG « a=NOM.DSK » … « d=NOM.DSK » (nom
// sans distinction de casse) ; sans « a= » (absent ou vide), la première image
// libre va dans A ; avec un « a= » qui ne correspond à aucune image, A reste
// vide (signalé dans `unmatched`) ; B à D restent vides sans réglage.
//
// Noms : DRIVE_SET_NAME_LEN octets au plus, zéro final compris (48 comme
// OSD_NAME_LEN, FatFs avec noms longs). Un nom trop long est refusé
// (drive_set_add rend false, compté dans `rejected`), jamais tronqué : un nom
// tronqué désignerait une autre image. Au-delà de DRIVE_SET_MAX images, le
// refus est compté de la même façon.
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

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DRIVE_SET_MAX    32
#define DRIVE_SET_DRIVES 4
#ifndef DRIVE_SET_NAME_LEN
#define DRIVE_SET_NAME_LEN 48  // taille d'un nom, zéro final compris (OSD_NAME_LEN)
#endif

typedef struct {
    char names[DRIVE_SET_MAX][DRIVE_SET_NAME_LEN];
    int count;
    int slot[DRIVE_SET_DRIVES];     // image de chaque lecteur, -1 : vide
    int rejected;                   // images refusées par drive_set_add (nom trop long, liste pleine)
    uint8_t unmatched;              // bit d : le lecteur d a un réglage sans image correspondante
} drive_set_t;

static inline void drive_set_init(drive_set_t* s) {
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < DRIVE_SET_DRIVES; i++) s->slot[i] = -1;
}

static inline bool drive_set_add(drive_set_t* s, const char* name) {
    if (s->count >= DRIVE_SET_MAX || strlen(name) >= DRIVE_SET_NAME_LEN) {
        s->rejected++;
        return false;
    }
    memcpy(s->names[s->count], name, strlen(name) + 1);
    s->count++;
    return true;
}

static inline bool drive_set_same_(const char* a, const char* b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return false;
        if (!x) return true;
    }
}

static inline int drive_set_find(const drive_set_t* s, const char* name) {
    for (int i = 0; i < s->count; i++)
        if (drive_set_same_(s->names[i], name)) return i;
    return -1;
}

static inline bool drive_set_in_use(const drive_set_t* s, int image, int except_drive) {
    for (int d = 0; d < DRIVE_SET_DRIVES; d++)
        if (d != except_drive && s->slot[d] == image) return true;
    return false;
}

// Affectation de départ ; wanted[d] : nom réglé pour le lecteur d (NULL ou "" : aucun)
static inline void drive_set_assign(drive_set_t* s, const char* const wanted[DRIVE_SET_DRIVES]) {
    for (int d = 0; d < DRIVE_SET_DRIVES; d++) s->slot[d] = -1;
    s->unmatched = 0;
    for (int d = 0; d < DRIVE_SET_DRIVES; d++) {
        if (!wanted[d] || !wanted[d][0]) continue;
        int i = drive_set_find(s, wanted[d]);
        if (i >= 0 && !drive_set_in_use(s, i, d)) s->slot[d] = i;
        else s->unmatched |= (uint8_t)(1u << d);
    }
    // Règle « première image libre » : seulement sans réglage « a= »
    if (s->slot[0] < 0 && !(wanted[0] && wanted[0][0])) {
        for (int i = 0; i < s->count; i++) {
            if (!drive_set_in_use(s, i, 0)) {
                s->slot[0] = i;
                break;
            }
        }
    }
}
