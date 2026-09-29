#pragma once

// printer_fx80.h — imprimante matricielle Epson FX-80 (ESC/P), pages en PNG
//
// Les octets reçus sur le port Centronics sont interprétés comme par une
// FX-80 (manuel « FX Series Printer User's Manual », Epson 1983, annexes B
// et C) et imprimés sur une page de 8,5 x 11 pouces rendue à 144 points par
// pouce (1224 x 1584 pixels, noir et blanc), écrite en PNG au fil de l'eau :
// l'image n'est jamais entière en mémoire, seule une bande de 32 lignes
// (≈ 5 Ko) l'est ; les lignes quittées par la tête sont compressées en blocs
// « stockés » (sans compression, donc sans table) et écrites aussitôt.
//
// Unités : horizontalement 1/1440 pouce (commun à toutes les densités :
// 60, 72, 80, 90, 120, 240 points par pouce) ; verticalement 1/216 pouce
// (ESC 3, ESC J). Une aiguille = 1/72 pouce = 3 unités = 2 pixels.
//
// Fait : Pica, Élite, Condensé, Élargi (SO, ESC W), Gras (ESC E),
// Double frappe (ESC G), Italique (ESC 4, codes 160-254), Souligné (ESC -),
// Exposant et indice (ESC S), Master Select (ESC !), interlignes (ESC 0 1 2 3
// A, ESC J j), longueur de page (ESC C), marges (ESC l Q), tabulations
// (HT, VT, ESC D B), graphiques (ESC K L Y Z * ^ ?), bit 8 (ESC # = >), jeu
// français (ESC R 1). Retour à la ligne automatique en fin de ligne.
// Non fait : Proportionnel (imprimé en Pica), caractères définis par
// l'utilisateur (ESC & : données lues et ignorées), jeux internationaux
// autres que États-Unis et France, tampon de ligne (CAN, DEL sans effet :
// chaque caractère est imprimé dès sa réception).
// Police : unscii-8 (8 x 8, domaine public, celle du menu), pas celle de la
// FX-80 (9 x 11) ; une rangée de glyphe par aiguille, la 9e aiguille pour le
// souligné.
//
// Écriture : fx80_busy() vrai tant qu'un saut de ligne attend l'écriture des
// lignes quittées ; fx80_service() en écrit au plus un nombre donné par
// appel (appelé à chaque trame sur le Neo6502 : la clé USB n'est jamais
// monopolisée). Une page est ouverte (fichier) au premier point imprimé ;
// fx80_finish() termine la page en cours (fin de travail, hors écriture).
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

#include "devices/printer_out.h"

#define FX80_DPI       144
#define FX80_WIDTH     1224                 // 8,5 pouces
#define FX80_ROW_BYTES (FX80_WIDTH / 8)
#define FX80_BAND      32                   // lignes de pixels en mémoire
#define FX80_LEFT      360                  // bord du papier -> première colonne : 1/4 pouce
#define FX80_LINE_MAX  11520                // 8 pouces (80 colonnes Pica)
#define FX80_PAGE_MAX  (22 * 216)           // ESC C 0 n : 22 pouces au plus

// ESC ! (Master Select) : un bit par mode
#define FX80_ELITE      0x01
#define FX80_PROP       0x02
#define FX80_CONDENSED  0x04
#define FX80_EMPHASIZED 0x08
#define FX80_DOUBLE     0x10
#define FX80_EXPANDED   0x20
#define FX80_ITALIC     0x40
#define FX80_UNDERLINE  0x80

typedef enum {
    FX80_P_NONE = 0,  // texte
    FX80_P_CMD,       // après ESC : lettre de commande
    FX80_P_ARGS,      // paramètres fixes
    FX80_P_GFX,       // données graphiques
    FX80_P_TABS,      // liste de tabulations (ESC D, B, b)
    FX80_P_SKIP,      // octets ignorés (ESC &)
} fx80_parse_t;

