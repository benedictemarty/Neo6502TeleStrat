#pragma once

// rom_pool.h — emplacements de banque en RAM : ROM intégrées et cartouches de la clé
//
// Les ROM intégrées (TELEMON, HYPER-BASIC…) restent en flash ; chacune est
// copiée au démarrage dans un emplacement de 16 Ko en RAM, d'où le 65C02 lit
// sans attente. Une cartouche de la clé réutilise l'emplacement de la ROM
// qu'elle remplace ; les emplacements supplémentaires (la « banque USB
// supplémentaire ») servent aux cartouches placées dans une banque vide ou de
// RAM. Contenu d'origine : la ROM est recopiée depuis la flash, sinon
// l'emplacement est libéré.
//
// À inclure après systems/telestrat.h (telestrat_set_bank_rom,
// telestrat_restore_bank). Indépendant de la plate-forme (testé dans
// tests/test_telestrat.c).
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
#include <stdio.h>
#include <string.h>

#include "osd/osd_config.h"

#define ROM_POOL_MAX   8
#define ROM_POOL_NAME  48

typedef struct {
    uint8_t (*slot)[OSD_BANK_BYTES];
    int nslots;
    int slot_bank[ROM_POOL_MAX];     // banque servie par l'emplacement, -1 : libre
    const uint8_t* builtin[8];       // ROM intégrée d'origine de la banque (flash), NULL sinon
    char name[8][ROM_POOL_NAME];     // cartouche de la clé en banque b ("" : origine)
} rom_pool_t;

static inline void rom_pool_init(rom_pool_t* p, uint8_t (*slots)[OSD_BANK_BYTES], int nslots) {
    memset(p, 0, sizeof(*p));
    p->slot = slots;
    p->nslots = nslots < ROM_POOL_MAX ? nslots : ROM_POOL_MAX;
    for (int s = 0; s < ROM_POOL_MAX; s++) p->slot_bank[s] = -1;
}

static inline int rom_pool_slot_of(const rom_pool_t* p, int bank) {
    for (int s = 0; s < p->nslots; s++)
        if (p->slot_bank[s] == bank) return s;
    return -1;
}

static inline int _rom_pool_free_slot(const rom_pool_t* p) {
    for (int s = 0; s < p->nslots; s++)
        if (p->slot_bank[s] < 0) return s;
    return -1;
}

// Emplacements libres (pour les cartouches en banque vide)
static inline int rom_pool_free(const rom_pool_t* p) {
    int n = 0;
    for (int s = 0; s < p->nslots; s++) n += p->slot_bank[s] < 0;
    return n;
}

// ROM intégrée en banque (avant telestrat_init) : copiée dans un emplacement,
// dont l'adresse va dans le descripteur ; NULL s'il n'y a plus de place
static inline const uint8_t* rom_pool_builtin(rom_pool_t* p, int bank, const uint8_t* flash) {
    const int s = _rom_pool_free_slot(p);
    if (s < 0 || bank < 0 || bank > 7) return NULL;
    memcpy(p->slot[s], flash, OSD_BANK_BYTES);
    p->slot_bank[s] = bank;
    p->builtin[bank] = flash;
    return p->slot[s];
}

// Contenu d'origine de la banque
static inline void rom_pool_restore(rom_pool_t* p, telestrat_t* sys, int bank) {
    if (bank < 0 || bank > 7) return;
    const int s = rom_pool_slot_of(p, bank);
    if (p->builtin[bank]) {
        if (s >= 0) memcpy(p->slot[s], p->builtin[bank], OSD_BANK_BYTES);
    } else if (s >= 0) {
        p->slot_bank[s] = -1;
    }
    telestrat_restore_bank(sys, bank);
    p->name[bank][0] = 0;
}

// Emplacement à remplir pour une cartouche de `size` octets en banque ; la
// banque est vidée le temps du chargement. NULL et *err si impossible.
static inline uint8_t* rom_pool_claim(rom_pool_t* p, telestrat_t* sys, int bank, size_t size, const char** err) {
    if (bank < 1 || bank > 7) {
        *err = "banque invalide";
        return NULL;
    }
    if (!osd_rom_size_ok(size)) {
        *err = "taille invalide (16, 8, 4, 2 ou 1 Ko)";
        return NULL;
    }
    int s = rom_pool_slot_of(p, bank);
    if (s < 0) s = _rom_pool_free_slot(p);
    if (s < 0) {
        *err = "plus de place : rendre une cartouche de la clé à son contenu d'origine";
        return NULL;
    }
    p->slot_bank[s] = bank;
    telestrat_set_bank_rom(sys, bank, NULL);
    return p->slot[s];
}

// Cartouche lue dans l'emplacement (premiers `size` octets) : répétée et branchée
static inline void rom_pool_commit(rom_pool_t* p, telestrat_t* sys, int bank, size_t size, const char* name) {
    const int s = rom_pool_slot_of(p, bank);
    if (s < 0) return;
    for (size_t base = size; base < OSD_BANK_BYTES; base += size) memcpy(p->slot[s] + base, p->slot[s], size);
    telestrat_set_bank_rom(sys, bank, p->slot[s]);
    snprintf(p->name[bank], sizeof(p->name[bank]), "%.47s", name);
}

// Lecture échouée après rom_pool_claim : contenu d'origine
static inline void rom_pool_abort(rom_pool_t* p, telestrat_t* sys, int bank) { rom_pool_restore(p, sys, bank); }
