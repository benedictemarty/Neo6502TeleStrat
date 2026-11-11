#pragma once

// rom_builtin.h — ROM intégrées au firmware proposées par le menu pour une banque
//
// Désignées dans TELESTRA.CFG par leur identifiant (« bank7=@atmos »). À
// inclure après roms/telestrat_roms.h et osd/rom_pool.h.
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
#ifdef TELESTRAT_ROMS_CLE
// Version slot du firmware : aucune cartouche en flash (elles viennent de la clé)
static const rom_builtin_t rom_builtins[] = {{NULL, NULL, NULL, NULL, {{0, NULL}, {0, NULL}}}};
#define ROM_BUILTINS 0
#else
// Une cartouche absente à la génération (tools/fetch_roms.py : TELESTRAT_HAS_<NOM>
// à 0, tableau à zéro) n'est pas proposée ; STRATORIC exige ses deux BASIC. La
// dernière entrée est une sentinelle (un tableau vide n'existe pas en C).
static const rom_builtin_t rom_builtins[] = {
#if TELESTRAT_HAS_STRATORIC && TELESTRAT_HAS_ATMOS && TELESTRAT_HAS_BASIC10
    {"@stratoric", "STRATORIC 4.0 (+ BASIC 1.1 en 6, 1.0 en 5)", "STRATORIC 4.0", telestrat_stratoric,
     {{6, "@atmos"}, {5, "@basic10"}}},
#endif
#if TELESTRAT_HAS_ATMOS
    {"@atmos", "ORIC BASIC 1.1 (Atmos)", "ORIC BASIC 1.1", telestrat_atmos, {{0, NULL}, {0, NULL}}},
#endif
#if TELESTRAT_HAS_BASIC10
    {"@basic10", "ORIC BASIC 1.0 (Oric-1)", "ORIC BASIC 1.0", telestrat_basic10, {{0, NULL}, {0, NULL}}},
#endif
    {NULL, NULL, NULL, NULL, {{0, NULL}, {0, NULL}}},
};
#define ROM_BUILTINS ((int)(sizeof(rom_builtins) / sizeof(rom_builtins[0])) - 1)
#endif

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

#define ROM_ERR_NO_BUILTIN "ROM intégrée absente de ce firmware (version slot : cartouches sur la clé)"

static inline bool _rom_pool_load_one(rom_pool_t* p, telestrat_t* sys, int bank, const rom_builtin_t* b,
                                      const char** err) {
    uint8_t* dst = rom_pool_claim(p, sys, bank, OSD_BANK_BYTES, err);
    if (!dst) return false;
    memcpy(dst, b->rom, OSD_BANK_BYTES);
    rom_pool_commit(p, sys, bank, OSD_BANK_BYTES, b->id);
    return true;
}

// Banques (bit b : banque b) qu'une cartouche intégrée met en place : la
// sienne, plus celles des autres ROM d'une cartouche complète
static inline uint32_t _rom_builtin_banks(int bank, const rom_builtin_t* b) {
    uint32_t mask = 1u << bank;
    for (int i = 0; i < 2; i++)
        if (b->with[i].bank && rom_builtin_find(b->with[i].id)) mask |= 1u << b->with[i].bank;
    return mask;
}

// Vérification préalable de la place : les banques du masque trouveront-elles
// un emplacement ? fresh : après remise à l'origine des banques 1 à 7 (profil) ;
// sinon l'état actuel du pool. Rien n'est modifié.
static inline bool _rom_pool_fits(const rom_pool_t* p, uint32_t mask, bool fresh, const char** err) {
    int need = 0, avail;
    if (fresh) {
        avail = p->nslots;
        for (int b = 0; b < 8; b++) avail -= p->builtin[b] != NULL;
    } else {
        avail = rom_pool_free(p);
    }
    for (int b = 1; b < 8; b++) {
        if (!(mask & (1u << b))) continue;
        if (fresh ? !p->builtin[b] : rom_pool_slot_of(p, b) < 0) need++;
    }
    if (need > avail) {
        *err = fresh ? "plus de place : pas d'emplacement de banque pour toutes les cartouches du profil"
                     : "plus de place : rendre une cartouche de la clé à son contenu d'origine";
        return false;
    }
    return true;
}