typedef struct {
    printer_out_t out;
    const uint8_t (*font)[8];  // 256 glyphes 8 x 8, bit 0 = pixel de gauche
    // Tête et réglages
    int32_t h;          // 1/1440 pouce depuis la première colonne
    int32_t v;          // 1/216 pouce depuis le haut de la page
    uint16_t spacing;   // interligne, 1/216 pouce
    uint16_t form;      // longueur de page, 1/216 pouce
    int32_t lmargin, rmargin;
    uint8_t mode;       // bits FX80_*
    bool so_expanded;   // élargi pour une ligne (SO)
    int8_t script;      // -1 : non ; 0 exposant ; 1 indice
    uint8_t intl;       // ESC R
    uint8_t msb;        // 0 : tel quel ; 1 : forcé à 0 ; 2 : forcé à 1
    bool print_hi;      // ESC 6 : 128-159 imprimables
    uint8_t gfx_map[4]; // densités de ESC K, L, Y, Z (ESC ?)
    int32_t htab[32];
    uint8_t nhtab;
    int32_t vtab[16];
    uint8_t nvtab;
    // Analyse des séquences
    fx80_parse_t parse;
    uint8_t cmd;
    uint8_t argc, need;
    uint8_t args[4];
    uint32_t left;      // données graphiques ou octets ignorés restants
    uint8_t gfx_step;   // pas des colonnes, 1/1440 pouce
    bool gfx9;          // ESC ^ : deux octets par colonne
    bool gfx_half;      // ESC ^ : premier octet reçu
    uint8_t gfx_first;
    uint8_t tab_max;
    int32_t tab_last;
    int16_t pending;    // caractère en attente après un retour à la ligne automatique (-1 : aucun)
    // Page
    bool page_open;     // fichier ouvert (au premier point)
    bool page_nofile;   // ouverture refusée : page perdue
    uint16_t page_rows;
    uint16_t band_top;  // première ligne de pixels pas encore écrite
    uint16_t target;    // lignes à écrire avant de continuer
    uint16_t debt;      // lignes blanches sautées avant l'ouverture
    bool finishing;     // page à terminer (saut de page, fin de travail)
    int32_t v_next;     // position de la tête sur la page suivante
    uint32_t pages;     // pages écrites
    // Flux PNG (un seul bloc IDAT, taille connue d'avance)
    uint32_t crc, adler;
    uint32_t raw_left, block_left;
    uint8_t band[FX80_BAND][FX80_ROW_BYTES];  // bit à 1 : papier blanc
} fx80_t;

static const uint8_t _fx80_gfx_step[7] = {24, 12, 12, 6, 18, 20, 16};  // modes ESC * 0-6

// Réglages de mise sous tension (ESC @)
static inline void _fx80_defaults(fx80_t* p) {
    p->spacing = 36;  // 1/6 pouce
    p->form = 11 * 216;
    p->lmargin = 0;
    p->rmargin = FX80_LINE_MAX;
    p->mode = 0;
    p->so_expanded = false;
    p->script = -1;
    p->intl = 0;
    p->msb = 0;
    p->print_hi = false;
    for (int i = 0; i < 4; i++) p->gfx_map[i] = (uint8_t)i;
    p->nhtab = 32;  // une tabulation tous les 8 caractères
    for (int i = 0; i < 32; i++) p->htab[i] = (i + 1) * 8 * 144;
    p->nvtab = 0;
    if (!p->page_open) p->page_rows = (uint16_t)(p->form * 2 / 3);
}

static inline void fx80_init(fx80_t* p, const printer_out_t* out, const uint8_t (*font)[8]) {
    memset(p, 0, sizeof(*p));
    if (out) p->out = *out;
    p->font = font;
    memset(p->band, 0xFF, sizeof(p->band));
    p->pending = -1;
    _fx80_defaults(p);
}

static inline bool _fx80_writing(const fx80_t* p) {
    return p->finishing || p->debt || p->band_top < p->target;
}

static inline bool fx80_busy(const fx80_t* p) { return p->pending >= 0 || _fx80_writing(p); }

/*-- Flux PNG ------------------------------------------------------------------*/

static inline void _fx80_w(fx80_t* p, const void* d, uint32_t n) {
    p->crc = printer_crc32(p->crc, (const uint8_t*)d, n);
    p->out.write(p->out.ctx, d, n);
}

