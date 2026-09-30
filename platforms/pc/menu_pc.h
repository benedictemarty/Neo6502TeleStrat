#pragma once

// menu_pc.h — menu (OSD) du banc PC : un répertoire tient lieu de clé USB
//
// Même menu et mêmes réglages que le Neo6502 (src/osd) : images .dsk et .rom
// du répertoire, TELESTRA.CFG (a= … d=, bank1= … bank7=) appliqué au
// démarrage et réécrit par « Enregistrer ». Les disquettes sont chargées en
// mémoire et réécrites dans leur fichier à la fin si elles ont changé.
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "osd/osd_menu.h"
#include "osd/osd_config.h"
#include "osd/rom_pool.h"
#include "osd/rom_builtin.h"
#include "systems/telestrat_state.h"

// Comme le firmware : un emplacement par ROM intégrée, plus un supplémentaire
#define MENU_PC_EXTRA_SLOTS 1

typedef struct {
    const char* dir;
    // Périphériques (menu, TELESTRA.CFG impression=, imprimante_type= et modem=)
    bool printer_on, modem_on;
    const char* printer_file;  // -P (NULL : pas d'imprimante branchée au banc)
    int printer_type;          // OSD_PRINTER_* (imprimante_type=)
    int printer_types;         // modèles proposés par le menu (3 avec -G, sinon 1)
    const char* printer_last;  // dernière page ou dernier tracé écrit (-G)
    bool boot_pending;         // demarrage=choix : page de démarrage à la première ouverture
    char user_label[ROM_USER_PROFILES][ROM_USER_LABEL];  // profils de la clé (profil=)
    int user_n;
    bool tape_turbo;           // cassette_rapide= (-Z)
    bool tape_motor_always;    // cassette_moteur= (-Y)
    bool line_present;         // -L
    osd_menu_t menu;
    osd_surface_t surf;
    uint8_t* tape;
    size_t tape_size;
    char tape_name[OSD_NAME_LEN];
    uint8_t* disk[4];
    size_t disk_size[4];
    char disk_name[4][OSD_NAME_LEN];
    uint8_t rom[ROM_POOL_MAX][OSD_BANK_BYTES];
    rom_pool_t pool;
} menu_pc_t;

// Avant telestrat_init : ROM intégrées du descripteur copiées dans les emplacements
static void menu_pc_prepare(menu_pc_t* p, telestrat_desc_t* desc) {
    int builtin = 0;
    for (int b = 0; b < 8; b++) builtin += desc->banks[b].type == TELESTRAT_BANK_ROM && desc->banks[b].rom;
    rom_pool_init(&p->pool, p->rom, builtin + MENU_PC_EXTRA_SLOTS);
    for (int b = 0; b < 8; b++)
        if (desc->banks[b].type == TELESTRAT_BANK_ROM && desc->banks[b].rom)
            desc->banks[b].rom = rom_pool_builtin(&p->pool, b, desc->banks[b].rom);
}