// ROM intégrée en banque (emplacement du pool), avec les autres banques d'une
// cartouche complète ; false et *err si l'une d'elles n'a pas de place (vérifié
// avant de toucher à une banque : une cartouche complète n'est pas chargée à moitié)
static inline bool rom_pool_load_builtin(rom_pool_t* p, telestrat_t* sys, int bank, const rom_builtin_t* b,
                                         const char** err) {
    if (bank < 1 || bank > 7) {
        *err = "banque invalide";
        return false;
    }
    if (!_rom_pool_fits(p, _rom_builtin_banks(bank, b), false, err)) return false;
    if (!_rom_pool_load_one(p, sys, bank, b, err)) return false;
    for (int i = 0; i < 2; i++) {
        const rom_builtin_t* w = b->with[i].bank ? rom_builtin_find(b->with[i].id) : NULL;
        if (w && !_rom_pool_load_one(p, sys, b->with[i].bank, w, err)) return false;
    }
    return true;
}

// Chargement d'un fichier .rom de la clé en banque (plate-forme)
typedef bool (*rom_load_file_t)(void* ctx, int bank, const char* name, const char** err);
// Taille d'un fichier .rom de la clé (plate-forme), sans le charger ; false s'il est illisible
typedef bool (*rom_probe_file_t)(void* ctx, const char* name, size_t* size);

// Contenu désigné par un nom de TELESTRA.CFG ou d'un instantané : ROM intégrée
// (« @… ») ou fichier .rom ; *err dit pourquoi une ROM intégrée n'est pas chargée
static inline bool rom_load_named(rom_pool_t* p, telestrat_t* sys, int bank, const char* name, rom_load_file_t load,
                                  void* ctx, const char** err) {
    if (name[0] != '@') return load(ctx, bank, name, err);
    const rom_builtin_t* rb = rom_builtin_find(name);
    if (!rb) {
        *err = "ROM intégrée inconnue";
        return false;
    }
    return rom_pool_load_builtin(p, sys, bank, rb, err);
}

// Profils de la clé : TELESTRA.CFG « profil=Libellé;bank7=X;bank6=Y;… »
// (X : fichier .rom de la clé ou ROM intégrée « @… » ; banques non citées :
// contenu d'origine), proposés après les profils intégrés. Au plus
// ROM_USER_PROFILES ; libellés de ROM_USER_LABEL - 1 caractères au plus ;
// éléments de ROM_ITEM_LEN - 1 caractères au plus (« bankN=nom » compris).
#define ROM_USER_PROFILES 3
#define ROM_USER_LABEL    40
#define ROM_ITEM_LEN      64

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

// Élément « bankN=nom » suivant d'un profil ; *pos : le « ; » qui le précède
// (NULL : fin), avancé. 1 : trouvé (item = « bankN=nom », *bank = N), 0 : plus
// d'élément, -1 : un élément (quel qu'il soit) dépasse ROM_ITEM_LEN - 1 caractères.
// Les éléments qui ne sont pas « bankN=nom » sont sautés.
static inline int _rom_user_next(const char** pos, char* item, int* bank) {
    while (*pos) {
        const char* f = *pos + 1;
        const char* end = strchr(f, ';');
        const size_t n = end ? (size_t)(end - f) : strlen(f);
        *pos = end;
        if (n >= ROM_ITEM_LEN) return -1;
        memcpy(item, f, n);
        item[n] = 0;
        for (size_t k = n; k && item[k - 1] == ' '; k--) item[k - 1] = 0;
        if (!strncmp(item, "bank", 4) && item[4] >= '1' && item[4] <= '7' && item[5] == '=' && item[6]) {
            *bank = item[4] - '0';
            return 1;
        }
    }
    return 0;
}