static inline void _fx80_chunk(fx80_t* p, const char* type, uint32_t len) {
    uint8_t b[4];
    printer_be32(b, len);
    p->out.write(p->out.ctx, b, 4);
    p->crc = 0;
    _fx80_w(p, type, 4);
}

static inline void _fx80_chunk_end(fx80_t* p) {
    uint8_t b[4];
    printer_be32(b, p->crc);
    p->out.write(p->out.ctx, b, 4);
}

// Données zlib brutes : blocs « stockés » de 65535 octets au plus
static inline void _fx80_idat(fx80_t* p, const uint8_t* d, uint32_t n) {
    while (n) {
        if (!p->block_left) {
            const uint32_t len = p->raw_left < 65535 ? p->raw_left : 65535;
            const uint8_t hdr[5] = {(uint8_t)(len == p->raw_left), (uint8_t)len, (uint8_t)(len >> 8),
                                    (uint8_t)~len, (uint8_t)(~len >> 8)};
            _fx80_w(p, hdr, 5);
            p->block_left = len;
        }
        const uint32_t k = n < p->block_left ? n : p->block_left;
        p->adler = printer_adler32(p->adler, d, k);
        _fx80_w(p, d, k);
        p->block_left -= k;
        p->raw_left -= k;
        d += k;
        n -= k;
    }
}

static inline uint32_t _fx80_raw_size(uint16_t rows) { return (uint32_t)rows * (FX80_ROW_BYTES + 1); }

static inline void _fx80_png_begin(fx80_t* p) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    p->out.write(p->out.ctx, sig, 8);
    uint8_t ihdr[13] = {0};
    printer_be32(ihdr, FX80_WIDTH);
    printer_be32(ihdr + 4, p->page_rows);
    ihdr[8] = 1;  // 1 bit par pixel, niveaux de gris (0 : noir)
    _fx80_chunk(p, "IHDR", 13);
    _fx80_w(p, ihdr, 13);
    _fx80_chunk_end(p);
    uint8_t phys[9];
    printer_be32(phys, 5669);  // 144 points par pouce, en points par mètre
    printer_be32(phys + 4, 5669);
    phys[8] = 1;
    _fx80_chunk(p, "pHYs", 9);
    _fx80_w(p, phys, 9);
    _fx80_chunk_end(p);
    const uint32_t raw = _fx80_raw_size(p->page_rows);
    const uint32_t blocks = (raw + 65534) / 65535;
    _fx80_chunk(p, "IDAT", 2 + 5 * blocks + raw + 4);
    static const uint8_t zhdr[2] = {0x78, 0x01};
    _fx80_w(p, zhdr, 2);
    p->adler = 1;
    p->raw_left = raw;
    p->block_left = 0;
}

static inline void _fx80_png_end(fx80_t* p) {
    uint8_t b[4];
    printer_be32(b, p->adler);
    _fx80_w(p, b, 4);
    _fx80_chunk_end(p);
    _fx80_chunk(p, "IEND", 0);
    _fx80_chunk_end(p);
}

static inline void _fx80_row(fx80_t* p, const uint8_t* row) {
    static const uint8_t filter = 0;
    _fx80_idat(p, &filter, 1);
    _fx80_idat(p, row, FX80_ROW_BYTES);
}

/*-- Page ----------------------------------------------------------------------*/

static inline void _fx80_char(fx80_t* p, uint8_t c, bool italic);

// Premier point de la page : ouverture du fichier
static inline bool _fx80_ink(fx80_t* p) {
    if (p->page_open) return true;
    if (p->page_nofile || !printer_out_ok(&p->out)) return false;
    if (!p->out.open(p->out.ctx, "PNG")) {
        p->page_nofile = true;
        return false;
    }
    p->page_open = true;
    _fx80_png_begin(p);
    p->debt = p->band_top;  // lignes déjà sautées : blanches
    return true;
}

static inline void _fx80_dot(fx80_t* p, int32_t h, int32_t v) {
    if (h < 0 || v < 0) return;
    const int32_t x = (FX80_LEFT + h) / 10, y = v * 2 / 3;
    if (x + 1 >= FX80_WIDTH || y < p->band_top || y + 1 >= p->band_top + FX80_BAND || y + 1 >= p->page_rows) return;
    if (!_fx80_ink(p)) return;
    for (int dy = 0; dy < 2; dy++) {
        uint8_t* r = p->band[(y + dy) % FX80_BAND];
        r[x >> 3] &= (uint8_t)~(0x80 >> (x & 7));
        r[(x + 1) >> 3] &= (uint8_t)~(0x80 >> ((x + 1) & 7));
    }
}

