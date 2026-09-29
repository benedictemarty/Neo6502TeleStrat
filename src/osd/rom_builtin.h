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
    const char* label;  // menu (choix d'une cartouche)
    const char* name;   // panneau des cartouches (court)
    const uint8_t* rom; // 16 Ko en flash
    // Cartouche complète : autres banques chargées avec celle-ci (banque 0 : fin)
    struct {
        int bank;
        const char* id;
    } with[2];
} rom_builtin_t;

// STRATORIC (manuel du développeur Telestrat) : banque 7 SEDORIC + démarrage,
// 6 ORIC BASIC V1.1, 5 ORIC BASIC V1.0 — mode Atmos avec disquettes SEDORIC et
// cassettes. ORIC BASIC 1.1 seul en banque 7 : mode Atmos simple (cassettes).
static const rom_builtin_t rom_builtins[] = {
    {"@stratoric", "STRATORIC 4.0 (+ BASIC 1.1 en 6, 1.0 en 5)", "STRATORIC 4.0", telestrat_stratoric,
     {{6, "@atmos"}, {5, "@basic10"}}},
    {"@atmos", "ORIC BASIC 1.1 (Atmos)", "ORIC BASIC 1.1", telestrat_atmos, {{0, NULL}, {0, NULL}}},
    {"@basic10", "ORIC BASIC 1.0 (Oric-1)", "ORIC BASIC 1.0", telestrat_basic10, {{0, NULL}, {0, NULL}}},
};
#define ROM_BUILTINS ((int)(sizeof(rom_builtins) / sizeof(rom_builtins[0])))

// Par identifiant ou libellé ; NULL si inconnue
static inline const rom_builtin_t* rom_builtin_find(const char* name) {
    for (int i = 0; i < ROM_BUILTINS; i++)
        if (!strcmp(rom_builtins[i].id, name) || !strcmp(rom_builtins[i].label, name)) return &rom_builtins[i];
    return NULL;
}

// Nom du contenu d'une banque tenu par le pool (cartouche de la clé ou ROM intégrée)
static inline const char* rom_builtin_label(const char* pool_name) {
    const rom_builtin_t* b = pool_name[0] == '@' ? rom_builtin_find(pool_name) : NULL;
    return b ? b->name : pool_name;
}

static inline bool _rom_pool_load_one(rom_pool_t* p, telestrat_t* sys, int bank, const rom_builtin_t* b,
                                      const char** err) {
    uint8_t* dst = rom_pool_claim(p, sys, bank, OSD_BANK_BYTES, err);
    if (!dst) return false;
    memcpy(dst, b->rom, OSD_BANK_BYTES);
    rom_pool_commit(p, sys, bank, OSD_BANK_BYTES, b->id);
    return true;
}

// ROM intégrée en banque (emplacement du pool), avec les autres banques d'une
// cartouche complète ; false et *err si l'une d'elles n'a pas de place
static inline bool rom_pool_load_builtin(rom_pool_t* p, telestrat_t* sys, int bank, const rom_builtin_t* b,
                                         const char** err) {
    if (!_rom_pool_load_one(p, sys, bank, b, err)) return false;
    for (int i = 0; i < 2; i++) {
        const rom_builtin_t* w = b->with[i].bank ? rom_builtin_find(b->with[i].id) : NULL;
        if (w && !_rom_pool_load_one(p, sys, b->with[i].bank, w, err)) return false;
    }
    return true;
}
