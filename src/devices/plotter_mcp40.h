#pragma once

// plotter_mcp40.h — table traçante 4 couleurs Oric MCP-40, tracés en SVG
//
// Les octets reçus sur le port Centronics sont interprétés comme par la
// MCP-40 (manuel « MCP-40 Color Printer », Oric) et le tracé est écrit en SVG
// au fil de l'eau (aucun tracé gardé en mémoire) : un chemin par suite de
// traits de même plume et même type de ligne, un texte par suite de
// caractères. L'en-tête (hauteur du rouleau) est complété à la fermeture.
//
// Papier : 480 pas de 0,2 mm en largeur (96 mm), X vers la droite, Y vers le
// haut ; coordonnées de -999 à 999. Mécanisme du Tandy CGP-115 (même jeu de
// commandes ; son manuel complète celui de la MCP-40).
// Mode texte (à la mise sous tension) : 40 colonnes (commutateur DIP) ;
// CHR$(8) retour arrière, 10 saut de ligne, 11 saut de ligne inverse, 13
// retour chariot (sans saut de ligne : commutateur « CR only »), 18 mode
// graphique, 29 plume suivante. La taille choisie par S en mode graphique
// reste en vigueur en mode texte (manuel CGP-115).
// Mode graphique : commandes d'une lettre et paramètres, terminées par un
// retour chariot (LPRINT) : A (retour au mode texte, origine en marge
// gauche), C n (plume 0-3 : noir, bleu, vert, rouge), D x,y[,x,y…] (trait
// absolu), H (retour à l'origine), I (nouvelle origine), J x,y[,…] (trait
// relatif), L n (type de ligne 0-15), M x,y (déplacement absolu), P texte,
// Q n (sens d'écriture 0-3), R x,y (déplacement relatif), S n (taille :
// 80 / (n + 1) caractères par ligne), X a,pas,nombre (axe gradué) ;
// CHR$(17) : mode texte. En entrant en mode graphique, l'origine est en
// marge gauche, sous la plume.
//
// Couleurs : les manuels Oric (anglais et français) disent à la commande C
// « 0 noir, 1 rouge, 2 vert, 3 bleu » mais conseillent les plumes 1 noir,
// 2 bleu, 3 vert, 4 rouge ; le manuel du CGP-115 dit « 0 = Black, 1 = Blue,
// 2 = Green, 3 = Red » : gardé.
// Mesuré sur l'autotest imprimé du manuel CGP-115 (figure 12) : interligne
// du mode texte ≈ 2 fois la largeur d'un caractère, hauteur des capitales
// ≈ 1 fois. Estimé (aucun manuel ne le chiffre) : pointillés L n = n pas
// tracés, n pas levés (d'après l'allure de la table des types de ligne) ;
// graduations de X : 2 pas de part et d'autre de l'axe. Caractères tracés
// en police « monospace » du lecteur SVG, pas avec la police de la machine.
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
#include <stdio.h>
#include <string.h>

#include "devices/printer_out.h"

#define MCP40_WIDTH     480  // pas
#define MCP40_TEXT_COLS 40
#define MCP40_RUN_MAX   80
#define MCP40_MARGIN    8    // marge autour du tracé dans l'image, pas

static const char* const mcp40_colors[4] = {"#000000", "#1f3fbf", "#13893a", "#d42020"};  // noir, bleu, vert, rouge

typedef struct {
    printer_out_t out;
    bool open;         // fichier SVG ouvert
    bool nofile;       // ouverture refusée : travail perdu
    bool graphic;
    int32_t x, y;      // plume, pas
    int32_t ox, oy;    // origine
    uint8_t pen, ltype, size, dir;
    int32_t ymin, ymax;  // étendue verticale du tracé
    // Chemin en cours
    bool path;
    uint8_t path_pen, path_ltype;
    int32_t px, py;    // dernier point du chemin
    // Texte en cours (mode texte ou commande P)
    char run[MCP40_RUN_MAX + 1];
    uint8_t run_n;
    int32_t run_x, run_y, run_adv;
    uint8_t run_dir, run_pen;
    // Commande graphique
    uint8_t cmd;       // lettre ; 0 : aucune
    int32_t num[3];
    uint8_t nnum;
    int32_t cur;
    bool cur_neg, cur_any;
    // Tampon de sortie
    char buf[64];
    uint8_t buf_n;
    uint32_t files;    // fichiers terminés
} mcp40_t;

