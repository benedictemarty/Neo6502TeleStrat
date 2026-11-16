#pragma once

// osd_config.h — cartouches (.rom) et réglages du menu dans TELESTRA.CFG
//
// Une image .rom de 16, 8, 4, 2 ou 1 Ko (osd_rom_size_ok) remplit une banque de
// 16 Ko, répétée comme une EPROM plus petite dans le support (rom_pool.h ; même
// règle que tools/fetch_roms.py pour TELEMATIC, 8 Ko).
//
// TELESTRA.CFG (une clé par ligne) : « a=NOM.DSK » … « d=NOM.DSK » (lecteurs),
// « bank1=NOM.ROM » … « bank7=NOM.ROM » (cartouches de la clé), « volume=0 »
// … « volume=8 » (8 : maximum, 0 : silence ; même clé que ORIC.CFG de reload).
// Le menu réécrit ces lignes et garde les autres (dial=, listen=, rs232=,
// reseau=…). Un fichier du réseau (TNFS) s'écrit avec le préfixe de volume
// de neo_storage.h : « a=net:/JEU.DSK », « bank5=net:/ORIX.ROM » ; un nom sans
// préfixe est sur la clé (les deux points n'existent pas dans un nom FAT).
// « reseau=hôte[:port] » : serveur TNFS (carte ; port 16384 par défaut).
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

#include "osd/osd_lang.h"   // brique du socle (socle-2026-11-11-28) : osd_printer_label, « langue= » (US-198, US-200)

#define OSD_BANK_BYTES 0x4000

// Taille acceptée pour une image .rom
static inline bool osd_rom_size_ok(size_t size) {
    return size == 0x4000 || size == 0x2000 || size == 0x1000 || size == 0x800 || size == 0x400;
}

// Clé de réglage d'une ligne (« a », « bank5 »…) ; retourne la valeur ou NULL
static inline const char* osd_config_value(const char* line, const char* key) {
    size_t n = strlen(key);
    if (strncmp(line, key, n) != 0 || line[n] != '=') return NULL;
    return line + n + 1;
}

// Serveur TNFS « hôte[:port] » (TELESTRA.CFG « reseau= », -N du banc) :
// hôte copié dans host, *port = le port donné ou dflt ; false si l'hôte est
// vide, trop long ou le port invalide
static inline bool osd_config_server(const char* value, char* host, size_t cap, uint16_t* port, uint16_t dflt) {
    const char* colon = strchr(value, ':');
    const size_t n = colon ? (size_t)(colon - value) : strlen(value);
    if (!n || n >= cap) return false;
    memcpy(host, value, n);
    host[n] = 0;
    *port = dflt;
    if (!colon) return true;
    unsigned long p = 0;
    const char* d = colon + 1;
    if (!*d) return false;
    for (; *d; d++) {
        if (*d < '0' || *d > '9' || (p = p * 10 + (unsigned long)(*d - '0')) > 65535) return false;
    }
    if (!p) return false;
    *port = (uint16_t)p;
    return true;
}

// Port TCP d'une valeur « listen= » : 1 à 65535, rien d'autre ; false sinon
static inline bool osd_config_port(const char* value, int* port) {
    unsigned long p = 0;
    if (!*value) return false;
    for (; *value; value++) {
        if (*value < '0' || *value > '9' || (p = p * 10 + (unsigned long)(*value - '0')) > 65535) return false;
    }
    if (!p) return false;
    *port = (int)p;
    return true;
}

// Lecture d'un fichier texte par morceaux : octets [pos, pos + len) dans buf
typedef bool (*osd_config_read_t)(void* ctx, uint32_t pos, uint8_t* buf, uint32_t len);

