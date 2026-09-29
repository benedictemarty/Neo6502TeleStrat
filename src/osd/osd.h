#pragma once

// osd.h — surface du menu (OSD) : grille de 120 x 34 cellules de 8 x 8
//
// Rendue à la résolution de sortie du Neo6502 (960 pixels, 272 lignes de
// tampon affichées deux fois : 960 x 544), en plein écran à la place de
// l'image du Telestrat. Chaque cellule : un caractère (osd_font.h), une
// couleur d'encre, une couleur de fond, et un fond « tramé » (un pixel sur
// deux, lignes alternées) qui donne des demi-teintes (bleu nuit…) avec les 8
// couleurs pures des trois plans de 1 bit. Grandes lettres : un caractère sur
// deux cellules, chaque pixel doublé en largeur.
//
// osd_render_line() écrit une ligne de tampon dans les trois plans (rouge,
// vert, bleu ; bit 0 d'un mot = pixel de gauche), comme telestrat_video_line.
// Code en C pur (testé sur PC par tests/test_telestrat.c, aperçu par le banc).
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "osd/osd_font.h"

#ifndef OSD_RAM
#define OSD_RAM
#endif

#define OSD_COLS  120
#define OSD_ROWS  34
#define OSD_WIDTH (OSD_COLS * 8)   // 960 pixels
#define OSD_LINES (OSD_ROWS * 8)   // 272 lignes de tampon

// Couleurs (bit 0 rouge, 1 vert, 2 bleu), comme l'Oric
enum {
    OSD_BLACK = 0, OSD_RED, OSD_GREEN, OSD_YELLOW, OSD_BLUE, OSD_MAGENTA, OSD_CYAN, OSD_WHITE,
};
#define OSD_DITHER 0x08  // fond tramé : la couleur de fond un pixel sur deux

// Attribut : bits 0-2 encre, bits 4-6 fond, bit 7 fond tramé
#define OSD_ATTR(ink, paper) ((uint8_t)((ink) | (((paper) & 7) << 4) | (((paper) & OSD_DITHER) ? 0x80 : 0)))

// Cellule d'une grande lettre (pixels doublés en largeur)
#define OSD_BIG_LEFT  1
#define OSD_BIG_RIGHT 2

typedef struct {
    uint8_t ch[OSD_ROWS][OSD_COLS];
    uint8_t attr[OSD_ROWS][OSD_COLS];
    uint8_t big[OSD_ROWS][OSD_COLS];
} osd_surface_t;

// Octet de glyphe -> 16 pixels (grandes lettres)
static inline uint16_t _osd_widen(uint8_t b) {
    uint16_t w = 0;
    for (int i = 0; i < 8; i++)
        if (b >> i & 1) w |= (uint16_t)(3u << (2 * i));
    return w;
}

static inline void osd_clear(osd_surface_t* s, uint8_t attr) {
    memset(s->ch, ' ', sizeof(s->ch));
    memset(s->attr, attr, sizeof(s->attr));
    memset(s->big, 0, sizeof(s->big));
}

// Rectangle d'attribut (fond d'un panneau), caractères effacés
static inline void osd_fill(osd_surface_t* s, int row, int col, int rows, int cols, uint8_t attr) {
    for (int r = row; r < row + rows; r++) {
        if (r < 0 || r >= OSD_ROWS) continue;
        for (int c = col; c < col + cols; c++) {
            if (c < 0 || c >= OSD_COLS) continue;
            s->ch[r][c] = ' ';
            s->attr[r][c] = attr;
            s->big[r][c] = 0;
        }
    }
}

static inline void osd_putc(osd_surface_t* s, int row, int col, uint8_t ch, uint8_t attr) {
    if (row < 0 || row >= OSD_ROWS || col < 0 || col >= OSD_COLS) return;
    s->ch[row][col] = ch;
    s->attr[row][col] = attr;
    s->big[row][col] = 0;
}

// Caractère suivant d'une chaîne UTF-8, ramené au codage de la police
// (Latin-1 pour les accents ; '?' hors de portée)
static inline uint8_t osd_next_char(const char** p) {
    const uint8_t* s = (const uint8_t*)*p;
    uint32_t c = s[0];
    int n = 1;
    if (c >= 0xC0 && c < 0xE0 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        n = 2;
    } else if (c >= 0xE0 && c < 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        n = 3;
    } else if (c >= 0x80 && c < 0xC0) {
        c = '?';  // octet de continuation isolé
    } else if (c >= 0xF0) {
        c = '?';
        n = 1;
    }
    *p += n;
    if (c == 0x2014 || c == 0x2013) return OSD_EMDASH;  // — –
    if (c == 0x2026) return OSD_ELLIPSIS;               // …
    return c < 256 ? (uint8_t)c : '?';
}

// Écrit une chaîne UTF-8 ; au plus max cellules (max < 0 : jusqu'au bord).
// Retourne le nombre de cellules écrites.
static inline int osd_puts(osd_surface_t* s, int row, int col, const char* str, uint8_t attr, int max) {
    int n = 0;
    while (*str && (max < 0 || n < max) && col + n < OSD_COLS) {
        osd_putc(s, row, col + n, osd_next_char(&str), attr);
        n++;
    }
    return n;
}