// Tête déplacée : les lignes qu'elle ne peut plus atteindre (plus bas que la
// 9e aiguille, avec double frappe) sont à écrire avant la suite
static inline void _fx80_need(fx80_t* p) {
    int32_t top = (p->v + 27) * 2 / 3 + 2 - FX80_BAND;
    if (top > p->page_rows) top = p->page_rows;
    if (top > p->target) p->target = (uint16_t)top;
}

static inline void _fx80_new_page(fx80_t* p, int32_t v_next) {
    p->finishing = true;
    p->target = p->page_rows;
    p->v_next = v_next;
}

static inline void _fx80_vmove(fx80_t* p, int32_t v) {
    if (p->finishing) return;
    if (v < 0) v = 0;
    if (v >= p->form) {
        v -= p->form;
        _fx80_new_page(p, v < p->form ? v : 0);
        return;
    }
    p->v = v;
    _fx80_need(p);
}

// Écrit au plus max lignes ; retourne le nombre écrit
static inline int fx80_service(fx80_t* p, int max) {
    int n = 0;
    if (!p->page_open) {
        // Page blanche jusqu'ici : rien à écrire
        if (p->target > p->band_top) p->band_top = p->target;
    } else {
        static const uint8_t blank[FX80_ROW_BYTES] = {
            [0 ... FX80_ROW_BYTES - 1] = 0xFF,
        };
        while (p->debt && n < max) {
            _fx80_row(p, blank);
            p->debt--;
            n++;
        }
        while (!p->debt && p->band_top < p->target && n < max) {
            uint8_t* r = p->band[p->band_top % FX80_BAND];
            _fx80_row(p, r);
            memset(r, 0xFF, FX80_ROW_BYTES);
            p->band_top++;
            n++;
        }
    }
    if (p->finishing && !p->debt && p->band_top >= p->page_rows) {
        if (p->page_open) {
            _fx80_png_end(p);
            p->out.close(p->out.ctx);
            p->pages++;
        }
        p->page_open = p->page_nofile = false;
        p->finishing = false;
        memset(p->band, 0xFF, sizeof(p->band));
        p->band_top = p->target = 0;
        p->page_rows = (uint16_t)(p->form * 2 / 3);
        p->v = p->v_next;
        p->h = p->lmargin;
        _fx80_need(p);
    }
    if (p->pending >= 0 && !_fx80_writing(p)) {
        const int c = p->pending;
        p->pending = -1;
        _fx80_char(p, (uint8_t)(c & 0x7F), c >= 128);
    }
    return n;
}

// Fin de travail : la page en cours est terminée (feuille suivante en haut).
// Refusée (false) tant que fx80_busy() : appeler fx80_service() d'abord.
static inline bool fx80_finish(fx80_t* p) {
    if (fx80_busy(p)) return false;
    if (p->page_open) {
        _fx80_new_page(p, 0);
    } else {
        p->v = 0;
        p->h = p->lmargin;
        p->band_top = p->target = 0;
        p->page_nofile = false;
    }
    return true;
}

/*-- Texte ---------------------------------------------------------------------*/

// Pas des caractères, 1/1440 pouce
static inline int32_t _fx80_adv(const fx80_t* p) {
    int32_t a = (p->mode & FX80_ELITE) ? 120 : (p->mode & FX80_CONDENSED) ? 84 : 144;
    if ((p->mode & FX80_EXPANDED) || p->so_expanded) a *= 2;
    return a;
}

static inline void _fx80_cr(fx80_t* p) {
    p->h = p->lmargin;
    p->so_expanded = false;
}

static inline void _fx80_lf(fx80_t* p) {
    p->so_expanded = false;
    _fx80_vmove(p, p->v + p->spacing);
}

