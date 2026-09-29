#pragma once

// osd_menu.h — menu du Telestrat (OSD) : disquettes, cartouches, clé USB
//
// Page principale : lecteurs A à D, cassette, banques 7 à 1 (cartouches),
// boutons Redémarrer / Enregistrer / Reprendre. Entrée sur un lecteur, la
// cassette ou une banque : sélecteur de fichiers de la clé (.dsk, .tap, .rom ;
// pour une banque, les ROM intégrées d'abord), avec défilement et saut à
// l'initiale tapée. osd_tape_banner dessine le bandeau de la cassette. La plate-forme remplit l'état (lecteurs, banques, clé,
// fichiers, message) et exécute les actions rendues par osd_menu_key().
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

#include <stdio.h>

#include "osd/osd.h"

#ifndef OSD_MENU_FILES
#define OSD_MENU_FILES 64  // fichiers de la clé listés (32 avec la RAM 64 Ko : place)
#endif
#define OSD_NAME_LEN   48

enum { OSD_FILE_DSK = 0, OSD_FILE_ROM = 1, OSD_FILE_TAP = 2 };

#define OSD_BUILTINS 4  // ROM intégrées proposées pour une banque

// Contenu d'une banque
enum { OSD_BANK_EMPTY = 0, OSD_BANK_RAM, OSD_BANK_ROM, OSD_BANK_ROM_USB };

enum { OSD_PAGE_MAIN = 0, OSD_PAGE_BROWSE };

// Touches du menu (en plus des caractères imprimables, pour le saut)
enum {
    OSD_KEY_UP = 0x100, OSD_KEY_DOWN, OSD_KEY_LEFT, OSD_KEY_RIGHT, OSD_KEY_ENTER, OSD_KEY_ESC, OSD_KEY_DEL,
    OSD_KEY_PGUP, OSD_KEY_PGDN, OSD_KEY_HOME, OSD_KEY_END,
};

enum {
    OSD_ACT_NONE = 0,
    OSD_ACT_INSERT,      // target = lecteur 0-3, file = index
    OSD_ACT_EJECT,       // target = lecteur
    OSD_ACT_LOAD_ROM,    // target = banque 1-7, file = index
    OSD_ACT_RESTORE,     // target = banque : contenu d'origine
    OSD_ACT_RESET,
    OSD_ACT_SAVE,        // TELESTRA.CFG
    OSD_ACT_RESUME,
    OSD_ACT_TAPE_INSERT,  // file = index (la cassette en place : rembobinée)
    OSD_ACT_TAPE_EJECT,
    OSD_ACT_LOAD_BUILTIN, // target = banque, file = ROM intégrée
};

typedef struct {
    int type;
    int target;
    int file;
} osd_action_t;

typedef struct {
    char name[OSD_NAME_LEN];
    uint32_t size;
    uint8_t kind;
} osd_file_t;

// Éléments de la page principale
#define OSD_ITEM_DRIVE0  0   // 0-3 : lecteurs A-D
#define OSD_ITEM_TAPE    4   // cassette
#define OSD_ITEM_BANK7   5   // 5-11 : banques 7 à 1
#define OSD_ITEM_RESET   12
#define OSD_ITEM_SAVE    13
#define OSD_ITEM_RESUME  14
#define OSD_ITEMS        15

typedef struct {
    // --- Rempli par la plate-forme ---
    char drive[4][OSD_NAME_LEN];  // "" : vide
    bool drive_ro[4];
    char bank[8][OSD_NAME_LEN];   // libellé du contenu
    uint8_t bank_kind[8];
    char tape[OSD_NAME_LEN];      // cassette ("" : aucune)
    int tape_percent;
    bool tape_motor;
    const char* builtin[OSD_BUILTINS];  // ROM intégrées proposées (NULL : fin)
    bool usb_present;
    char usb_label[OSD_NAME_LEN];
    osd_file_t files[OSD_MENU_FILES];
    int nfiles;
    char message[96];             // dernier résultat d'action
    bool message_error;
    const char* version;
    // --- Navigation ---
    int page;
    int cursor;
    int browse_target;  // élément de la page principale qui a ouvert le sélecteur
    int browse_cursor;  // 0 = « vider / éjecter », puis la liste
    // Liste du sélecteur : index de fichier (>= 0) ou ROM intégrée k (-2 - k)
    int browse_scroll;
    int browse_list[OSD_MENU_FILES];
    int browse_count;
} osd_menu_t;