// Longueur d'une chaîne UTF-8 en cellules
static inline int osd_strlen(const char* str) {
    int n = 0;
    while (*str) {
        osd_next_char(&str);
        n++;
    }
    return n;
}

// Grandes lettres : deux cellules par caractère
static inline int osd_puts_big(osd_surface_t* s, int row, int col, const char* str, uint8_t attr) {
    int n = 0;
    while (*str && col + n + 1 < OSD_COLS) {
        uint8_t c = osd_next_char(&str);
        osd_putc(s, row, col + n, c, attr);
        osd_putc(s, row, col + n + 1, c, attr);
        s->big[row][col + n] = OSD_BIG_LEFT;
        s->big[row][col + n + 1] = OSD_BIG_RIGHT;
        n += 2;
    }
    return n;
}

// Cadre à coins arrondis ; titre facultatif dans le filet du haut
static inline void osd_frame(osd_surface_t* s, int row, int col, int rows, int cols, uint8_t attr, const char* title,
                             uint8_t title_attr) {
    for (int c = col + 1; c < col + cols - 1; c++) {
        osd_putc(s, row, c, OSD_HLINE, attr);
        osd_putc(s, row + rows - 1, c, OSD_HLINE, attr);
    }
    for (int r = row + 1; r < row + rows - 1; r++) {
        osd_putc(s, r, col, OSD_VLINE, attr);
        osd_putc(s, r, col + cols - 1, OSD_VLINE, attr);
    }
    osd_putc(s, row, col, OSD_TL, attr);
    osd_putc(s, row, col + cols - 1, OSD_TR, attr);
    osd_putc(s, row + rows - 1, col, OSD_BL, attr);
    osd_putc(s, row + rows - 1, col + cols - 1, OSD_BR, attr);
    if (title) {
        osd_putc(s, row, col + 2, ' ', attr);
        int n = osd_puts(s, row, col + 3, title, title_attr, cols - 6);
        osd_putc(s, row, col + 3 + n, ' ', attr);
    }
}

// Masques par couleur : octet p = 0xFF si le plan p (rouge, vert, bleu) est
// allumé ; fond : 8 couleurs pleines puis 8 tramées, une table par parité de
// ligne (160 octets en tout, en RAM sur le Neo6502)
static uint32_t OSD_RAM osd_ink_lut[8];
static uint32_t OSD_RAM osd_paper_lut[2][16];
static bool osd_lut_ready = false;

static inline void osd_init_luts(void) {
    for (int c = 0; c < 16; c++) {
        uint32_t full = 0, even = 0, odd = 0;
        for (int p = 0; p < 3; p++) {
            if (!((c >> p) & 1)) continue;
            full |= 0xFFu << (8 * p);
            even |= (uint32_t)((c & 8) ? 0x55 : 0xFF) << (8 * p);
            odd |= (uint32_t)((c & 8) ? 0xAA : 0xFF) << (8 * p);
        }
        if (c < 8) osd_ink_lut[c] = full;
        osd_paper_lut[0][c] = even;
        osd_paper_lut[1][c] = odd;
    }
    osd_lut_ready = true;
}

// Une ligne de tampon (0 à OSD_LINES - 1) dans les plans rouge, vert, bleu.
// Par cellule : l'octet du glyphe recopié dans les trois octets d'un mot
// (multiplication, un cycle sur le RP2040), masqué par l'encre ; son
// complément par le fond.
static inline void OSD_RAM osd_render_line(const osd_surface_t* s, int line, uint32_t* red, uint32_t* green,
                                           uint32_t* blue) {
    if (!osd_lut_ready) osd_init_luts();
    const int row = line >> 3, y = line & 7;
    const uint8_t* chs = s->ch[row];
    const uint8_t* attrs = s->attr[row];
    const uint8_t* bigs = s->big[row];
    const uint32_t* paper_lut = osd_paper_lut[line & 1];
    for (int w = 0; w < OSD_COLS / 4; w++) {
        uint32_t r = 0, g = 0, b = 0;
        for (int k = 0; k < 4; k++) {
            const int c = w * 4 + k;
            uint32_t px = osd_font[chs[c]][y];
            if (bigs[c]) {
                const uint16_t wide = _osd_widen((uint8_t)px);
                px = bigs[c] == OSD_BIG_LEFT ? (wide & 0xFF) : (uint32_t)(wide >> 8);
            }
            const uint32_t a = attrs[c];
            // Fond : bits 4-6 couleur, bit 7 trame -> index 0-15
            const uint32_t v = ((px * 0x010101u) & osd_ink_lut[a & 7]) |
                               (((px ^ 0xFFu) * 0x010101u) & paper_lut[((a >> 4) & 7) | ((a >> 4) & 8)]);
            const uint32_t sh = 8u * (uint32_t)k;
            r |= (v & 0xFF) << sh;
            g |= ((v >> 8) & 0xFF) << sh;
            b |= (v >> 16) << sh;
        }
        red[w] = r;
        green[w] = g;
        blue[w] = b;
    }
}