static inline void mcp40_init(mcp40_t* p, const printer_out_t* out) {
    memset(p, 0, sizeof(*p));
    if (out) p->out = *out;
    p->size = 480 / MCP40_TEXT_COLS / 6 - 1;  // 40 colonnes : S1
}

/*-- Sortie --------------------------------------------------------------------*/

static inline void _mcp40_flush(mcp40_t* p) {
    if (p->buf_n) p->out.write(p->out.ctx, p->buf, p->buf_n);
    p->buf_n = 0;
}

static inline void _mcp40_puts(mcp40_t* p, const char* s) {
    while (*s) {
        if (p->buf_n == sizeof(p->buf)) _mcp40_flush(p);
        p->buf[p->buf_n++] = *s++;
    }
}

#define _MCP40_PRINTF(p, ...)                    \
    do {                                         \
        char _t[96];                             \
        snprintf(_t, sizeof(_t), __VA_ARGS__);   \
        _mcp40_puts(p, _t);                      \
    } while (0)

// En-tête de longueur fixe (réécrit à la fermeture avec l'étendue réelle)
static inline void _mcp40_header(mcp40_t* p, int32_t top, int32_t height) {
    _MCP40_PRINTF(p, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" ");
    _MCP40_PRINTF(p, "width=\"%.1fmm\" height=\"%09.1fmm\" ", (MCP40_WIDTH + 2 * MCP40_MARGIN) * 0.2, height * 0.2);
    _MCP40_PRINTF(p, "viewBox=\"%d %011d %d %09d\" ", -MCP40_MARGIN, (int)top, MCP40_WIDTH + 2 * MCP40_MARGIN, (int)height);
    _mcp40_puts(p, "style=\"background:#fff\">\n");
}

static inline void _mcp40_extent(mcp40_t* p, int32_t y) {
    if (y < p->ymin) p->ymin = y;
    if (y > p->ymax) p->ymax = y;
}

static inline bool _mcp40_ink(mcp40_t* p) {
    if (p->open) return true;
    if (p->nofile || !printer_out_ok(&p->out) || !p->out.seek) return false;
    if (!p->out.open(p->out.ctx, "SVG")) {
        p->nofile = true;
        return false;
    }
    p->open = true;
    p->buf_n = 0;
    p->ymin = p->ymax = p->y;
    _mcp40_header(p, 0, 0);
    return true;
}

static inline void _mcp40_end_path(mcp40_t* p) {
    if (!p->path) return;
    _mcp40_puts(p, "\"/>\n");
    p->path = false;
}

static inline void _mcp40_end_run(mcp40_t* p) {
    if (!p->run_n) return;
    p->run[p->run_n] = 0;
    if (_mcp40_ink(p)) {
        _mcp40_end_path(p);
        // Capitales ≈ 0,72 em en « monospace » : hauteur ≈ largeur d'un caractère
        const int32_t fs = p->run_adv * 3 / 2;
        _MCP40_PRINTF(p, "<text x=\"%d\" y=\"%d\" font-family=\"monospace\" font-size=\"%d\" fill=\"%s\"",
                      (int)p->run_x, (int)-p->run_y, (int)fs, mcp40_colors[p->run_pen & 3]);
        _MCP40_PRINTF(p, " textLength=\"%d\" lengthAdjust=\"spacingAndGlyphs\"", (int)(p->run_adv * p->run_n));
        if (p->run_dir) _MCP40_PRINTF(p, " transform=\"rotate(%d %d %d)\"", 90 * p->run_dir, (int)p->run_x, (int)-p->run_y);
        _mcp40_puts(p, ">");
        for (int i = 0; i < p->run_n; i++) {
            const char c = p->run[i];
            char one[2] = {c, 0};
            _mcp40_puts(p, c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : one);
        }
        _mcp40_puts(p, "</text>\n");
        // Étendue : le texte monte d'environ une hauteur de caractère
        _mcp40_extent(p, p->run_y + fs);
        _mcp40_extent(p, p->run_y - fs);
        if (p->run_dir == 1) _mcp40_extent(p, p->run_y - p->run_adv * p->run_n);
        if (p->run_dir == 3) _mcp40_extent(p, p->run_y + p->run_adv * p->run_n);
    }
    p->run_n = 0;
}