static inline void osd_menu_init(osd_menu_t* m) {
    memset(m, 0, sizeof(*m));
    m->version = "";
    m->cursor = OSD_ITEM_RESUME;
}

static inline int _osd_item_bank(int item) { return 7 - (item - OSD_ITEM_BANK7); }

static inline bool _osd_item_is_drive(int item) { return item >= OSD_ITEM_DRIVE0 && item < OSD_ITEM_TAPE; }

static inline bool _osd_item_is_bank(int item) { return item >= OSD_ITEM_BANK7 && item < OSD_ITEM_RESET; }

// Ouverture du sélecteur pour un lecteur ou une banque
// Nom d'un élément de la liste du sélecteur
static inline const char* _osd_entry_name(const osd_menu_t* m, int entry) {
    return entry >= 0 ? m->files[entry].name : m->builtin[-2 - entry];
}

static inline void _osd_open_browser(osd_menu_t* m, int item) {
    const uint8_t kind = _osd_item_is_drive(item) ? OSD_FILE_DSK : item == OSD_ITEM_TAPE ? OSD_FILE_TAP : OSD_FILE_ROM;
    m->browse_count = 0;
    if (kind == OSD_FILE_ROM)
        for (int k = 0; k < OSD_BUILTINS && m->builtin[k]; k++) m->browse_list[m->browse_count++] = -2 - k;
    for (int i = 0; i < m->nfiles && m->browse_count < OSD_MENU_FILES; i++)
        if (m->files[i].kind == kind) m->browse_list[m->browse_count++] = i;
    m->browse_target = item;
    m->browse_cursor = 0;
    m->browse_scroll = 0;
    // Curseur sur l'image, la cassette ou la cartouche en place
    const char* cur = _osd_item_is_drive(item) ? m->drive[item] : item == OSD_ITEM_TAPE ? m->tape : m->bank[_osd_item_bank(item)];
    for (int k = 0; k < m->browse_count; k++)
        if (!strcmp(_osd_entry_name(m, m->browse_list[k]), cur)) m->browse_cursor = k + 1;
    m->page = OSD_PAGE_BROWSE;
}

#define OSD_BROWSE_VISIBLE 18

static inline void _osd_browse_clamp(osd_menu_t* m) {
    const int n = m->browse_count + 1;
    if (m->browse_cursor < 0) m->browse_cursor = 0;
    if (m->browse_cursor >= n) m->browse_cursor = n - 1;
    if (m->browse_cursor < m->browse_scroll) m->browse_scroll = m->browse_cursor;
    if (m->browse_cursor >= m->browse_scroll + OSD_BROWSE_VISIBLE)
        m->browse_scroll = m->browse_cursor - OSD_BROWSE_VISIBLE + 1;
}

