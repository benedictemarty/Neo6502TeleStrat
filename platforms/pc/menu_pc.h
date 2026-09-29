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

#define MENU_PC_ROM_SLOTS 2

typedef struct {
    const char* dir;
    osd_menu_t menu;
    osd_surface_t surf;
    uint8_t* disk[4];
    size_t disk_size[4];
    char disk_name[4][OSD_NAME_LEN];
    uint8_t rom[MENU_PC_ROM_SLOTS][OSD_BANK_BYTES];
    int rom_bank[MENU_PC_ROM_SLOTS];  // banque tenue par l'emplacement, -1 : libre
    char rom_name[8][OSD_NAME_LEN];   // cartouche de la clé en banque b ("" : origine)
} menu_pc_t;

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
        bool dsk = _menu_pc_ext(e->d_name, ".dsk"), rom = _menu_pc_ext(e->d_name, ".rom");
        if ((!dsk && !rom) || strlen(e->d_name) >= OSD_NAME_LEN) continue;
        char path[512];
        struct stat st;
        _menu_pc_path(p, e->d_name, path, sizeof(path));
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        osd_file_t* f = &m->files[m->nfiles++];
        snprintf(f->name, sizeof(f->name), "%s", e->d_name);
        f->size = (uint32_t)st.st_size;
        f->kind = dsk ? OSD_FILE_DSK : OSD_FILE_ROM;
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
    wd1793_eject(&sys->fdc.wd, d);
    _menu_pc_flush(p, sys, d);
    free(p->disk[d]);
    p->disk[d] = NULL;
    p->disk_name[d][0] = 0;
}

static void menu_pc_restore(menu_pc_t* p, telestrat_t* sys, int bank) {
    telestrat_restore_bank(sys, bank);
    for (int s = 0; s < MENU_PC_ROM_SLOTS; s++)
        if (p->rom_bank[s] == bank) p->rom_bank[s] = -1;
    p->rom_name[bank][0] = 0;
}

// Cartouche de la clé en banque ; message d'erreur dans err
static bool menu_pc_load_rom(menu_pc_t* p, telestrat_t* sys, int bank, const char* name, const char** err) {
    size_t size = 0;
    uint8_t* data = _menu_pc_read(p, name, &size);
    if (!data) {
        *err = "fichier illisible";
        return false;
    }
    int slot = -1;
    for (int s = 0; s < MENU_PC_ROM_SLOTS; s++)
        if (p->rom_bank[s] == bank) slot = s;
    for (int s = 0; s < MENU_PC_ROM_SLOTS && slot < 0; s++)
        if (p->rom_bank[s] < 0) slot = s;
    bool ok = false;
    if (!osd_rom_size_ok(size)) *err = "taille invalide (16, 8, 4, 2 ou 1 Ko)";
    else if (slot < 0) *err = "plus de place : rendre une banque à son contenu d'origine";
    else ok = true;
    if (ok) {
        telestrat_set_bank_rom(sys, bank, NULL);  // l'emplacement change
        osd_rom_fill(p->rom[slot], data, size);
        p->rom_bank[slot] = bank;
        telestrat_set_bank_rom(sys, bank, p->rom[slot]);
        snprintf(p->rom_name[bank], sizeof(p->rom_name[bank]), "%s", name);
    }
    free(data);
    return ok;
}

static const char* menu_pc_bank_label(const telestrat_t* sys, int b) {
    const uint8_t* r = sys->bank_rd_orig[b];
    if (r == telestrat_telemon24) return "TELEMON 2.4";
    if (r == telestrat_hyperbas) return "HYPER-BASIC";
    if (r == telestrat_teleass) return "TELE-ASS";
    if (r == telestrat_telematic) return "TELEMATIC";
    return sys->bank_type_orig[b] == TELESTRAT_BANK_RAM ? (b == 0 ? "RAM interne" : "RAM 16 Ko") : "";
}

// État du menu d'après le système
static void menu_pc_refresh(menu_pc_t* p, telestrat_t* sys) {
    osd_menu_t* m = &p->menu;
    for (int d = 0; d < 4; d++) {
        snprintf(m->drive[d], sizeof(m->drive[d]), "%s", p->disk[d] ? p->disk_name[d] : "");
        m->drive_ro[d] = p->disk[d] && sys->fdc.wd.disk[d].write_protect;
    }
    for (int b = 0; b < 8; b++) {
        if (p->rom_name[b][0]) {
            snprintf(m->bank[b], sizeof(m->bank[b]), "%s", p->rom_name[b]);
            m->bank_kind[b] = OSD_BANK_ROM_USB;
            continue;
        }
        snprintf(m->bank[b], sizeof(m->bank[b]), "%s", menu_pc_bank_label(sys, b));
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

// TELESTRA.CFG au démarrage : lecteurs et cartouches
static void menu_pc_init(menu_pc_t* p, telestrat_t* sys, const char* dir, const char* version) {
    memset(p, 0, sizeof(*p));
    p->dir = dir;
    for (int s = 0; s < MENU_PC_ROM_SLOTS; s++) p->rom_bank[s] = -1;
    osd_menu_init(&p->menu);
    p->menu.version = version;
    menu_pc_scan(p);
    char* cfg = _menu_pc_read_cfg(p);
    for (char* line = cfg ? strtok(cfg, "\r\n") : NULL; line; line = strtok(NULL, "\r\n")) {
        const char* v;
        for (int d = 0; d < 4; d++) {
            const char key[2] = {(char)('a' + d), 0};
            if ((v = osd_config_value(line, key)) && !menu_pc_insert(p, sys, d, v))
                fprintf(stderr, "TELESTRA.CFG : %s illisible\n", v);
        }
        for (int b = 1; b < 8; b++) {
            char key[8];
            const char* err = "";
            snprintf(key, sizeof(key), "bank%d", b);
            if ((v = osd_config_value(line, key)) && !menu_pc_load_rom(p, sys, b, v, &err))
                fprintf(stderr, "TELESTRA.CFG : %s : %s\n", v, err);
        }
    }
    free(cfg);
}

static void menu_pc_save(menu_pc_t* p) {
    char* old = _menu_pc_read_cfg(p);
    const char* drives[4];
    const char* banks[8];
    for (int d = 0; d < 4; d++) drives[d] = p->disk[d] ? p->disk_name[d] : NULL;
    for (int b = 0; b < 8; b++) banks[b] = p->rom_name[b];
    static char out[4096];
    size_t n = osd_config_merge(old, drives, banks, out, sizeof(out));
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
        case OSD_ACT_RESUME: return true;
        default: break;
    }
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