// Jeu français (ESC R 1) : table ESC/P usuelle, codes 35 à 126 remplacés
static inline uint8_t _fx80_intl(const fx80_t* p, uint8_t c) {
    if (p->intl != 1) return c;
    switch (c) {
        case '@': return 0xE0;  // à
        case '[': return 0xB0;  // °
        case '\\': return 0xE7; // ç
        case ']': return 0xA7;  // §
        case '{': return 0xE9;  // é
        case '|': return 0xF9;  // ù
        case '}': return 0xE8;  // è
        case '~': return 0xA8;  // ¨
        default: return c;
    }
}

static inline void _fx80_char(fx80_t* p, uint8_t c, bool italic) {
    const int32_t adv = _fx80_adv(p);
    if (p->font && c != ' ') {
        const uint8_t* g = p->font[_fx80_intl(p, c)];
        const bool narrow = (p->mode & (FX80_ELITE | FX80_CONDENSED)) != 0;
        const bool emph = (p->mode & FX80_EMPHASIZED) && !narrow;
        const bool dbl = (p->mode & FX80_DOUBLE) || p->script >= 0;
        const bool wide = (p->mode & FX80_EXPANDED) || p->so_expanded;
        italic = italic || (p->mode & FX80_ITALIC);
        for (int r = 0; r < 8; r++) {
            if (!g[r]) continue;
            const int32_t vy = p->script < 0 ? p->v + 3 * r : p->v + 12 * p->script + 3 * r / 2;
            const int32_t dx = italic ? (7 - r) * adv / 48 : 0;
            for (int cx = 0; cx < 8; cx++) {
                if (!(g[r] >> cx & 1)) continue;
                const int32_t x = p->h + dx + cx * adv / 8;
                _fx80_dot(p, x, vy);
                if (wide) _fx80_dot(p, x + adv / 16, vy);
                if (emph) _fx80_dot(p, x + 6, vy);
                if (dbl) _fx80_dot(p, x, vy + 1);
            }
        }
    }
    if (p->mode & FX80_UNDERLINE)
        for (int32_t x = 0; x < adv; x += 6) _fx80_dot(p, p->h + x, p->v + 24);
    p->h += adv;
}

/*-- Graphiques ----------------------------------------------------------------*/

static inline void _fx80_gfx_col(fx80_t* p, uint8_t b, bool pin9) {
    if (p->h <= p->rmargin) {
        for (int i = 0; i < 8; i++)
            if (b & (0x80 >> i)) _fx80_dot(p, p->h, p->v + 3 * i);
        if (pin9) _fx80_dot(p, p->h, p->v + 24);
    }
    p->h += p->gfx_step;
}

static inline void _fx80_gfx_begin(fx80_t* p, int mode, uint32_t n, bool nine) {
    p->gfx_step = _fx80_gfx_step[mode >= 0 && mode < 7 ? mode : 0];
    p->gfx9 = nine;
    p->gfx_half = false;
    p->left = nine ? 2 * n : n;
    p->parse = p->left ? FX80_P_GFX : FX80_P_NONE;
}

/*-- Séquences ESC -------------------------------------------------------------*/

static inline int _fx80_nargs(uint8_t cmd) {
    switch (cmd) {
        case '!': case '-': case '/': case '3': case 'A': case 'C': case 'I': case 'J': case 'N': case 'Q':
        case 'R': case 'S': case 'U': case 'W': case 'b': case 'i': case 'j': case 'l': case 'p': case 's':
            return 1;
        case '%': case '?': case 'K': case 'L': case 'Y': case 'Z':
            return 2;
        case '&': case '*': case ':': case '^':
            return 3;
        default:
            return 0;
    }
}

static inline void _fx80_tabs_begin(fx80_t* p, uint8_t max) {
    p->parse = FX80_P_TABS;
    p->tab_max = max;
    p->tab_last = 0;
    if (p->cmd == 'D') p->nhtab = 0;
    else p->nvtab = 0;
}