static inline int _osd_upper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static inline osd_action_t osd_menu_key(osd_menu_t* m, int key) {
    osd_action_t a = {OSD_ACT_NONE, 0, -1};
    if (m->page == OSD_PAGE_BROWSE) {
        const int n = m->browse_count + 1;
        switch (key) {
            case OSD_KEY_UP: m->browse_cursor = (m->browse_cursor + n - 1) % n; break;
            case OSD_KEY_DOWN: m->browse_cursor = (m->browse_cursor + 1) % n; break;
            case OSD_KEY_PGUP: m->browse_cursor -= OSD_BROWSE_VISIBLE; break;
            case OSD_KEY_PGDN: m->browse_cursor += OSD_BROWSE_VISIBLE; break;
            case OSD_KEY_HOME: m->browse_cursor = 0; break;
            case OSD_KEY_END: m->browse_cursor = n - 1; break;
            case OSD_KEY_ESC:
            case OSD_KEY_LEFT: m->page = OSD_PAGE_MAIN; break;
            case OSD_KEY_ENTER: {
                const int item = m->browse_target;
                const int file = m->browse_cursor ? m->browse_list[m->browse_cursor - 1] : -1;
                const bool none = m->browse_cursor == 0;
                if (_osd_item_is_drive(item)) {
                    a.target = item - OSD_ITEM_DRIVE0;
                    a.type = none ? OSD_ACT_EJECT : OSD_ACT_INSERT;
                } else if (item == OSD_ITEM_TAPE) {
                    a.type = none ? OSD_ACT_TAPE_EJECT : OSD_ACT_TAPE_INSERT;
                } else {
                    a.target = _osd_item_bank(item);
                    a.type = none ? OSD_ACT_RESTORE : file >= 0 ? OSD_ACT_LOAD_ROM : OSD_ACT_LOAD_BUILTIN;
                }
                a.file = none ? -1 : file >= 0 ? file : -2 - file;
                m->page = OSD_PAGE_MAIN;
                break;
            }
            default:
                // Saut au fichier suivant qui commence par la lettre tapée
                if (key > ' ' && key < 0x100) {
                    for (int k = 1; k <= m->browse_count; k++) {
                        const int idx = (m->browse_cursor - 1 + k) % m->browse_count;
                        if (_osd_upper((uint8_t)_osd_entry_name(m, m->browse_list[idx])[0]) == _osd_upper(key)) {
                            m->browse_cursor = idx + 1;
                            break;
                        }
                    }
                }
                break;
        }
        _osd_browse_clamp(m);
        return a;
    }
    int c = m->cursor;
    switch (key) {
        case OSD_KEY_UP: c = (c + OSD_ITEMS - 1) % OSD_ITEMS; break;
        case OSD_KEY_DOWN: c = (c + 1) % OSD_ITEMS; break;
        case OSD_KEY_LEFT:
            if (_osd_item_is_bank(c)) c = OSD_ITEM_DRIVE0 + (c - OSD_ITEM_BANK7 < 5 ? c - OSD_ITEM_BANK7 : 4);
            else if (c > OSD_ITEM_RESET) c--;
            break;
        case OSD_KEY_RIGHT:
            if (c <= OSD_ITEM_TAPE) c = OSD_ITEM_BANK7 + (c - OSD_ITEM_DRIVE0);
            else if (c >= OSD_ITEM_RESET && c < OSD_ITEM_RESUME) c++;
            break;
        case OSD_KEY_HOME: c = 0; break;
        case OSD_KEY_END: c = OSD_ITEMS - 1; break;
        case OSD_KEY_ESC: a.type = OSD_ACT_RESUME; break;
        case OSD_KEY_DEL:
            if (_osd_item_is_drive(c)) {
                a.type = OSD_ACT_EJECT;
                a.target = c - OSD_ITEM_DRIVE0;
            } else if (c == OSD_ITEM_TAPE) {
                a.type = OSD_ACT_TAPE_EJECT;
            } else if (_osd_item_is_bank(c)) {
                a.type = OSD_ACT_RESTORE;
                a.target = _osd_item_bank(c);
            }
            break;
        case OSD_KEY_ENTER:
            if (c <= OSD_ITEM_TAPE || _osd_item_is_bank(c)) _osd_open_browser(m, c);
            else if (c == OSD_ITEM_RESET) a.type = OSD_ACT_RESET;
            else if (c == OSD_ITEM_SAVE) a.type = OSD_ACT_SAVE;
            else a.type = OSD_ACT_RESUME;
            break;
        default: break;
    }
    m->cursor = c;
    return a;
}

static inline void osd_menu_message(osd_menu_t* m, bool error, const char* text) {
    snprintf(m->message, sizeof(m->message), "%s", text);
    m->message_error = error;
}

/*-- Dessin ------------------------------------------------------------------*/