// Caractère au point de la plume, qui avance
static inline void _mcp40_char(mcp40_t* p, char c, int32_t adv, uint8_t dir) {
    if (p->run_n && (p->run_n == MCP40_RUN_MAX || adv != p->run_adv || dir != p->run_dir || p->pen != p->run_pen))
        _mcp40_end_run(p);
    if (!p->run_n) {
        p->run_x = p->x;
        p->run_y = p->y;
        p->run_adv = adv;
        p->run_dir = dir;
        p->run_pen = p->pen;
    }
    p->run[p->run_n++] = c;
    static const int8_t dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};
    p->x += dx[dir & 3] * adv;
    p->y += dy[dir & 3] * adv;
}

// Trait de la plume jusqu'à (x, y)
static inline void _mcp40_draw(mcp40_t* p, int32_t x, int32_t y) {
    _mcp40_end_run(p);
    if (_mcp40_ink(p)) {
        if (p->path && (p->path_pen != p->pen || p->path_ltype != p->ltype)) _mcp40_end_path(p);
        if (!p->path) {
            _MCP40_PRINTF(p, "<path fill=\"none\" stroke=\"%s\" stroke-width=\"1.5\" stroke-linecap=\"round\"",
                          mcp40_colors[p->pen & 3]);
            if (p->ltype) _MCP40_PRINTF(p, " stroke-dasharray=\"%d %d\"", p->ltype, p->ltype);
            _mcp40_puts(p, " d=\"");
            p->path = true;
            p->path_pen = p->pen;
            p->path_ltype = p->ltype;
            p->px = p->x + 1;  // forcer le M
        }
        if (p->px != p->x || p->py != p->y) _MCP40_PRINTF(p, "M%d %d", (int)p->x, (int)-p->y);
        _MCP40_PRINTF(p, "L%d %d", (int)x, (int)-y);
        p->px = x;
        p->py = y;
        _mcp40_extent(p, p->y);
        _mcp40_extent(p, y);
    }
    p->x = x;
    p->y = y;
}

static inline void _mcp40_move(mcp40_t* p, int32_t x, int32_t y) {
    _mcp40_end_run(p);
    p->x = x;
    p->y = y;
}

// Fin de travail : fichier terminé (en-tête complété)
static inline void mcp40_finish(mcp40_t* p) {
    _mcp40_end_run(p);
    _mcp40_end_path(p);
    if (p->open) {
        _mcp40_puts(p, "</svg>\n");
        _mcp40_flush(p);
        p->out.seek(p->out.ctx, 0);
        const int32_t top = -(p->ymax + MCP40_MARGIN);
        _mcp40_header(p, top, p->ymax - p->ymin + 2 * MCP40_MARGIN);
        _mcp40_flush(p);
        p->out.close(p->out.ctx);
        p->files++;
    }
    p->open = p->nofile = false;
}

/*-- Mode texte ----------------------------------------------------------------*/

static inline void _mcp40_text(mcp40_t* p, uint8_t c) {
    const int32_t adv = 6 * (p->size + 1), pitch = 2 * adv;
    switch (c) {
        case 8: _mcp40_move(p, p->x >= adv ? p->x - adv : 0, p->y); return;
        case 10: _mcp40_move(p, p->x, p->y - pitch); return;
        case 11: _mcp40_move(p, p->x, p->y + pitch); return;
        case 13: _mcp40_move(p, 0, p->y); return;
        case 18:
            _mcp40_end_run(p);
            p->graphic = true;
            p->cmd = 0;
            p->ox = 0;  // origine : marge gauche, sous la plume
            p->oy = p->y;
            return;
        case 29:
            _mcp40_end_run(p);
            p->pen = (uint8_t)((p->pen + 1) & 3);
            return;
        default: break;
    }
    if (c < 32 || c > 126) return;
    if (p->x + adv > MCP40_WIDTH) _mcp40_move(p, 0, p->y - pitch);  // fin de ligne
    // Ligne de base : sous la position de la plume (haut de la ligne)
    const int32_t y = p->y;
    p->y = y - adv;
    _mcp40_char(p, (char)c, adv, 0);
    p->y = y;
}

/*-- Mode graphique ------------------------------------------------------------*/

