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

// Profils de démarrage (TELESTRA.CFG « demarrage=choix » : proposés au
// démarrage ; « demarrage=ID » : appliqué) : banques 1-7 d'origine, puis la
// cartouche complète de la banque 7 (bank7 vide : Telestrat d'origine)
typedef struct {
    const char* id;
    const char* label;
    const char* bank7;
} rom_profile_t;

static const rom_profile_t rom_profiles[] = {
    {"telestrat", "Telestrat : TELEMON 2.4, HYPER-BASIC, TELE-ASS, TELEMATIC", ""},
    {"stratoric", "STRATORIC : mode Atmos, disquettes SEDORIC, cassettes", "@stratoric"},
    {"atmos", "ORIC BASIC 1.1 : mode Atmos simple, cassettes", "@atmos"},
};
#define ROM_PROFILES ((int)(sizeof(rom_profiles) / sizeof(rom_profiles[0])))

static inline int rom_profile_find(const char* id) {
    for (int i = 0; i < ROM_PROFILES; i++)
        if (!strcmp(rom_profiles[i].id, id)) return i;
    return -1;
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

// Profils de la clé : TELESTRA.CFG « profil=Libellé;bank7=X;bank6=Y;… »
// (X : fichier .rom de la clé ou ROM intégrée « @… » ; banques non citées :
// contenu d'origine), proposés après les profils intégrés. Au plus
// ROM_USER_PROFILES ; libellés de ROM_USER_LABEL - 1 caractères au plus.
#define ROM_USER_PROFILES 3
#define ROM_USER_LABEL    40

// Libellé d'une valeur « profil= » (jusqu'au premier « ; »)
static inline void rom_user_profile_label(const char* v, char* out, size_t cap) {
    size_t n = 0;
    while (v[n] && v[n] != ';' && n + 1 < cap) {
        out[n] = v[n];
        n++;
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
}

// Chargement d'un fichier .rom de la clé en banque (plate-forme)
typedef bool (*rom_load_file_t)(void* ctx, int bank, const char* name, const char** err);

// Profil de la clé appliqué (le démarrage à froid est à faire ensuite)
static inline bool rom_user_profile_apply(rom_pool_t* p, telestrat_t* sys, const char* v, rom_load_file_t load,
                                          void* ctx, const char** err) {
    for (int b = 1; b < 8; b++) rom_pool_restore(p, sys, b);
    const char* f = strchr(v, ';');
    while (f) {
        f++;
        const char* end = strchr(f, ';');
        const size_t n = end ? (size_t)(end - f) : strlen(f);
        char item[64];
        if (n < sizeof(item)) {
            memcpy(item, f, n);
            item[n] = 0;
            for (size_t k = n; k && item[k - 1] == ' '; k--) item[k - 1] = 0;
            if (!strncmp(item, "bank", 4) && item[4] >= '1' && item[4] <= '7' && item[5] == '=' && item[6]) {
                const int b = item[4] - '0';
                const char* name = item + 6;
                const rom_builtin_t* rb = name[0] == '@' ? rom_builtin_find(name) : NULL;
                if (name[0] == '@' && !rb) {
                    *err = "ROM intégrée inconnue";
                    return false;
                }
                if (rb ? !rom_pool_load_builtin(p, sys, b, rb, err) : !load(ctx, b, name, err)) return false;
            }
        }
        f = end;
    }
    return true;
}

// Profil appliqué (banques ; le démarrage à froid est à faire ensuite)
static inline bool rom_profile_apply(rom_pool_t* p, telestrat_t* sys, int profile, const char** err) {
    if (profile < 0 || profile >= ROM_PROFILES) {
        *err = "profil inconnu";
        return false;
    }
    for (int b = 1; b < 8; b++) rom_pool_restore(p, sys, b);
    const char* id = rom_profiles[profile].bank7;
    if (!id[0]) return true;
    const rom_builtin_t* rb = rom_builtin_find(id);
    return rb && rom_pool_load_builtin(p, sys, 7, rb, err);
}