#define OSD_BG        OSD_ATTR(OSD_WHITE, OSD_BLACK)
#define OSD_PANEL     OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER)
#define OSD_PANEL_DIM OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER)
#define OSD_PANEL_ACC OSD_ATTR(OSD_YELLOW, OSD_BLUE | OSD_DITHER)
#define OSD_PANEL_OK  OSD_ATTR(OSD_GREEN, OSD_BLUE | OSD_DITHER)
#define OSD_PANEL_ERR OSD_ATTR(OSD_RED, OSD_BLUE | OSD_DITHER)
#define OSD_EDGE      OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER)
#define OSD_SEL       OSD_ATTR(OSD_WHITE, OSD_BLUE)
#define OSD_SEL_ACC   OSD_ATTR(OSD_YELLOW, OSD_BLUE)
#define OSD_SEL_DIM   OSD_ATTR(OSD_CYAN, OSD_BLUE)

// Panneau : fond tramé et cadre arrondi titré
static inline void _osd_panel(osd_surface_t* s, int row, int col, int rows, int cols, uint8_t icon, const char* title) {
    osd_fill(s, row, col, rows, cols, OSD_PANEL);
    osd_frame(s, row, col, rows, cols, OSD_EDGE, NULL, 0);
    int c = col + 2;
    osd_putc(s, row, c++, ' ', OSD_EDGE);
    if (icon) {
        osd_putc(s, row, c++, icon, OSD_PANEL_ACC);
        osd_putc(s, row, c++, (uint8_t)(icon + 1), OSD_PANEL_ACC);
        osd_putc(s, row, c++, ' ', OSD_EDGE);
    }
    c += osd_puts(s, row, c, title, OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER), -1);
    osd_putc(s, row, c, ' ', OSD_EDGE);
}

// Ligne d'élément : barre de sélection pleine, marqueur ▶
static inline void _osd_item_bar(osd_surface_t* s, int row, int col, int cols, bool selected) {
    osd_fill(s, row, col, 1, cols, selected ? OSD_SEL : OSD_PANEL);
    if (selected) osd_putc(s, row, col, OSD_TRI_R, OSD_SEL_ACC);
}

static inline void _osd_size(char* buf, size_t n, uint32_t size) {
    if (size >= 1024 * 1024) snprintf(buf, n, "%u,%u Mo", (unsigned)(size >> 20), (unsigned)((size % (1u << 20)) * 10 >> 20));
    else snprintf(buf, n, "%u Ko", (unsigned)((size + 1023) >> 10));
}

// Bouton (pilule) de la rangée d'actions
static inline void _osd_button(osd_surface_t* s, int row, int col, int cols, const char* label, bool selected) {
    const uint8_t attr = selected ? OSD_ATTR(OSD_BLACK, OSD_CYAN) : OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER);
    osd_fill(s, row, col, 1, cols, attr);
    const int len = osd_strlen(label);
    osd_puts(s, row, col + (cols - len) / 2, label, attr, -1);
}