static inline void _fx80_exec(fx80_t* p) {
    const uint8_t* a = p->args;
    p->parse = FX80_P_NONE;
    switch (p->cmd) {
        case '!': p->mode = a[0]; break;
        case '-': p->mode = (a[0] & 1) ? p->mode | FX80_UNDERLINE : p->mode & ~FX80_UNDERLINE; break;
        case '0': p->spacing = 27; break;
        case '1': p->spacing = 21; break;
        case '2': p->spacing = 36; break;
        case '3': p->spacing = a[0]; break;
        case 'A': p->spacing = (uint16_t)(3 * (a[0] > 85 ? 85 : a[0])); break;
        case '4': p->mode |= FX80_ITALIC; break;
        case '5': p->mode &= ~FX80_ITALIC; break;
        case '6': p->print_hi = true; break;
        case '7': p->print_hi = false; break;
        case '#': p->msb = 0; break;
        case '=': p->msb = 1; break;
        case '>': p->msb = 2; break;
        case '@': _fx80_defaults(p); break;
        case 'C':
            if (a[0] == 0 && p->argc == 1) {
                // ESC C 0 n : longueur en pouces
                p->need = 2;
                p->parse = FX80_P_ARGS;
                return;
            }
            p->form = (uint16_t)(p->argc == 2 ? (a[1] > 22 ? 22 : a[1]) * 216 : a[0] * p->spacing);
            if (p->form < 36) p->form = 36;
            if (p->form > FX80_PAGE_MAX) p->form = FX80_PAGE_MAX;
            if (!p->page_open && !p->band_top) p->page_rows = (uint16_t)(p->form * 2 / 3);
            break;
        case 'D': _fx80_tabs_begin(p, 32); break;
        case 'B': _fx80_tabs_begin(p, 16); break;
        case 'b': if (!a[0]) _fx80_tabs_begin(p, 16); else { p->parse = FX80_P_TABS; p->tab_max = 0; } break;
        case 'E': p->mode |= FX80_EMPHASIZED; break;
        case 'F': p->mode &= ~FX80_EMPHASIZED; break;
        case 'G': p->mode |= FX80_DOUBLE; break;
        case 'H': p->mode &= ~FX80_DOUBLE; break;
        case 'M': p->mode |= FX80_ELITE; break;
        case 'P': p->mode &= ~FX80_ELITE; break;
        case 'p': p->mode = (a[0] & 1) ? p->mode | FX80_PROP : p->mode & ~FX80_PROP; break;
        case 'W': p->mode = (a[0] & 1) ? p->mode | FX80_EXPANDED : p->mode & ~FX80_EXPANDED; break;
        case 'S': p->script = (int8_t)(a[0] & 1); break;
        case 'T': p->script = -1; break;
        case 'R': p->intl = a[0]; break;
        case 'J': _fx80_vmove(p, p->v + a[0]); break;
        case 'j': _fx80_vmove(p, p->v - a[0]); break;
        case 'l': {
            const int32_t m = a[0] * _fx80_adv(p);
            if (m < p->rmargin) p->lmargin = m;
            break;
        }
        case 'Q': {
            const int32_t m = a[0] * _fx80_adv(p);
            if (m > p->lmargin && m <= FX80_LINE_MAX + 1440) p->rmargin = m;
            break;
        }
        case '?': {
            static const char keys[4] = {'K', 'L', 'Y', 'Z'};
            for (int i = 0; i < 4; i++)
                if (a[0] == keys[i] && a[1] < 7) p->gfx_map[i] = a[1];
            break;
        }
        case 'K': case 'L': case 'Y': case 'Z': {
            const int i = p->cmd == 'K' ? 0 : p->cmd == 'L' ? 1 : p->cmd == 'Y' ? 2 : 3;
            _fx80_gfx_begin(p, p->gfx_map[i], (uint32_t)a[0] | (uint32_t)a[1] << 8, false);
            break;
        }
        case '*': _fx80_gfx_begin(p, a[0], (uint32_t)a[1] | (uint32_t)a[2] << 8, false); break;
        case '^': _fx80_gfx_begin(p, a[0] ? 1 : 0, (uint32_t)a[1] | (uint32_t)a[2] << 8, true); break;
        case '&':
            // Caractères de l'utilisateur : attribut et 11 octets par caractère
            if (a[2] >= a[1]) {
                p->left = (uint32_t)(a[2] - a[1] + 1) * 12;
                p->parse = FX80_P_SKIP;
            }
            break;
        default: break;  // sans effet ici (vitesse, capteur de papier, sens…)
    }
}

