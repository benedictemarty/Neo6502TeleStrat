#pragma once

// osd_config.h — cartouches (.rom) et réglages du menu dans TELESTRA.CFG
//
// osd_rom_fill : une image .rom de 16, 8, 4, 2 ou 1 Ko remplit une banque de
// 16 Ko, répétée comme une EPROM plus petite dans le support (même règle que
// tools/fetch_roms.py pour TELEMATIC, 8 Ko).
//
// TELESTRA.CFG (une clé par ligne) : « a=NOM.DSK » … « d=NOM.DSK » (lecteurs),
// « bank1=NOM.ROM » … « bank7=NOM.ROM » (cartouches de la clé). Le menu
// réécrit ces lignes et garde les autres (dial=, listen=, rs232=…).
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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OSD_BANK_BYTES 0x4000

// Taille acceptée pour une image .rom
static inline bool osd_rom_size_ok(size_t size) {
    return size == 0x4000 || size == 0x2000 || size == 0x1000 || size == 0x800 || size == 0x400;
}

// Remplit la banque (répétition des images plus petites) ; false si la taille ne va pas
static inline bool osd_rom_fill(uint8_t bank[OSD_BANK_BYTES], const uint8_t* data, size_t size) {
    if (!osd_rom_size_ok(size)) return false;
    for (size_t base = 0; base < OSD_BANK_BYTES; base += size) memcpy(bank + base, data, size);
    return true;
}

// Clé de réglage d'une ligne (« a », « bank5 »…) ; retourne la valeur ou NULL
static inline const char* osd_config_value(const char* line, const char* key) {
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || line[n] != '=') return NULL;
    return line + n + 1;
}

// Modèle d'imprimante (TELESTRA.CFG « imprimante_type= ») : texte brut dans
// un fichier, Epson FX-80 (pages PNG), table traçante MCP-40 (SVG)
enum { OSD_PRINTER_TEXT = 0, OSD_PRINTER_FX80, OSD_PRINTER_MCP40, OSD_PRINTER_TYPES };
static const char* const osd_printer_keys[OSD_PRINTER_TYPES] = {"texte", "fx80", "mcp40"};
static const char* const osd_printer_names[OSD_PRINTER_TYPES] = {"Texte", "Epson FX-80", "Traceur MCP-40"};

static inline int osd_printer_type(const char* value, int dflt) {
    for (int i = 0; value && i < OSD_PRINTER_TYPES; i++)
        if (!strcmp(value, osd_printer_keys[i])) return i;
    return dflt;
}

// Entrée sur « Imprimante » dans le menu : coupée -> premier modèle -> … ->
// dernier des ntypes modèles proposés -> coupée
static inline void osd_printer_cycle(bool* on, int* type, int ntypes) {
    if (!*on) {
        *on = true;
        *type = 0;
    } else if (*type + 1 < ntypes) {
        (*type)++;
    } else {
        *on = false;
    }
}

// Options du menu (panneau Périphériques), écrites dans TELESTRA.CFG
typedef struct {
    bool printer;            // impression=oui|non
    int printer_type;        // imprimante_type=texte|fx80|mcp40 (OSD_PRINTER_*)
    bool modem;              // modem=oui|non
    bool tape_turbo;         // cassette_rapide=oui|non
    bool tape_motor_always;  // cassette_moteur=toujours|relais
} osd_options_t;

// Clés gérées par le menu (options seulement si le menu les écrit)
static inline bool _osd_config_owned(const char* line, bool options) {
    static const char* const keys[] = {"a", "b", "c", "d", "bank1", "bank2", "bank3", "bank4", "bank5", "bank6", "bank7"};
    static const char* const opts[] = {"impression", "imprimante_type", "modem", "cassette_rapide", "cassette_moteur"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (osd_config_value(line, keys[i])) return true;
    for (size_t i = 0; options && i < sizeof(opts) / sizeof(opts[0]); i++)
        if (osd_config_value(line, opts[i])) return true;
    return false;
}

// Valeur oui / non d'une clé (défaut si absente ou autre)
static inline bool osd_config_yes(const char* value, bool dflt) {
    if (!value) return dflt;
    if (!strcmp(value, "oui") || !strcmp(value, "1")) return true;
    if (!strcmp(value, "non") || !strcmp(value, "0")) return false;
    return dflt;
}

// Réécrit TELESTRA.CFG : lignes du menu remplacées, autres gardées. drive[d] :
// image du lecteur (NULL ou "" : aucune ligne) ; bank[b] : cartouche de la clé
// en banque b (NULL ou "" : contenu d'origine). Retourne la longueur écrite
// (0 si out est trop petit).
// Avec les options du menu (opt ; NULL : lignes existantes gardées telles
// quelles)
static inline size_t osd_config_merge_ex(const char* old, const char* const drive[4], const char* const bank[8],
                                         const osd_options_t* opt, char* out, size_t cap) {
    size_t len = 0;
    char line[128];
#define _OSD_APPEND(s)                              \
    do {                                            \
        size_t l_ = strlen(s);                      \
        if (len + l_ + 1 > cap) return 0;           \
        memcpy(out + len, s, l_);                   \
        len += l_;                                  \
        out[len] = 0;                               \
    } while (0)
    if (cap) out[0] = 0;
    // Lignes gardées
    const char* p = old ? old : "";
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        size_t m = n < sizeof(line) - 1 ? n : sizeof(line) - 1;
        memcpy(line, p, m);
        line[m] = 0;
        while (m && (line[m - 1] == '\r' || line[m - 1] == ' ')) line[--m] = 0;
        if (m && !_osd_config_owned(line, opt != NULL)) {
            _OSD_APPEND(line);
            _OSD_APPEND("\n");
        }
        p += n + (e ? 1 : 0);
    }
    // Lignes du menu
    for (int d = 0; d < 4; d++) {
        if (!drive[d] || !drive[d][0]) continue;
        snprintf(line, sizeof(line), "%c=%.100s\n", 'a' + d, drive[d]);
        _OSD_APPEND(line);
    }
    for (int b = 1; b < 8; b++) {
        if (!bank[b] || !bank[b][0]) continue;
        snprintf(line, sizeof(line), "bank%d=%.100s\n", b, bank[b]);
        _OSD_APPEND(line);
    }
    if (opt) {
        _OSD_APPEND(opt->printer ? "impression=oui\n" : "impression=non\n");
        if (opt->printer_type >= 0 && opt->printer_type < OSD_PRINTER_TYPES) {
            snprintf(line, sizeof(line), "imprimante_type=%s\n", osd_printer_keys[opt->printer_type]);
            _OSD_APPEND(line);
        }
        _OSD_APPEND(opt->modem ? "modem=oui\n" : "modem=non\n");
        _OSD_APPEND(opt->tape_turbo ? "cassette_rapide=oui\n" : "cassette_rapide=non\n");
        _OSD_APPEND(opt->tape_motor_always ? "cassette_moteur=toujours\n" : "cassette_moteur=relais\n");
    }
#undef _OSD_APPEND
    return len;
}

static inline size_t osd_config_merge(const char* old, const char* const drive[4], const char* const bank[8], char* out,
                                      size_t cap) {
    return osd_config_merge_ex(old, drive, bank, NULL, out, cap);
}