// Ligne suivante d'un fichier de size octets, lu à partir de *pos : fin de
// ligne retirée, ainsi que les espaces de fin. Une ligne de cap caractères ou
// plus est coupée à cap - 1 et sa suite sautée (jamais lue comme une autre
// ligne) : *cut vaut alors true. false à la fin du fichier ou si la lecture échoue.
static inline bool osd_config_line(osd_config_read_t read, void* ctx, uint32_t size, uint32_t* pos, char* line,
                                   uint32_t cap, bool* cut) {
    uint32_t n = 0;
    bool eol = false;
    *cut = false;
    if (*pos >= size) return false;
    while (!eol && *pos < size) {
        uint8_t blk[32];  // lecture par morceaux : un appel de lecture par 32 octets
        uint32_t want = size - *pos;
        if (want > sizeof(blk)) want = sizeof(blk);
        if (!read(ctx, *pos, blk, want)) return false;
        uint32_t i = 0;
        for (; i < want && !eol; i++) {
            if (blk[i] == '\n') eol = true;
            else if (n + 1 < cap) line[n++] = (char)blk[i];
            else *cut = true;
        }
        *pos += i;
    }
    while (n && (line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
    line[n] = 0;
    return true;
}

// Modèle d'imprimante (TELESTRA.CFG « imprimante_type= ») : texte brut dans
// un fichier, Epson FX-80 (pages PNG), table traçante MCP-40 (SVG)
enum { OSD_PRINTER_TEXT = 0, OSD_PRINTER_FX80, OSD_PRINTER_MCP40, OSD_PRINTER_TYPES };
static const char* const osd_printer_keys[OSD_PRINTER_TYPES] = {"texte", "fx80", "mcp40"};
static const char* const osd_printer_names[OSD_PRINTER_TYPES] = {"Texte", "Epson FX-80", "Traceur MCP-40"};
static const char* const osd_printer_names_en[OSD_PRINTER_TYPES] = {"Text", "Epson FX-80", "MCP-40 plotter"};

// Nom de l'imprimante t dans la langue du menu (osd_lang.h)
static inline const char* osd_printer_label(int t) { return OSD_T(osd_printer_names[t], osd_printer_names_en[t]); }

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
    int volume;              // volume=0…8 (-1 : ligne existante gardée telle quelle)
    const char* lang;        // langue=fr|en du menu (osd_lang_code) ; NULL : ligne existante gardée telle quelle
} osd_options_t;

// Volume d'une valeur « volume= » (0 à max), -1 si invalide (comme oric_config_volume de reload)
static inline int osd_config_volume(const char* v, int max) {
    int n = 0, digits = 0;
    while (v[digits] >= '0' && v[digits] <= '9' && digits < 3) n = n * 10 + (v[digits++] - '0');
    const char e = v[digits];
    if (!digits || (e && e != '\r' && e != '\n' && e != ' ') || n > max) return -1;
    return n;
}

// Clés gérées par le menu (options seulement si le menu les écrit)
static inline bool _osd_config_owned(const char* line, const osd_options_t* opt) {
    const bool options = opt != NULL;
    if (opt && opt->volume >= 0 && osd_config_value(line, "volume")) return true;
    if (opt && opt->lang && osd_config_value(line, "langue")) return true;
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
// (0 si out est trop petit : l'appelant doit alors refuser d'écrire, jamais
// écrire un fichier amputé).
// Avec les options du menu (opt ; NULL : lignes existantes gardées telles
// quelles)
static inline size_t osd_config_merge_ex(const char* old, const char* const drive[4], const char* const bank[8],
                                         const osd_options_t* opt, char* out, size_t cap) {
    size_t len = 0;
    char line[128];
#define _OSD_APPEND_N(s, n)                         \
    do {                                            \
        size_t l_ = (n);                            \
        if (len + l_ + 1 > cap) return 0;           \
        memcpy(out + len, s, l_);                   \
        len += l_;                                  \
        out[len] = 0;                               \
    } while (0)
#define _OSD_APPEND(s) _OSD_APPEND_N(s, strlen(s))
    if (cap) out[0] = 0;
    // Lignes gardées, recopiées sur leur longueur entière (seul le début, qui
    // porte la clé, sert à reconnaître une ligne du menu)
    const char* p = old ? old : "";
    while (*p) {
        const char* e = strchr(p, '\n');
        const size_t n = e ? (size_t)(e - p) : strlen(p);
        size_t m = n;
        while (m && (p[m - 1] == '\r' || p[m - 1] == ' ')) m--;
        const size_t k = m < sizeof(line) - 1 ? m : sizeof(line) - 1;
        memcpy(line, p, k);
        line[k] = 0;
        if (m && !_osd_config_owned(line, opt)) {
            _OSD_APPEND_N(p, m);
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
        if (opt->volume >= 0) {
            snprintf(line, sizeof(line), "volume=%d\n", opt->volume);
            _OSD_APPEND(line);
        }
        if (opt->lang) {
            snprintf(line, sizeof(line), "langue=%.8s\n", opt->lang);
            _OSD_APPEND(line);
        }
    }
#undef _OSD_APPEND
#undef _OSD_APPEND_N
    return len;
}