static inline void _fx80_control(fx80_t* p, uint8_t c) {
    switch (c) {
        case 8: {
            const int32_t h = p->h - _fx80_adv(p);
            p->h = h < p->lmargin ? p->lmargin : h;
            break;
        }
        case 9:
            for (int i = 0; i < p->nhtab; i++)
                if (p->htab[i] > p->h) {
                    if (p->htab[i] < p->rmargin) p->h = p->htab[i];
                    break;
                }
            break;
        case 10: _fx80_lf(p); break;
        case 11: {
            // Tabulation verticale ; sans tabulation plus bas : saut de ligne
            for (int i = 0; i < p->nvtab; i++)
                if (p->vtab[i] > p->v) {
                    p->so_expanded = false;
                    _fx80_vmove(p, p->vtab[i]);
                    return;
                }
            _fx80_lf(p);
            break;
        }
        case 12:
            p->so_expanded = false;
            _fx80_new_page(p, 0);
            break;
        case 13: _fx80_cr(p); break;
        case 14: p->so_expanded = true; break;
        case 15: p->mode |= FX80_CONDENSED; break;
        case 18: p->mode &= ~FX80_CONDENSED; break;
        case 20: p->so_expanded = false; break;
        case 27:
            p->parse = FX80_P_CMD;
            break;
        default: break;  // NUL, BEL, DC1, DC3, CAN, DEL
    }
}

// Un octet reçu. Ne pas appeler tant que fx80_busy() (l'octet serait traité
// sur une page pas encore écrite) : attendre fx80_service().
static inline void fx80_feed(fx80_t* p, uint8_t c) {
    if (p->msb == 1) c &= 0x7F;
    else if (p->msb == 2) c |= 0x80;
    switch (p->parse) {
        case FX80_P_CMD:
            p->cmd = c & 0x7F;
            p->argc = 0;
            p->need = (uint8_t)_fx80_nargs(p->cmd);
            memset(p->args, 0, sizeof(p->args));
            if (p->need) p->parse = FX80_P_ARGS;
            else _fx80_exec(p);
            return;
        case FX80_P_ARGS:
            p->args[p->argc++] = c;
            if (p->argc >= p->need) _fx80_exec(p);
            return;
        case FX80_P_GFX:
            if (p->gfx9) {
                if (!p->gfx_half) {
                    p->gfx_first = c;
                    p->gfx_half = true;
                } else {
                    _fx80_gfx_col(p, p->gfx_first, (c & 0x80) != 0);
                    p->gfx_half = false;
                }
            } else {
                _fx80_gfx_col(p, c, false);
            }
            if (--p->left == 0) p->parse = FX80_P_NONE;
            return;
        case FX80_P_SKIP:
            if (--p->left == 0) p->parse = FX80_P_NONE;
            return;
        case FX80_P_TABS: {
            // Fin : 0 ou valeur inférieure à la précédente
            const int32_t unit = p->cmd == 'D' ? _fx80_adv(p) : p->spacing;
            const int32_t pos = (int32_t)c * unit;
            if (!c || pos <= p->tab_last) {
                p->parse = FX80_P_NONE;
                return;
            }
            p->tab_last = pos;
            if (p->cmd == 'D' && p->nhtab < p->tab_max) p->htab[p->nhtab++] = pos;
            else if (p->cmd != 'D' && p->nvtab < p->tab_max) p->vtab[p->nvtab++] = pos;
            return;
        }
        case FX80_P_NONE: break;
    }
    const uint8_t lo = c & 0x7F;
    if (lo < 32 || lo == 127) {
        if (c >= 128 && c < 160 && p->print_hi) return;  // caractères internationaux italiques : absents
        _fx80_control(p, lo);
        return;
    }
    // 160-254 : italique. Fin de ligne : retour à la ligne automatique ; le
    // caractère attend alors l'écriture des lignes quittées.
    if (p->h + _fx80_adv(p) > p->rmargin) {
        _fx80_cr(p);
        _fx80_lf(p);
        if (_fx80_writing(p)) {
            p->pending = (int16_t)(lo | (c & 0x80));
            return;
        }
    }
    _fx80_char(p, lo, c >= 128);
}
