#pragma once

// rom_builtin.h — ROM intégrées au firmware proposées par le menu pour une banque
//
// Désignées dans TELESTRA.CFG par leur identifiant (« bank7=@atmos »). À
// inclure après roms/telestrat_roms.h et osd/rom_pool.h.
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <string.h>

typedef struct {
    const char* id;     // TELESTRA.CFG
    const char* label;  // menu
    const uint8_t* rom; // 16 Ko en flash
} rom_builtin_t;

// Cartouche Atmos : ORIC EXTENDED BASIC V1.1, en banque 7 pour le mode Atmos
// (lecteur de cassette)
static const rom_builtin_t rom_builtins[] = {
    {"@atmos", "ORIC BASIC 1.1 (Atmos)", telestrat_atmos},
};
#define ROM_BUILTINS ((int)(sizeof(rom_builtins) / sizeof(rom_builtins[0])))

// Par identifiant ou libellé ; NULL si inconnue
static inline const rom_builtin_t* rom_builtin_find(const char* name) {
    for (int i = 0; i < ROM_BUILTINS; i++)
        if (!strcmp(rom_builtins[i].id, name) || !strcmp(rom_builtins[i].label, name)) return &rom_builtins[i];
    return NULL;
}

// Libellé du contenu d'une banque tenu par le pool (cartouche de la clé ou ROM intégrée)
static inline const char* rom_builtin_label(const char* pool_name) {
    const rom_builtin_t* b = pool_name[0] == '@' ? rom_builtin_find(pool_name) : NULL;
    return b ? b->label : pool_name;
}

// ROM intégrée en banque (emplacement du pool) ; false et *err si impossible
static inline bool rom_pool_load_builtin(rom_pool_t* p, telestrat_t* sys, int bank, const rom_builtin_t* b,
                                         const char** err) {
    uint8_t* dst = rom_pool_claim(p, sys, bank, OSD_BANK_BYTES, err);
    if (!dst) return false;
    memcpy(dst, b->rom, OSD_BANK_BYTES);
    rom_pool_commit(p, sys, bank, OSD_BANK_BYTES, b->id);
    return true;
}