static char _rom_err_buf[96];

// Vérification préalable d'un profil de la clé, avant de toucher à une banque :
// éléments de longueur admise, ROM intégrées connues et présentes, fichiers
// lisibles et de taille admise (probe ; NULL : non vérifiés), emplacements en
// nombre suffisant. *err : la cause (valable jusqu'à l'appel suivant).
static inline bool rom_user_profile_check(const rom_pool_t* p, const char* v, rom_probe_file_t probe, void* ctx,
                                          const char** err) {
    const char* f = strchr(v, ';');
    char item[ROM_ITEM_LEN];
    uint32_t mask = 0;
    int b, r;
    while ((r = _rom_user_next(&f, item, &b)) > 0) {
        const char* name = item + 6;
        if (name[0] == '@') {
            const rom_builtin_t* rb = rom_builtin_find(name);
            if (!rb) {
                *err = "ROM intégrée inconnue";
                return false;
            }
            mask |= _rom_builtin_banks(b, rb);
            continue;
        }
        size_t size = 0;
        if (probe && !probe(ctx, name, &size)) {
            snprintf(_rom_err_buf, sizeof(_rom_err_buf), "%.60s : fichier illisible", name);
            *err = _rom_err_buf;
            return false;
        }
        if (probe && !osd_rom_size_ok(size)) {
            snprintf(_rom_err_buf, sizeof(_rom_err_buf), "%.40s : taille invalide (16, 8, 4, 2 ou 1 Ko)", name);
            *err = _rom_err_buf;
            return false;
        }
        mask |= 1u << b;
    }
    if (r < 0) {
        *err = "élément de profil trop long (63 caractères au plus)";
        return false;
    }
    return _rom_pool_fits(p, mask, true, err);
}

// Profil de la clé appliqué (le démarrage à froid est à faire ensuite). Le
// profil est vérifié en entier avant toute modification ; un échec ensuite
// (lecture d'un fichier) laisse des banques changées : démarrage à froid à faire aussi.
static inline bool rom_user_profile_apply(rom_pool_t* p, telestrat_t* sys, const char* v, rom_load_file_t load,
                                          void* ctx, const char** err) {
    if (!rom_user_profile_check(p, v, NULL, NULL, err)) return false;
    for (int b = 1; b < 8; b++) rom_pool_restore(p, sys, b);
    const char* f = strchr(v, ';');
    char item[ROM_ITEM_LEN];
    int b;
    while (_rom_user_next(&f, item, &b) > 0)
        if (!rom_load_named(p, sys, b, item + 6, load, ctx, err)) return false;
    return true;
}

// Vérification préalable d'un profil intégré (voir rom_user_profile_check)
static inline bool rom_profile_check(const rom_pool_t* p, int profile, const char** err) {
    if (profile < 0 || profile >= ROM_PROFILES) {
        *err = "profil inconnu";
        return false;
    }
    const char* id = rom_profiles[profile].bank7;
    if (!id[0]) return true;
    const rom_builtin_t* rb = rom_builtin_find(id);
    if (!rb) {
        *err = ROM_ERR_NO_BUILTIN;
        return false;
    }
    return _rom_pool_fits(p, _rom_builtin_banks(7, rb), true, err);
}

// Profil appliqué (banques ; le démarrage à froid est à faire ensuite), après
// vérification préalable
static inline bool rom_profile_apply(rom_pool_t* p, telestrat_t* sys, int profile, const char** err) {
    if (!rom_profile_check(p, profile, err)) return false;
    for (int b = 1; b < 8; b++) rom_pool_restore(p, sys, b);
    const char* id = rom_profiles[profile].bank7;
    if (!id[0]) return true;
    return rom_pool_load_builtin(p, sys, 7, rom_builtin_find(id), err);
}