static inline void osd_menu_draw(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    osd_clear(s, OSD_BG);

    // Bandeau
    osd_fill(s, 0, 0, 3, OSD_COLS, OSD_ATTR(OSD_WHITE, OSD_BLUE));
    osd_puts_big(s, 1, 3, "TELESTRAT", OSD_ATTR(OSD_WHITE, OSD_BLUE));
    osd_puts(s, 1, 24, "Oric", OSD_ATTR(OSD_YELLOW, OSD_BLUE), -1);
    snprintf(buf, sizeof(buf), "Neo6502  %s", m->version);
    osd_puts(s, 1, OSD_COLS - 3 - osd_strlen(buf), buf, OSD_ATTR(OSD_CYAN, OSD_BLUE), -1);
    for (int c = 0; c < OSD_COLS; c++) osd_putc(s, 3, c, OSD_HLINE, OSD_ATTR(OSD_CYAN, OSD_BLACK));

    // Disquettes
    _osd_panel(s, 5, 2, 12, 56, OSD_FLOP_L, "Disquettes");
    for (int d = 0; d < 4; d++) {
        const int row = 7 + 2 * d;
        const bool sel = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_DRIVE0 + d;
        _osd_item_bar(s, row, 4, 52, sel);
        const uint8_t base = sel ? OSD_SEL : OSD_PANEL, dim = sel ? OSD_SEL_DIM : OSD_PANEL_DIM;
        snprintf(buf, sizeof(buf), "%c", 'A' + d);
        osd_puts(s, row, 6, buf, sel ? OSD_SEL_ACC : OSD_PANEL_ACC, -1);
        if (m->drive[d][0]) {
            osd_puts(s, row, 9, m->drive[d], base, 32);
            if (m->drive_ro[d]) {
                osd_putc(s, row, 44, OSD_LOCK, sel ? OSD_ATTR(OSD_RED, OSD_BLUE) : OSD_PANEL_ERR);
                osd_puts(s, row, 46, "protégée", dim, -1);
            } else {
                osd_putc(s, row, 44, OSD_DOT, sel ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK);
                osd_puts(s, row, 46, "écriture", dim, -1);
            }
        } else {
            osd_puts(s, row, 9, "— vide —", dim, -1);
        }
    }
    {
        const int row = 15;
        const bool sel = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_TAPE;
        _osd_item_bar(s, row, 4, 52, sel);
        const uint8_t base = sel ? OSD_SEL : OSD_PANEL, dim = sel ? OSD_SEL_DIM : OSD_PANEL_DIM;
        const uint8_t acc = sel ? OSD_SEL_ACC : OSD_PANEL_ACC;
        osd_putc(s, row, 6, OSD_TAPE_L, acc);
        osd_putc(s, row, 7, OSD_TAPE_R, acc);
        if (m->tape[0]) {
            osd_puts(s, row, 9, m->tape, base, 30);
            osd_putc(s, row, 41, m->tape_motor ? OSD_TRI_R : OSD_FULL, m->tape_motor ? acc : dim);
            snprintf(buf, sizeof(buf), "%3d %%", m->tape_percent);
            osd_puts(s, row, 43, buf, dim, -1);
            // Barre de position : 6 cellules
            for (int i = 0; i < 6; i++) osd_putc(s, row, 49 + i, i * 100 / 6 < m->tape_percent ? OSD_FULL : OSD_SHADE, dim);
        } else {
            osd_puts(s, row, 9, "— pas de cassette —", dim, -1);
        }
    }

    // Cartouches
    _osd_panel(s, 5, 61, 18, 57, OSD_CART_L, "Cartouches");
    static const char* const origin[4] = {"", "RAM", "ROM", "clé USB"};
    for (int i = 0; i < 7; i++) {
        const int bank = 7 - i, row = 7 + 2 * i;
        const bool sel = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_BANK7 + i;
        _osd_item_bar(s, row, 63, 53, sel);
        const uint8_t base = sel ? OSD_SEL : OSD_PANEL, dim = sel ? OSD_SEL_DIM : OSD_PANEL_DIM;
        snprintf(buf, sizeof(buf), "Banque %d", bank);
        osd_puts(s, row, 65, buf, sel ? OSD_SEL_ACC : OSD_PANEL_ACC, -1);
        if (m->bank_kind[bank] == OSD_BANK_EMPTY) {
            osd_puts(s, row, 76, "— vide —", dim, -1);
        } else {
            osd_puts(s, row, 76, m->bank[bank], base, 28);
            osd_puts(s, row, 106, origin[m->bank_kind[bank]], dim, -1);
        }
    }

    // Clé USB
    _osd_panel(s, 18, 2, 5, 56, OSD_USB_L, "Clé USB");
    if (m->usb_present) {
        int ndsk = 0, nrom = 0, ntap = 0;
        for (int i = 0; i < m->nfiles; i++) {
            if (m->files[i].kind == OSD_FILE_DSK) ndsk++;
            else if (m->files[i].kind == OSD_FILE_TAP) ntap++;
            else nrom++;
        }
        osd_puts(s, 19, 5, m->usb_label[0] ? m->usb_label : "Clé montée", OSD_PANEL, 50);
        snprintf(buf, sizeof(buf), "%d .dsk   %d .tap   %d .rom", ndsk, ntap, nrom);
        osd_puts(s, 20, 5, buf, OSD_PANEL_DIM, -1);
    } else {
        osd_puts(s, 19, 5, "Aucune clé", OSD_PANEL, -1);
        osd_puts(s, 20, 5, "Brancher une clé FAT : .dsk, .tap, .rom à la racine", OSD_PANEL_DIM, -1);
    }

    // Actions
    const int btn_row = 25;
    const char* const labels[3] = {"Redémarrer (RESET)", "Enregistrer la configuration", "Reprendre"};
    const int cols[3] = {2, 40, 82}, widths[3] = {34, 38, 36};
    for (int i = 0; i < 3; i++)
        _osd_button(s, btn_row, cols[i], widths[i], labels[i], m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_RESET + i);

    // Message
    if (m->message[0]) {
        osd_putc(s, 28, 3, m->message_error ? OSD_CROSS : OSD_CHECK,
                 OSD_ATTR(m->message_error ? OSD_RED : OSD_GREEN, OSD_BLACK));
        osd_puts(s, 28, 5, m->message, OSD_ATTR(m->message_error ? OSD_RED : OSD_YELLOW, OSD_BLACK), -1);
    }

    // Pied : aide des touches
    osd_fill(s, 32, 0, 2, OSD_COLS, OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER));
    const uint8_t key = OSD_ATTR(OSD_BLACK, OSD_CYAN), txt = OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER);
    const char* const help_main[][2] = {{" Flèches ", "choisir"}, {" Entrée ", "ouvrir"}, {" Suppr ", "éjecter / d'origine"},
                                       {" Échap ", "reprendre"}};
    const char* const help_browse[][2] = {{" Flèches ", "choisir"}, {" Entrée ", "valider"}, {" Lettre ", "aller à"},
                                         {" Échap ", "retour"}};
    const char* const(*help)[2] = m->page == OSD_PAGE_MAIN ? help_main : help_browse;
    int c = 3;
    for (int i = 0; i < 4; i++) {
        c += osd_puts(s, 32, c, help[i][0], key, -1) + 1;
        c += osd_puts(s, 32, c, help[i][1], txt, -1) + 4;
    }
    osd_puts(s, 32, OSD_COLS - 22, "F1 : ouvrir ce menu", OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER), -1);

    if (m->page != OSD_PAGE_BROWSE) return;

    // Sélecteur de fichiers, par-dessus
    const int item = m->browse_target;
    const bool drive = _osd_item_is_drive(item), tape = item == OSD_ITEM_TAPE;
    if (drive) snprintf(buf, sizeof(buf), "Disquette pour le lecteur %c", 'A' + item);
    else if (tape) snprintf(buf, sizeof(buf), "Cassette (la même : rembobinée)");
    else snprintf(buf, sizeof(buf), "Cartouche pour la banque %d", _osd_item_bank(item));
    const int top = 6, left = 22, width = 76, height = OSD_BROWSE_VISIBLE + 4;
    osd_fill(s, top + 1, left + 2, height, width, OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER));  // ombre
    osd_fill(s, top, left, height, width, OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_frame(s, top, left, height, width, OSD_ATTR(OSD_YELLOW, OSD_BLACK), NULL, 0);
    osd_putc(s, top, left + 2, ' ', OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    const uint8_t icon = drive ? OSD_FLOP_L : tape ? OSD_TAPE_L : OSD_CART_L;
    osd_putc(s, top, left + 3, icon, OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    osd_putc(s, top, left + 4, (uint8_t)(icon + 1), OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    const int tl = osd_puts(s, top, left + 6, buf, OSD_ATTR(OSD_WHITE, OSD_BLACK), -1);
    osd_putc(s, top, left + 6 + tl, ' ', OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    const int n = m->browse_count + 1;
    for (int k = 0; k < OSD_BROWSE_VISIBLE && m->browse_scroll + k < n; k++) {
        const int idx = m->browse_scroll + k, row = top + 2 + k;
        const bool sel = idx == m->browse_cursor;
        const uint8_t base = sel ? OSD_ATTR(OSD_BLACK, OSD_CYAN) : OSD_ATTR(OSD_WHITE, OSD_BLACK);
        const uint8_t dim = sel ? OSD_ATTR(OSD_BLUE, OSD_CYAN) : OSD_ATTR(OSD_CYAN, OSD_BLACK);
        osd_fill(s, row, left + 2, 1, width - 5, base);
        if (idx == 0) {
            osd_puts(s, row, left + 4,
                     drive ? "Éjecter la disquette" : tape ? "Éjecter la cassette" : "Contenu d'origine de la banque", dim,
                     -1);
            continue;
        }
        const int entry = m->browse_list[idx - 1];
        if (entry < 0) {  // ROM intégrée
            osd_puts(s, row, left + 4, m->builtin[-2 - entry], base, 50);
            osd_puts(s, row, left + width - 13, "intégrée", dim, -1);
            continue;
        }
        const osd_file_t* f = &m->files[entry];
        osd_puts(s, row, left + 4, f->name, base, 50);
        char size[16];
        _osd_size(size, sizeof(size), f->size);
        osd_puts(s, row, left + width - 5 - osd_strlen(size), size, dim, -1);
        // Image déjà dans un autre lecteur (la choisir est refusé)
        for (int d = 0; drive && d < 4; d++) {
            if (d == item - OSD_ITEM_DRIVE0 || strcmp(m->drive[d], f->name)) continue;
            char tag[8];
            snprintf(tag, sizeof(tag), "en %c", 'A' + d);
            osd_puts(s, row, left + width - 18, tag, sel ? OSD_ATTR(OSD_RED, OSD_CYAN) : OSD_ATTR(OSD_YELLOW, OSD_BLACK), -1);
        }
    }
    if (m->browse_count == 0)
        osd_puts(s, top + 4, left + 4,
                 drive ? "Aucune image .dsk sur la clé" : tape ? "Aucune cassette .tap sur la clé" : "Aucune image .rom sur la clé",
                 OSD_ATTR(OSD_RED, OSD_BLACK), -1);
    // Barre de défilement
    if (n > OSD_BROWSE_VISIBLE) {
        const int bar = left + width - 2;
        for (int k = 0; k < OSD_BROWSE_VISIBLE; k++) osd_putc(s, top + 2 + k, bar, OSD_SHADE, OSD_ATTR(OSD_BLUE, OSD_BLACK));
        const int thumb = m->browse_scroll * (OSD_BROWSE_VISIBLE - 1) / (n - OSD_BROWSE_VISIBLE);
        osd_putc(s, top + 2 + thumb, bar, OSD_FULL, OSD_ATTR(OSD_CYAN, OSD_BLACK));
    }
}

// Bandeau de la cassette, incrusté sous l'image du Telestrat pendant que le
// moteur tourne : icône, nom, barre de position, pour cent
static inline void osd_tape_banner(osd_row_t* r, const char* name, int percent) {
    const uint8_t base = OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER);
    osd_row_clear(r, base);
    const uint8_t acc = OSD_ATTR(OSD_YELLOW, OSD_BLUE | OSD_DITHER);
    r->ch[16] = OSD_TRI_R;
    r->attr[16] = acc;
    r->ch[18] = OSD_TAPE_L;
    r->ch[19] = OSD_TAPE_R;
    r->attr[18] = r->attr[19] = acc;
    osd_row_puts(r, 21, "Lecture", acc);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.34s", name);
    osd_row_puts(r, 30, buf, base);
    const int bar = 66, cells = 30;
    for (int i = 0; i < cells; i++) {
        r->ch[bar + i] = i * 100 / cells < percent ? OSD_FULL : OSD_SHADE;
        r->attr[bar + i] = OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER);
    }
    snprintf(buf, sizeof(buf), "%3d %%", percent);
    osd_row_puts(r, bar + cells + 2, buf, acc);
}