static bool _menu_pc_ext(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        char c = name[n - e + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

static int _menu_pc_cmp(const void* a, const void* b) {
    return strcmp(((const osd_file_t*)a)->name, ((const osd_file_t*)b)->name);
}

static void _menu_pc_path(const menu_pc_t* p, const char* name, char* out, size_t n) {
    snprintf(out, n, "%s/%s", p->dir, name);
}

static void menu_pc_scan(menu_pc_t* p) {
    osd_menu_t* m = &p->menu;
    m->nfiles = 0;
    m->usb_present = false;
    DIR* d = opendir(p->dir);
    if (!d) return;
    m->usb_present = true;
    snprintf(m->usb_label, sizeof(m->usb_label), "Répertoire %s", p->dir);
    struct dirent* e;
    while ((e = readdir(d)) && m->nfiles < OSD_MENU_FILES) {
        bool dsk = _menu_pc_ext(e->d_name, ".dsk"), rom = _menu_pc_ext(e->d_name, ".rom"),
             tap = _menu_pc_ext(e->d_name, ".tap"), sta = _menu_pc_ext(e->d_name, ".sta");
        if ((!dsk && !rom && !tap && !sta) || strlen(e->d_name) >= OSD_NAME_LEN) continue;
        char path[512];
        struct stat st;
        _menu_pc_path(p, e->d_name, path, sizeof(path));
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        osd_file_t* f = &m->files[m->nfiles++];
        snprintf(f->name, sizeof(f->name), "%s", e->d_name);
        f->size = (uint32_t)st.st_size;
        f->kind = dsk ? OSD_FILE_DSK : tap ? OSD_FILE_TAP : sta ? OSD_FILE_STA : OSD_FILE_ROM;
    }
    closedir(d);
    qsort(m->files, (size_t)m->nfiles, sizeof(m->files[0]), _menu_pc_cmp);
}

static uint8_t* _menu_pc_read(const menu_pc_t* p, const char* name, size_t* size) {
    char path[512];
    _menu_pc_path(p, name, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = n > 0 ? malloc((size_t)n) : NULL;
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

// Réécrit les disquettes modifiées (fin du banc, éjection)
static void _menu_pc_flush(menu_pc_t* p, telestrat_t* sys, int d) {
    if (!p->disk[d] || !sys->fdc.wd.disk[d].modified) return;
    char path[512];
    _menu_pc_path(p, p->disk_name[d], path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(p->disk[d], 1, p->disk_size[d], f);
        fclose(f);
    }
}

static bool menu_pc_insert(menu_pc_t* p, telestrat_t* sys, int d, const char* name) {
    for (int o = 0; o < 4; o++)
        if (o != d && p->disk[o] && !strcmp(p->disk_name[o], name)) return false;  // déjà dans un autre lecteur
    size_t size = 0;
    uint8_t* img = _menu_pc_read(p, name, &size);
    if (!img) return false;
    // En-tête vérifié avant de toucher au lecteur
    if (size < 16 || memcmp(img, "MFM_DISK", 8) != 0) {
        free(img);
        return false;
    }
    _menu_pc_flush(p, sys, d);  // l'image en place, si elle a changé
    if (!telestrat_insert_disk(sys, d, img, size, false)) {
        free(img);
        return false;
    }
    free(p->disk[d]);
    p->disk[d] = img;
    p->disk_size[d] = size;
    snprintf(p->disk_name[d], sizeof(p->disk_name[d]), "%s", name);
    return true;
}

static void menu_pc_eject(menu_pc_t* p, telestrat_t* sys, int d) {
    _menu_pc_flush(p, sys, d);  // avant l'éjection, qui efface le drapeau « modifié »
    wd1793_eject(&sys->fdc.wd, d);
    free(p->disk[d]);
    p->disk[d] = NULL;
    p->disk_name[d][0] = 0;
}

static void menu_pc_restore(menu_pc_t* p, telestrat_t* sys, int bank) { rom_pool_restore(&p->pool, sys, bank); }

// Cartouche de la clé en banque ; message d'erreur dans err
static bool menu_pc_load_rom(menu_pc_t* p, telestrat_t* sys, int bank, const char* name, const char** err) {
    size_t size = 0;
    uint8_t* data = _menu_pc_read(p, name, &size);
    if (!data) {
        *err = "fichier illisible";
        return false;
    }
    uint8_t* dst = rom_pool_claim(&p->pool, sys, bank, size, err);
    if (dst) {
        memcpy(dst, data, size);
        rom_pool_commit(&p->pool, sys, bank, size, name);
    }
    free(data);
    return dst != NULL;
}

static const char* menu_pc_bank_label(const menu_pc_t* p, const telestrat_t* sys, int b) {
    const uint8_t* r = p->pool.builtin[b];
    if (r == telestrat_telemon24) return "TELEMON 2.4";
    if (r == telestrat_hyperbas) return "HYPER-BASIC";
    if (r == telestrat_teleass) return "TELE-ASS";
    if (r == telestrat_telematic) return "TELEMATIC";
    return sys->bank_type_orig[b] == TELESTRAT_BANK_RAM ? (b == 0 ? "RAM interne" : "RAM 16 Ko") : "";
}

// État du menu d'après le système
static bool _menu_pc_tape_read(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
    const menu_pc_t* p = ctx;
    if ((size_t)off + len > p->tape_size) return false;
    memcpy(buf, p->tape + off, len);
    return true;
}

// Cassette de la clé (la même : rembobinée)
static bool menu_pc_tape(menu_pc_t* p, telestrat_t* sys, const char* name) {
    size_t size = 0;
    uint8_t* img = _menu_pc_read(p, name, &size);
    if (!img) return false;
    free(p->tape);
    p->tape = img;
    p->tape_size = size;
    snprintf(p->tape_name, sizeof(p->tape_name), "%s", name);
    telestrat_tape_insert(sys, (uint32_t)size, _menu_pc_tape_read, p);
    return true;
}

// Options de la cassette appliquées : système, ROM des emplacements
// (BASIC 1.1 patché ou remis) ; retourne le nombre de BASIC 1.1 trouvés
static int menu_pc_tape_options(menu_pc_t* p, telestrat_t* sys) {
    telestrat_tape_options(sys, p->tape_turbo, p->tape_motor_always);
    return oric_turbo_apply_all(p->rom, p->pool.nslots, p->tape_turbo);
}

static void menu_pc_refresh(menu_pc_t* p, telestrat_t* sys) {
    osd_menu_t* m = &p->menu;
    m->printer_on = p->printer_on;
    m->printer_model = osd_printer_names[p->printer_type];
    const char* pf = p->printer_type != OSD_PRINTER_TEXT ? (p->printer_last ? p->printer_last : "")
                     : p->printer_file                  ? p->printer_file
                                                        : "";
    const char* base = strrchr(pf, '/');
    snprintf(m->printer_file, sizeof(m->printer_file), "%s", base ? base + 1 : pf);
    m->modem_on = p->modem_on;
    m->modem_state = p->line_present ? "ligne TCP (banc)" : "absent";
    m->tape_turbo = p->tape_turbo;
    m->tape_motor_always = p->tape_motor_always;
    snprintf(m->tape, sizeof(m->tape), "%s", sys->tape.inserted ? p->tape_name : "");
    m->tape_percent = oric_tape_percent(&sys->tape);
    m->tape_motor = sys->tape.motor && sys->tape.inserted;
    for (int k = 0; k < ROM_BUILTINS && k < OSD_BUILTINS; k++) m->builtin[k] = rom_builtins[k].label;
    for (int k = 0; k < OSD_PROFILES; k++) m->profile[k] = NULL;
    for (int k = 0; k < ROM_PROFILES && k < OSD_PROFILES; k++) m->profile[k] = rom_profiles[k].label;
    for (int k = 0; k < p->user_n && ROM_PROFILES + k < OSD_PROFILES; k++) m->profile[ROM_PROFILES + k] = p->user_label[k];
    for (int d = 0; d < 4; d++) {
        snprintf(m->drive[d], sizeof(m->drive[d]), "%s", p->disk[d] ? p->disk_name[d] : "");
        m->drive_ro[d] = p->disk[d] && sys->fdc.wd.disk[d].write_protected;
    }
    for (int b = 0; b < 8; b++) {
        if (p->pool.name[b][0]) {
            snprintf(m->bank[b], sizeof(m->bank[b]), "%s", rom_builtin_label(p->pool.name[b]));
            m->bank_kind[b] = p->pool.name[b][0] == '@' ? OSD_BANK_ROM : OSD_BANK_ROM_USB;
            continue;
        }
        snprintf(m->bank[b], sizeof(m->bank[b]), "%s", menu_pc_bank_label(p, sys, b));
        m->bank_kind[b] = sys->bank_type[b] == TELESTRAT_BANK_RAM   ? OSD_BANK_RAM
                          : sys->bank_type[b] == TELESTRAT_BANK_ROM ? OSD_BANK_ROM
                                                                    : OSD_BANK_EMPTY;
    }
}

static char* _menu_pc_read_cfg(const menu_pc_t* p) {
    size_t size = 0;
    uint8_t* d = _menu_pc_read(p, "TELESTRA.CFG", &size);
    if (!d) return NULL;
    char* s = realloc(d, size + 1);
    s[size] = 0;
    return s;
}

// Profil de la clé : la k-ième ligne « profil= » de TELESTRA.CFG
typedef struct {
    menu_pc_t* p;
    telestrat_t* sys;
} _menu_pc_load_ctx_t;

static bool menu_pc_load_rom(menu_pc_t* p, telestrat_t* sys, int bank, const char* name, const char** err);

static bool _menu_pc_load_cb(void* ctx, int bank, const char* name, const char** err) {
    _menu_pc_load_ctx_t* c = (_menu_pc_load_ctx_t*)ctx;
    return menu_pc_load_rom(c->p, c->sys, bank, name, err);
}

static bool menu_pc_user_profile(menu_pc_t* p, telestrat_t* sys, int k, const char** err) {
    char* cfg = _menu_pc_read_cfg(p);
    bool ok = false;
    *err = "profil absent de TELESTRA.CFG";
    int n = 0;
    for (char* line = cfg ? strtok(cfg, "\r\n") : NULL; line; line = strtok(NULL, "\r\n")) {
        const char* v = osd_config_value(line, "profil");
        if (!v || n++ != k) continue;
        _menu_pc_load_ctx_t c = {p, sys};
        ok = rom_user_profile_apply(&p->pool, sys, v, _menu_pc_load_cb, &c, err);
        break;
    }
    free(cfg);
    return ok;
}

// TELESTRA.CFG au démarrage : lecteurs et cartouches (après menu_pc_prepare
// et telestrat_init)
static void menu_pc_init(menu_pc_t* p, telestrat_t* sys, const char* dir, const char* version) {
    p->dir = dir;
    p->printer_on = p->modem_on = true;
    if (p->printer_types < 1) p->printer_types = 1;
    osd_menu_init(&p->menu);
    p->menu.version = version;
    menu_pc_scan(p);
    char* cfg = _menu_pc_read_cfg(p);
    bool profile_applied = false;
    char boot[ROM_USER_LABEL] = "";
    p->user_n = 0;
    for (char* line = cfg ? strtok(cfg, "\r\n") : NULL; line; line = strtok(NULL, "\r\n")) {
        const char* v;
        if ((v = osd_config_value(line, "impression"))) p->printer_on = osd_config_yes(v, true);
        if ((v = osd_config_value(line, "imprimante_type"))) {
            const int t = osd_printer_type(v, p->printer_type);
            if (t < p->printer_types) p->printer_type = t;
        }
        if ((v = osd_config_value(line, "modem"))) p->modem_on = osd_config_yes(v, true);
        if ((v = osd_config_value(line, "cassette_rapide"))) p->tape_turbo = osd_config_yes(v, p->tape_turbo);
        if ((v = osd_config_value(line, "profil")) && p->user_n < ROM_USER_PROFILES)
            rom_user_profile_label(v, p->user_label[p->user_n++], ROM_USER_LABEL);
        if ((v = osd_config_value(line, "demarrage"))) snprintf(boot, sizeof(boot), "%s", v);
        if ((v = osd_config_value(line, "cassette_moteur"))) p->tape_motor_always = !strcmp(v, "toujours");
        for (int d = 0; d < 4; d++) {
            const char key[2] = {(char)('a' + d), 0};
            if ((v = osd_config_value(line, key)) && !menu_pc_insert(p, sys, d, v))
                fprintf(stderr, "TELESTRA.CFG : %s illisible\n", v);
        }
        for (int b = 1; b < 8; b++) {
            char key[8];
            const char* err = "";
            snprintf(key, sizeof(key), "bank%d", b);
            if (!(v = osd_config_value(line, key))) continue;
            const rom_builtin_t* rb = v[0] == '@' ? rom_builtin_find(v) : NULL;
            if (v[0] == '@' && !rb) err = "ROM intégrée inconnue";
            if (rb ? !rom_pool_load_builtin(&p->pool, sys, b, rb, &err) : !menu_pc_load_rom(p, sys, b, v, &err))
                fprintf(stderr, "TELESTRA.CFG : %s : %s\n", v, err);
        }
    }
    free(cfg);
    // Démarrage : page de choix, profil intégré (identifiant) ou de la clé (libellé)
    if (!strcmp(boot, "choix")) {
        p->boot_pending = true;
    } else if (boot[0]) {
        const char* err = "profil inconnu";
        const int b = rom_profile_find(boot);
        if (b >= 0) {
            profile_applied = rom_profile_apply(&p->pool, sys, b, &err);
        } else {
            for (int k = 0; k < p->user_n; k++)
                if (!strcmp(boot, p->user_label[k])) profile_applied = menu_pc_user_profile(p, sys, k, &err);
        }
        if (!profile_applied) fprintf(stderr, "TELESTRA.CFG : demarrage=%s : %s\n", boot, err);
    }
    menu_pc_tape_options(p, sys);
    if (profile_applied) telestrat_cold_reset(sys);
}

static void menu_pc_save(menu_pc_t* p) {
    char* old = _menu_pc_read_cfg(p);
    const char* drives[4];
    const char* banks[8];
    for (int d = 0; d < 4; d++) drives[d] = p->disk[d] ? p->disk_name[d] : NULL;
    for (int b = 0; b < 8; b++) banks[b] = p->pool.name[b];
    static char out[4096];
    const osd_options_t opt = {p->printer_on, p->printer_type, p->modem_on, p->tape_turbo, p->tape_motor_always, -1};
    size_t n = osd_config_merge_ex(old, drives, banks, &opt, out, sizeof(out));
    free(old);
    char path[512];
    _menu_pc_path(p, "TELESTRA.CFG", path, sizeof(path));
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(out, 1, n, f);
        fclose(f);
    }
    osd_menu_message(&p->menu, !f, f ? "Configuration enregistrée dans TELESTRA.CFG" : "TELESTRA.CFG : écriture impossible");
}

/*-- Instantanés ---------------------------------------------------------------*/

static bool _menu_pc_state_write(void* ctx, void* d, uint32_t n) { return fwrite(d, 1, n, (FILE*)ctx) == n; }
static bool _menu_pc_state_read(void* ctx, void* d, uint32_t n) { return fread(d, 1, n, (FILE*)ctx) == n; }

// Texte de la plate-forme : cartouches (bank1= … bank7=, vide : origine),
// supports (pour information)
static void menu_pc_state_info(menu_pc_t* p, telestrat_t* sys, char* out, size_t cap) {
    size_t n = 0;
    for (int b = 1; b < 8 && n < cap; b++) n += (size_t)snprintf(out + n, cap - n, "bank%d=%s\n", b, p->pool.name[b]);
    for (int d = 0; d < 4 && n < cap; d++)
        if (p->disk[d]) n += (size_t)snprintf(out + n, cap - n, "%c=%s\n", 'a' + d, p->disk_name[d]);
    if (n < cap && sys->tape.inserted) snprintf(out + n, cap - n, "cassette=%s\n", p->tape_name);
}

// Nouvel instantané ETATnnnn.STA ; name : son nom
static bool menu_pc_state_save(menu_pc_t* p, telestrat_t* sys, char* name, size_t cap, const char** err) {
    char path[512];
    for (int k = 1; k <= 9999; k++) {
        snprintf(name, cap, "ETAT%04d.STA", k);
        _menu_pc_path(p, name, path, sizeof(path));
        FILE* t = fopen(path, "rb");
        if (!t) break;
        fclose(t);
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        *err = "écriture impossible";
        return false;
    }
    static char info[TELESTRAT_STATE_INFO_MAX];
    menu_pc_state_info(p, sys, info, sizeof(info));
    const bool ok = telestrat_state_save(sys, info, _menu_pc_state_write, f, err);
    fclose(f);
    if (!ok) remove(path);
    return ok;
}

// Reprise : cartouches de l'instantané remises, puis la machine
static bool menu_pc_state_load(menu_pc_t* p, telestrat_t* sys, const char* name, const char** err) {
    char path[512];
    _menu_pc_path(p, name, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) {
        *err = "illisible";
        return false;
    }
    static char info[TELESTRAT_STATE_INFO_MAX + 1];
    bool ok = telestrat_state_load_info(_menu_pc_state_read, f, info, sizeof(info), err);
    for (char* line = ok ? strtok(info, "\n") : NULL; ok && line; line = strtok(NULL, "\n")) {
        for (int b = 1; b < 8; b++) {
            char key[8];
            snprintf(key, sizeof(key), "bank%d", b);
            const char* v = osd_config_value(line, key);
            if (!v || !strcmp(v, p->pool.name[b])) continue;
            const rom_builtin_t* rb = v[0] == '@' ? rom_builtin_find(v) : NULL;
            if (!v[0]) menu_pc_restore(p, sys, b);
            else if (rb ? !rom_pool_load_builtin(&p->pool, sys, b, rb, err) : !menu_pc_load_rom(p, sys, b, v, err)) {
                *err = "cartouche de l'instantané absente de la clé";
                ok = false;
            }
        }
    }
    if (ok && !telestrat_state_load_machine(sys, _menu_pc_state_read, f, err)) {
        telestrat_cold_reset(sys);  // machine incohérente
        ok = false;
    }
    fclose(f);
    return ok;
}

// Exécute une action du menu ; retourne true s'il faut fermer le menu
static bool menu_pc_action(menu_pc_t* p, telestrat_t* sys, osd_action_t a) {
    osd_menu_t* m = &p->menu;
    char msg[96];
    const char* err = "";
    switch (a.type) {
        case OSD_ACT_INSERT:
            if (menu_pc_insert(p, sys, a.target, m->files[a.file].name)) {
                snprintf(msg, sizeof(msg), "Lecteur %c : %s", 'A' + a.target, m->files[a.file].name);
                osd_menu_message(m, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : image refusée (MFM_DISK, déjà en place ?)", m->files[a.file].name);
                osd_menu_message(m, true, msg);
            }
            break;
        case OSD_ACT_EJECT:
            menu_pc_eject(p, sys, a.target);
            snprintf(msg, sizeof(msg), "Lecteur %c vide", 'A' + a.target);
            osd_menu_message(m, false, msg);
            break;
        case OSD_ACT_LOAD_ROM:
            if (menu_pc_load_rom(p, sys, a.target, m->files[a.file].name, &err)) {
                snprintf(msg, sizeof(msg), "Banque %d : %s — RESET conseillé", a.target, m->files[a.file].name);
                osd_menu_message(m, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : %s", m->files[a.file].name, err);
                osd_menu_message(m, true, msg);
            }
            break;
        case OSD_ACT_RESTORE:
            menu_pc_restore(p, sys, a.target);
            snprintf(msg, sizeof(msg), "Banque %d : contenu d'origine", a.target);
            osd_menu_message(m, false, msg);
            break;
        case OSD_ACT_RESET:
            telestrat_cold_reset(sys);
            return true;
        case OSD_ACT_SAVE: menu_pc_save(p); break;
        case OSD_ACT_PRINTER:
            osd_printer_cycle(&p->printer_on, &p->printer_type, p->printer_types);
            if (p->printer_on) {
                char msg[64];
                snprintf(msg, sizeof(msg), "Imprimante : %s", osd_printer_names[p->printer_type]);
                osd_menu_message(m, false, msg);
            } else {
                osd_menu_message(m, false, "Imprimante coupée");
            }
            break;
        case OSD_ACT_MODEM:
            p->modem_on = !p->modem_on;
            osd_menu_message(m, false, p->modem_on ? "Modem activé" : "Modem coupé (ligne raccrochée)");
            break;
        case OSD_ACT_PROFILE:
            if (a.file < 0) {
                osd_menu_message(m, false, "Démarrage : configuration de la clé");
                return true;
            }
            if (a.file < ROM_PROFILES ? rom_profile_apply(&p->pool, sys, a.file, &err)
                                      : menu_pc_user_profile(p, sys, a.file - ROM_PROFILES, &err)) {
                if (p->tape_turbo) oric_turbo_apply_all(p->rom, p->pool.nslots, true);
                telestrat_cold_reset(sys);
                snprintf(msg, sizeof(msg), "Démarrage : %s",
                         a.file < ROM_PROFILES ? rom_profiles[a.file].label : p->user_label[a.file - ROM_PROFILES]);
                osd_menu_message(m, false, msg);
                menu_pc_refresh(p, sys);  // image du menu (-O) à jour
                return true;
            }
            snprintf(msg, sizeof(msg), "Démarrage : %s", err);
            osd_menu_message(m, true, msg);
            break;
        case OSD_ACT_STATE_SAVE:
            if (menu_pc_state_save(p, sys, m->state_last, sizeof(m->state_last), &err)) {
                snprintf(msg, sizeof(msg), "Instantané enregistré : %s", m->state_last);
                osd_menu_message(m, false, msg);
                menu_pc_scan(p);
            } else {
                snprintf(msg, sizeof(msg), "Instantané : %s", err);
                osd_menu_message(m, true, msg);
            }
            break;
        case OSD_ACT_STATE_LOAD:
            if (menu_pc_state_load(p, sys, m->files[a.file].name, &err)) {
                snprintf(m->state_last, sizeof(m->state_last), "%s", m->files[a.file].name);
                snprintf(msg, sizeof(msg), "Instantané repris : %s", m->files[a.file].name);
                osd_menu_message(m, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : %s", m->files[a.file].name, err);
                osd_menu_message(m, true, msg);
            }
            break;
        case OSD_ACT_TAPE_TURBO:
            p->tape_turbo = !p->tape_turbo;
            osd_menu_message(m, false, oric_turbo_message(p->tape_turbo, menu_pc_tape_options(p, sys)));
            break;
        case OSD_ACT_TAPE_MOTOR:
            p->tape_motor_always = !p->tape_motor_always;
            menu_pc_tape_options(p, sys);
            osd_menu_message(m, false, p->tape_motor_always ? "Moteur toujours en marche : la cassette défile dès son insertion"
                                                            : "Moteur commandé par le relais (PB6)");
            break;
        case OSD_ACT_TAPE_INSERT:
            if (menu_pc_tape(p, sys, m->files[a.file].name)) {
                snprintf(msg, sizeof(msg), "Cassette : %s (au début)", m->files[a.file].name);
                osd_menu_message(m, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : illisible", m->files[a.file].name);
                osd_menu_message(m, true, msg);
            }
            break;
        case OSD_ACT_TAPE_EJECT:
            telestrat_tape_insert(sys, 0, NULL, NULL);
            osd_menu_message(m, false, "Cassette éjectée");
            break;
        case OSD_ACT_LOAD_BUILTIN:
            if (a.file < ROM_BUILTINS && rom_pool_load_builtin(&p->pool, sys, a.target, &rom_builtins[a.file], &err)) {
                snprintf(msg, sizeof(msg), "Banque %d : %s — RESET conseillé", a.target, rom_builtins[a.file].label);
                osd_menu_message(m, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "Banque %d : %s", a.target, err);
                osd_menu_message(m, true, msg);
            }
            break;
        case OSD_ACT_RESUME: return true;
        default: break;
    }
    // Cartouche changée : BASIC 1.1 de nouveau patché si l'option est active
    if (p->tape_turbo) oric_turbo_apply_all(p->rom, p->pool.nslots, true);
    menu_pc_refresh(p, sys);
    return false;
}

// Touches d'un script : u d l r (flèches), e (Entrée), x (Échap), s (Suppr),
// h (début), z (fin), majuscule = saut à l'initiale. Retourne true si le menu
// est resté ouvert.
static bool menu_pc_script(menu_pc_t* p, telestrat_t* sys, const char* keys) {
    menu_pc_scan(p);
    menu_pc_refresh(p, sys);
    p->menu.page = OSD_PAGE_MAIN;
    p->menu.cursor = OSD_ITEM_RESUME;
    if (p->boot_pending) {
        p->boot_pending = false;
        osd_menu_open_boot(&p->menu);
    }
    for (const char* k = keys; *k; k++) {
        int key = *k;
        switch (*k) {
            case 'u': key = OSD_KEY_UP; break;
            case 'd': key = OSD_KEY_DOWN; break;
            case 'l': key = OSD_KEY_LEFT; break;
            case 'r': key = OSD_KEY_RIGHT; break;
            case 'e': key = OSD_KEY_ENTER; break;
            case 'x': key = OSD_KEY_ESC; break;
            case 's': key = OSD_KEY_DEL; break;
            case 'h': key = OSD_KEY_HOME; break;
            case 'z': key = OSD_KEY_END; break;
            default: break;
        }
        const osd_action_t a = osd_menu_key(&p->menu, key);
        p->menu.message[0] = 0;
        const bool close = menu_pc_action(p, sys, a);
        if (p->menu.message[0]) fprintf(stderr, "menu : %s\n", p->menu.message);
        if (close) return false;
    }
    return true;
}

static void menu_pc_ppm(menu_pc_t* p, const char* path) {
    osd_menu_draw(&p->menu, &p->surf);
    const size_t n = strlen(path);
    if (n > 4 && !strcmp(path + n - 4, ".txt")) {
        // Texte du menu (Latin-1 -> UTF-8 ; icônes et filets : espaces)
        FILE* t = fopen(path, "w");
        if (!t) {
            perror(path);
            return;
        }
        for (int row = 0; row < OSD_ROWS; row++) {
            for (int col = 0; col < OSD_COLS; col++) {
                const uint8_t c = p->surf.ch[row][col];
                if (c >= 0xA0) fputc(0xC0 | c >> 6, t), fputc(0x80 | (c & 0x3F), t);
                else fputc(c >= 0x20 && c < 0x7F ? c : ' ', t);
            }
            fputc('\n', t);
        }
        fclose(t);
        return;
    }
    FILE* f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", OSD_WIDTH, OSD_LINES * 2);
    uint32_t r[OSD_WIDTH / 32], g[OSD_WIDTH / 32], b[OSD_WIDTH / 32];
    static uint8_t rgb[OSD_WIDTH * 3];
    for (int line = 0; line < OSD_LINES; line++) {
        osd_render_line(&p->surf, line, r, g, b);
        for (int x = 0; x < OSD_WIDTH; x++) {
            rgb[3 * x] = (r[x >> 5] >> (x & 31) & 1) ? 255 : 0;
            rgb[3 * x + 1] = (g[x >> 5] >> (x & 31) & 1) ? 255 : 0;
            rgb[3 * x + 2] = (b[x >> 5] >> (x & 31) & 1) ? 255 : 0;
        }
        fwrite(rgb, 1, sizeof(rgb), f);
        fwrite(rgb, 1, sizeof(rgb), f);
    }
    fclose(f);
}

static void menu_pc_finish(menu_pc_t* p, telestrat_t* sys) {
    for (int d = 0; d < 4; d++) _menu_pc_flush(p, sys, d);
}