// Nombre complet
static inline void _mcp40_push(mcp40_t* p) {
    if (!p->cur_any) return;
    int32_t v = p->cur_neg ? -p->cur : p->cur;
    if (v < -999) v = -999;
    if (v > 999) v = 999;
    if (p->nnum < 3) p->num[p->nnum++] = v;
    p->cur = 0;
    p->cur_neg = p->cur_any = false;
    // Paires des traits : exécutées au fil de l'eau
    if (p->nnum == 2) {
        switch (p->cmd) {
            case 'D': _mcp40_draw(p, p->ox + p->num[0], p->oy + p->num[1]); p->nnum = 0; break;
            case 'J': _mcp40_draw(p, p->x + p->num[0], p->y + p->num[1]); p->nnum = 0; break;
            case 'M': _mcp40_move(p, p->ox + p->num[0], p->oy + p->num[1]); break;
            case 'R': _mcp40_move(p, p->x + p->num[0], p->y + p->num[1]); break;
            default: break;
        }
    }
}

// Fin de commande (retour chariot ou lettre suivante)
static inline void _mcp40_exec(mcp40_t* p) {
    _mcp40_push(p);
    const int32_t n = p->nnum ? p->num[0] : 0;
    switch (p->cmd) {
        case 'A':
            _mcp40_end_run(p);
            p->graphic = false;
            _mcp40_move(p, 0, p->y);
            p->ox = 0;
            p->oy = p->y;
            break;
        case 'C': _mcp40_end_run(p); p->pen = (uint8_t)(n & 3); break;
        case 'H': _mcp40_move(p, p->ox, p->oy); break;
        case 'I': p->ox = p->x; p->oy = p->y; break;
        case 'L': p->ltype = (uint8_t)(n < 0 ? 0 : n > 15 ? 15 : n); break;
        case 'Q': p->dir = (uint8_t)(n & 3); break;
        case 'S': p->size = (uint8_t)(n < 0 ? 0 : n > 63 ? 63 : n); break;
        case 'P': _mcp40_end_run(p); break;
        case 'X':
            if (p->nnum == 3 && p->num[2] > 0) {
                // Axe : a = 1 axe X, 0 axe Y ; graduations de 2 pas de part et d'autre
                const bool xaxis = p->num[0] != 0;
                const int32_t step = p->num[1];
                for (int32_t i = 0; i < p->num[2] && i < 255; i++) {
                    const int32_t x = p->x, y = p->y;
                    if (xaxis) {
                        _mcp40_draw(p, x, y + 2);
                        _mcp40_draw(p, x, y - 2);
                        _mcp40_move(p, x, y);
                        _mcp40_draw(p, x + step, y);
                    } else {
                        _mcp40_draw(p, x + 2, y);
                        _mcp40_draw(p, x - 2, y);
                        _mcp40_move(p, x, y);
                        _mcp40_draw(p, x, y + step);
                    }
                }
                const int32_t x = p->x, y = p->y;
                if (xaxis) _mcp40_draw(p, x, y + 2), _mcp40_draw(p, x, y - 2);
                else _mcp40_draw(p, x + 2, y), _mcp40_draw(p, x - 2, y);
                _mcp40_move(p, x, y);
            }
            break;
        default: break;
    }
    p->cmd = 0;
    p->nnum = 0;
}

static inline void _mcp40_graphic(mcp40_t* p, uint8_t c) {
    if (c == 13) {
        _mcp40_exec(p);
        return;
    }
    if (p->cmd == 'P') {
        if (c >= 32 && c <= 126) _mcp40_char(p, (char)c, 6 * (p->size + 1), p->dir);
        return;
    }
    if (c == 17) {
        _mcp40_exec(p);
        _mcp40_end_run(p);
        p->graphic = false;
        return;
    }
    if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
    if (c >= 'A' && c <= 'Z') {
        _mcp40_exec(p);
        p->cmd = c;
        return;
    }
    if (c >= '0' && c <= '9') {
        p->cur = p->cur * 10 + (c - '0');
        if (p->cur > 9999) p->cur = 9999;
        p->cur_any = true;
    } else if (c == '-') {
        _mcp40_push(p);
        p->cur_neg = true;
    } else if (c == ',' || c == ' ') {
        _mcp40_push(p);
    }
}

// Un octet reçu
static inline void mcp40_feed(mcp40_t* p, uint8_t c) {
    if (p->graphic) _mcp40_graphic(p, c);
    else _mcp40_text(p, c);
}
