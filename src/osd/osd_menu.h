#pragma once

// osd_menu.h — menu du Telestrat (OSD) : disquettes, cartouches, clé USB
//
// Page principale : lecteurs A à D, cassette, banques 7 à 1 (cartouches),
// boutons Redémarrer / Enregistrer / Reprendre. Entrée sur un lecteur, la
// cassette ou une banque : sélecteur de fichiers de la clé (.dsk, .tap, .rom ;
// pour une banque, les ROM intégrées d'abord), avec défilement et saut à
// l'initiale tapée. Volume Réseau prêt (net_present) : choix de la source
// (Clé USB / Réseau) avant le sélecteur ; les fichiers du réseau sont nommés
// « net:/NOM » (préfixe de neo_storage.h du socle). osd_tape_banner dessine le
// bandeau de la cassette. La plate-forme remplit l'état (lecteurs, banques,
// clé, fichiers, message) et exécute les actions rendues par osd_menu_key().
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
#include "osd/osd_netcfg.h"   // page Réseau (TNFS) commune du socle ; porte aussi OSD_KEY_* (osd_browser.h)
#include "osd/net_status.h"   // signes Wi-Fi et TNFS de la ligne Réseau (NET_SIGN_*)
#include "osd/osd_lang.h"     // langue du menu (OSD_T), touche L

#ifndef OSD_MENU_FILES
#define OSD_MENU_FILES 64  // fichiers listés (64 par défaut ; platforms/rp2040/CMakeLists.txt : 40, ou 16 avec la RAM 64 Ko)
#endif
#define OSD_NAME_LEN   48
// Préfixe des fichiers du réseau (NEO_NET_PREFIX "/" de neo_storage.h) ; un
// nom sans préfixe est sur la clé
#define OSD_NET_PREFIX     "net:/"
#define OSD_NET_PREFIX_LEN 5

enum { OSD_FILE_DSK = 0, OSD_FILE_ROM = 1, OSD_FILE_TAP = 2, OSD_FILE_STA = 3 };

#define OSD_BUILTINS 4  // ROM intégrées proposées pour une banque
#define OSD_PROFILES 6  // profils de démarrage

// Contenu d'une banque
enum { OSD_BANK_EMPTY = 0, OSD_BANK_RAM, OSD_BANK_ROM, OSD_BANK_ROM_USB, OSD_BANK_ROM_NET };

// Source des fichiers (volume de neo_storage : 0 la clé, 1 le réseau ; OSD_SRC_* pour ne pas heurter OSD_VOL_* du socle)
enum { OSD_SRC_USB = 0, OSD_SRC_NET = 1 };

enum { OSD_PAGE_MAIN = 0, OSD_PAGE_BROWSE, OSD_PAGE_SOURCE, OSD_PAGE_NET };

// Touches du menu : OSD_KEY_* de osd_browser.h (socle), en plus des caractères imprimables (saut)

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
    OSD_ACT_PRINTER,      // imprimante activée / coupée
    OSD_ACT_MODEM,        // modem activé / coupé
    OSD_ACT_TAPE_TURBO,   // cassette : lecture accélérée oui / non
    OSD_ACT_TAPE_MOTOR,   // cassette : moteur toujours en marche / relais
    OSD_ACT_STATE_SAVE,   // nouvel instantané
    OSD_ACT_STATE_LOAD,   // file = instantané à reprendre
    OSD_ACT_PROFILE,      // démarrage : file = profil, -1 : configuration de la clé
    OSD_ACT_SOURCE,       // source choisie : file = volume ; la plate-forme le relit
                          // (neo_storage_list) puis appelle osd_menu_browse
    OSD_ACT_NET_OPEN,     // bouton Réseau : la plate-forme remplit la page (osd_menu_open_net)
    OSD_ACT_NET_SAVE,     // page Réseau validée : menu.netcfg à écrire dans TELESTRA.CFG (osd_netcfg_merge)
    OSD_ACT_LANG,         // touche L de la page principale : la plate-forme bascule osd_lang (français / anglais)
};

typedef struct {
    int type;
    int target;
    int file;
} osd_action_t;

typedef struct {
    char name[OSD_NAME_LEN];  // réseau : « net:/NOM »
    uint32_t size;
    uint8_t kind;
    uint8_t vol;  // OSD_SRC_USB, OSD_SRC_NET
} osd_file_t;

// Entrée de répertoire à ne pas lister : cachée, ou « ._NOM » (fichier AppleDouble
// laissé par macOS sur une clé FAT, de contenu sans rapport avec « NOM »)
static inline bool osd_name_ignored(const char* name, bool hidden) {
    return hidden || (name[0] == '.' && name[1] == '_');
}

// Fichier du réseau (nom préfixé)
static inline bool osd_is_net(const char* name) { return !strncmp(name, OSD_NET_PREFIX, OSD_NET_PREFIX_LEN); }

// Éléments de la page principale
#define OSD_ITEM_DRIVE0  0   // 0-3 : lecteurs A-D
#define OSD_ITEM_TAPE    4   // cassette
#define OSD_ITEM_BANK7   5   // 5-11 : banques 7 à 1
#define OSD_ITEM_STATE   12  // instantanés (enregistrer, reprendre)
#define OSD_ITEM_PRINTER 13  // périphériques
#define OSD_ITEM_MODEM   14
#define OSD_ITEM_TURBO   15  // cassette : lecture accélérée
#define OSD_ITEM_MOTOR   16  // cassette : moteur toujours en marche
#define OSD_ITEM_RESET   17
#define OSD_ITEM_SAVE    18
#define OSD_ITEM_RESUME  19
#define OSD_ITEMS        20   // éléments du cycle haut / bas
#define OSD_ITEM_NET     20   // Réseau (TNFS), à droite des instantanés : hors du cycle (gauche / droite)
#define OSD_ITEM_BOOT    100  // page de démarrage (hors de la page principale)

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
    const char* profile[OSD_PROFILES];  // profils de démarrage (NULL : fin)
    bool printer_on;              // impression vers le fichier printer_file
    const char* printer_model;    // « Texte », « Epson FX-80 »… (NULL : non affiché)
    char printer_file[OSD_NAME_LEN];  // fichier texte ou dernière page écrite
    bool modem_on;                // modem utilisé (sinon ligne coupée)
    char state_last[OSD_NAME_LEN];  // dernier instantané enregistré ou repris
    bool tape_turbo;              // cassette : lecture accélérée (BASIC 1.1)
    bool tape_motor_always;       // cassette : moteur toujours en marche
    const char* modem_state;      // « absent », « branché », « sonnerie », « en ligne »…
    bool usb_present;
    char usb_label[OSD_NAME_LEN];
    bool net_present;             // volume Réseau prêt : choix de la source proposé
    const char* net_label;        // serveur (NULL : non affiché)
    const net_status_t* net_status;  // signes Wi-Fi et TNFS de la ligne Réseau (net_status.h) ; NULL ou sans détail : aide
    osd_file_t files[OSD_MENU_FILES];
    int nfiles;
    bool truncated;               // liste incomplète : OSD_MENU_FILES atteint (la plate-forme le lève à chaque relecture)
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
    int browse_vol;     // source du sélecteur (OSD_VOL_*)
    int source_cursor;  // page de la source : 0 clé, 1 réseau
    osd_netcfg_t netcfg;  // page Réseau (OSD_PAGE_NET)
} osd_menu_t;

// Remise à zéro (banc PC et tests : le firmware tient `menu` en .bss, déjà à zéro)
static inline void osd_menu_init(osd_menu_t* m) {
    memset(m, 0, sizeof(*m));
    m->version = "";
    m->cursor = OSD_ITEM_RESUME;
}

// Page Réseau remplie par la plate-forme : serveur, Wi-Fi et mot de passe en cours
static inline void osd_menu_open_net(osd_menu_t* m, const char* server, const char* ssid, const char* password) {
    osd_netcfg_load(&m->netcfg, NULL);
    snprintf(m->netcfg.server, sizeof(m->netcfg.server), "%s", server ? server : "");
    snprintf(m->netcfg.ssid, sizeof(m->netcfg.ssid), "%s", ssid ? ssid : "");
    snprintf(m->netcfg.pass, sizeof(m->netcfg.pass), "%s", password ? password : "");
    m->netcfg.cursor = (int)strlen(m->netcfg.server);
    m->page = OSD_PAGE_NET;
}

static inline int _osd_item_bank(int item) { return 7 - (item - OSD_ITEM_BANK7); }

static inline bool _osd_item_is_drive(int item) { return item >= OSD_ITEM_DRIVE0 && item < OSD_ITEM_TAPE; }

static inline bool _osd_item_is_bank(int item) { return item >= OSD_ITEM_BANK7 && item < OSD_ITEM_BANK7 + 7; }

// Nom d'un élément de la liste du sélecteur
static inline const char* _osd_entry_name(const osd_menu_t* m, int entry) {
    if (entry <= -100) return m->profile[-100 - entry];
    return entry >= 0 ? m->files[entry].name : m->builtin[-2 - entry];
}

// Nom affiché : sans le préfixe du réseau (la source est dans le titre)
static inline const char* _osd_shown_name(const char* name) {
    return osd_is_net(name) ? name + OSD_NET_PREFIX_LEN : name;
}

static inline void _osd_open_browser(osd_menu_t* m, int item);

// Page de démarrage : profils (TELESTRA.CFG « demarrage=choix »)
static inline void osd_menu_open_boot(osd_menu_t* m) {
    m->browse_vol = OSD_SRC_USB;
    _osd_open_browser(m, OSD_ITEM_BOOT);
}

static inline void _osd_open_browser(osd_menu_t* m, int item) {
    if (item == OSD_ITEM_BOOT) {
        m->browse_count = 0;
        for (int k = 0; k < OSD_PROFILES && m->profile[k]; k++) m->browse_list[m->browse_count++] = -100 - k;
        m->browse_target = item;
        m->browse_cursor = 0;
        m->browse_scroll = 0;
        m->page = OSD_PAGE_BROWSE;
        return;
    }
    const uint8_t kind = _osd_item_is_drive(item) ? OSD_FILE_DSK
                         : item == OSD_ITEM_TAPE  ? OSD_FILE_TAP
                         : item == OSD_ITEM_STATE ? OSD_FILE_STA
                                                  : OSD_FILE_ROM;
    m->browse_count = 0;
    if (kind == OSD_FILE_ROM && m->browse_vol == OSD_SRC_USB)
        for (int k = 0; k < OSD_BUILTINS && m->builtin[k]; k++) m->browse_list[m->browse_count++] = -2 - k;
    for (int i = 0; i < m->nfiles && m->browse_count < OSD_MENU_FILES; i++)
        if (m->files[i].kind == kind && m->files[i].vol == m->browse_vol) m->browse_list[m->browse_count++] = i;
    m->browse_target = item;
    m->browse_cursor = 0;
    m->browse_scroll = 0;
    // Curseur sur l'image, la cassette ou la cartouche en place
    const char* cur = _osd_item_is_drive(item)   ? m->drive[item]
                      : item == OSD_ITEM_TAPE  ? m->tape
                      : item == OSD_ITEM_STATE ? m->state_last
                                               : m->bank[_osd_item_bank(item)];
    for (int k = 0; k < m->browse_count; k++)
        if (!strcmp(_osd_entry_name(m, m->browse_list[k]), cur)) m->browse_cursor = k + 1;
    m->page = OSD_PAGE_BROWSE;
}

// Sélecteur de la source vol pour l'élément item (après OSD_ACT_SOURCE : la
// liste du volume relue par la plate-forme)
static inline void osd_menu_browse(osd_menu_t* m, int item, int vol) {
    m->browse_vol = vol;
    _osd_open_browser(m, item);
}

// Entrée sur un lecteur, la cassette, une banque ou les instantanés : choix de
// la source si le réseau est prêt (curseur sur celle du fichier en place),
// sinon le sélecteur de la clé
static inline void _osd_open_source(osd_menu_t* m, int item) {
    if (!m->net_present) {
        osd_menu_browse(m, item, OSD_SRC_USB);
        return;
    }
    const char* cur = _osd_item_is_drive(item)   ? m->drive[item]
                      : item == OSD_ITEM_TAPE  ? m->tape
                      : item == OSD_ITEM_STATE ? m->state_last
                                               : m->bank[_osd_item_bank(item)];
    m->browse_target = item;
    m->source_cursor = osd_is_net(cur) ? 1 : 0;
    m->page = OSD_PAGE_SOURCE;
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

// Page du choix de la source (clé, réseau)
static inline osd_action_t _osd_key_source(osd_menu_t* m, int key) {
    osd_action_t a = {OSD_ACT_NONE, 0, -1};
    switch (key) {
        case OSD_KEY_UP:
        case OSD_KEY_DOWN: m->source_cursor = !m->source_cursor; break;
        case OSD_KEY_HOME: m->source_cursor = 0; break;
        case OSD_KEY_END: m->source_cursor = 1; break;
        case OSD_KEY_ESC:
        case OSD_KEY_LEFT: m->page = OSD_PAGE_MAIN; break;
        case OSD_KEY_ENTER:
        case OSD_KEY_RIGHT:
            // Sélecteur ouvert sur la liste connue ; la plate-forme relit le volume
            a.type = OSD_ACT_SOURCE;
            a.target = m->browse_target;
            a.file = m->source_cursor ? OSD_SRC_NET : OSD_SRC_USB;
            osd_menu_browse(m, m->browse_target, a.file);
            break;
        default:
            // Initiale : C (clé), R (réseau)
            if (_osd_upper(key) == 'C') m->source_cursor = 0;
            else if (_osd_upper(key) == 'R') m->source_cursor = 1;
            break;
    }
    return a;
}

// Sélecteur de fichiers
static inline osd_action_t _osd_key_browse(osd_menu_t* m, int key) {
    osd_action_t a = {OSD_ACT_NONE, 0, -1};
    const int n = m->browse_count + 1;
    switch (key) {
        case OSD_KEY_UP: m->browse_cursor = (m->browse_cursor + n - 1) % n; break;
        case OSD_KEY_DOWN: m->browse_cursor = (m->browse_cursor + 1) % n; break;
        case OSD_KEY_PGUP: m->browse_cursor -= OSD_BROWSE_VISIBLE; break;
        case OSD_KEY_PGDN: m->browse_cursor += OSD_BROWSE_VISIBLE; break;
        case OSD_KEY_HOME: m->browse_cursor = 0; break;
        case OSD_KEY_END: m->browse_cursor = n - 1; break;
        case OSD_KEY_ESC:
        case OSD_KEY_LEFT:
            m->page = OSD_PAGE_MAIN;
            if (m->browse_target == OSD_ITEM_BOOT) a.type = OSD_ACT_RESUME;  // configuration de la clé
            break;
        case OSD_KEY_ENTER: {
            const int item = m->browse_target;
            const int file = m->browse_cursor ? m->browse_list[m->browse_cursor - 1] : -1;
            const bool none = m->browse_cursor == 0;
            if (item == OSD_ITEM_BOOT) {
                a.type = OSD_ACT_PROFILE;
                a.file = none ? -1 : -100 - file;
            } else if (_osd_item_is_drive(item)) {
                a.target = item - OSD_ITEM_DRIVE0;
                a.type = none ? OSD_ACT_EJECT : OSD_ACT_INSERT;
            } else if (item == OSD_ITEM_TAPE) {
                a.type = none ? OSD_ACT_TAPE_EJECT : OSD_ACT_TAPE_INSERT;
            } else if (item == OSD_ITEM_STATE) {
                a.type = none ? OSD_ACT_STATE_SAVE : OSD_ACT_STATE_LOAD;
            } else {
                a.target = _osd_item_bank(item);
                a.type = none ? OSD_ACT_RESTORE : file >= 0 ? OSD_ACT_LOAD_ROM : OSD_ACT_LOAD_BUILTIN;
            }
            if (item != OSD_ITEM_BOOT) a.file = none ? -1 : file >= 0 ? file : -2 - file;
            m->page = OSD_PAGE_MAIN;
            break;
        }
        default:
            // Saut au fichier suivant qui commence par la lettre tapée
            if (key > ' ' && key < 0x100) {
                for (int k = 1; k <= m->browse_count; k++) {
                    const int idx = (m->browse_cursor - 1 + k) % m->browse_count;
                    if (_osd_upper((uint8_t)_osd_shown_name(_osd_entry_name(m, m->browse_list[idx]))[0]) ==
                        _osd_upper(key)) {
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

// Page principale
static inline osd_action_t _osd_key_main(osd_menu_t* m, int key) {
    osd_action_t a = {OSD_ACT_NONE, 0, -1};
    int c = m->cursor;
    switch (key) {
        case OSD_KEY_UP:
            c = c == OSD_ITEM_NET ? OSD_ITEM_BANK7 + 6 : (c + OSD_ITEMS - 1) % OSD_ITEMS;
            break;
        case OSD_KEY_DOWN: c = c == OSD_ITEM_NET ? OSD_ITEM_MODEM : (c + 1) % OSD_ITEMS; break;
        case OSD_KEY_LEFT:
            if (c == OSD_ITEM_NET) c = OSD_ITEM_STATE;
            else if (c == OSD_ITEM_MODEM) c = OSD_ITEM_PRINTER;
            else if (c == OSD_ITEM_MOTOR) c = OSD_ITEM_TURBO;
            else if (c == OSD_ITEM_PRINTER || c == OSD_ITEM_TURBO) break;
            else if (_osd_item_is_bank(c)) c = OSD_ITEM_DRIVE0 + (c - OSD_ITEM_BANK7 < 5 ? c - OSD_ITEM_BANK7 : 4);
            else if (c > OSD_ITEM_RESET) c--;
            break;
        case OSD_KEY_RIGHT:
            if (c <= OSD_ITEM_TAPE) c = OSD_ITEM_BANK7 + (c - OSD_ITEM_DRIVE0);
            else if (c == OSD_ITEM_STATE) c = OSD_ITEM_NET;
            else if (c == OSD_ITEM_PRINTER) c = OSD_ITEM_MODEM;
            else if (c == OSD_ITEM_TURBO) c = OSD_ITEM_MOTOR;
            else if (c >= OSD_ITEM_RESET && c < OSD_ITEM_RESUME) c++;
            break;
        case OSD_KEY_HOME: c = 0; break;
        case OSD_KEY_END: c = OSD_ITEMS - 1; break;
        case OSD_KEY_ESC: a.type = OSD_ACT_RESUME; break;
        case 'L':
        case 'l': a.type = OSD_ACT_LANG; break;   // langue du menu (osd_lang.h)
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
            if (c <= OSD_ITEM_TAPE || _osd_item_is_bank(c) || c == OSD_ITEM_STATE) _osd_open_source(m, c);
            else if (c == OSD_ITEM_NET) a.type = OSD_ACT_NET_OPEN;
            else if (c == OSD_ITEM_PRINTER) a.type = OSD_ACT_PRINTER;
            else if (c == OSD_ITEM_MODEM) a.type = OSD_ACT_MODEM;
            else if (c == OSD_ITEM_TURBO) a.type = OSD_ACT_TAPE_TURBO;
            else if (c == OSD_ITEM_MOTOR) a.type = OSD_ACT_TAPE_MOTOR;
            else if (c == OSD_ITEM_RESET) a.type = OSD_ACT_RESET;
            else if (c == OSD_ITEM_SAVE) a.type = OSD_ACT_SAVE;
            else a.type = OSD_ACT_RESUME;
            break;
        default: break;
    }
    m->cursor = c;
    return a;
}

static inline osd_action_t osd_menu_key(osd_menu_t* m, int key) {
    if (m->page == OSD_PAGE_NET) {
        osd_action_t a = {OSD_ACT_NONE, 0, -1};
        const osd_netcfg_result_t r = osd_netcfg_key(&m->netcfg, key);   // OSD_NETCFG_SCAN : la plate-forme cherche à l'image suivante
        if (r == OSD_NETCFG_CANCEL) m->page = OSD_PAGE_MAIN;
        if (r == OSD_NETCFG_SAVE) {
            m->page = OSD_PAGE_MAIN;
            a.type = OSD_ACT_NET_SAVE;
        }
        return a;
    }
    if (m->page == OSD_PAGE_SOURCE) return _osd_key_source(m, key);
    if (m->page == OSD_PAGE_BROWSE) return _osd_key_browse(m, key);
    return _osd_key_main(m, key);
}

// Instantané à reprendre par F3 : le dernier enregistré ou repris
// (state_last) s'il est encore dans la liste, sinon celui de plus grand nom
// parmi les .STA de la clé (ETAT0001.STA… : le plus récent) ; -1 : aucun
static inline int osd_state_latest(const osd_menu_t* m) {
    int best = -1;
    for (int i = 0; i < m->nfiles; i++) {
        if (m->files[i].kind != OSD_FILE_STA) continue;
        if (m->state_last[0] && !strcmp(m->files[i].name, m->state_last)) return i;
        if (m->files[i].vol != OSD_SRC_USB) continue;
        if (best < 0 || strcmp(m->files[i].name, m->files[best].name) > 0) best = i;
    }
    return best;
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

// Bandeau du haut
static inline void _osd_draw_header(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    osd_fill(s, 0, 0, 3, OSD_COLS, OSD_ATTR(OSD_WHITE, OSD_BLUE));
    osd_puts_big(s, 1, 3, "TELESTRAT", OSD_ATTR(OSD_WHITE, OSD_BLUE));
    osd_puts(s, 1, 24, "Oric", OSD_ATTR(OSD_YELLOW, OSD_BLUE), -1);
    snprintf(buf, sizeof(buf), "Neo6502  %s", m->version);
    osd_puts(s, 1, OSD_COLS - 3 - osd_strlen(buf), buf, OSD_ATTR(OSD_CYAN, OSD_BLUE), -1);
    for (int c = 0; c < OSD_COLS; c++) osd_putc(s, 3, c, OSD_HLINE, OSD_ATTR(OSD_CYAN, OSD_BLACK));

}

// Disquettes et cassette
static inline void _osd_draw_media(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    _osd_panel(s, 5, 2, 12, 56, OSD_FLOP_L, OSD_T("Disquettes", "Floppy disks"));
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
                osd_puts(s, row, 46, OSD_T("protégée", "read-only"), dim, -1);
            } else {
                osd_putc(s, row, 44, OSD_DOT, sel ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK);
                osd_puts(s, row, 46, OSD_T("écriture", "writable"), dim, -1);
            }
        } else {
            osd_puts(s, row, 9, OSD_T("— vide —", "— empty —"), dim, -1);
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
            osd_puts(s, row, 9, OSD_T("— pas de cassette —", "— no tape —"), dim, -1);
        }
    }

}

// Cartouches (banques 7 à 1)
static inline void _osd_draw_banks(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    _osd_panel(s, 5, 61, 17, 57, OSD_CART_L, OSD_T("Cartouches", "Cartridges"));
    const char* const origin[5] = {"", "RAM", "ROM", OSD_T("clé USB", "USB key"), OSD_T("réseau", "network")};
    for (int i = 0; i < 7; i++) {
        const int bank = 7 - i, row = 7 + 2 * i;
        const bool sel = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_BANK7 + i;
        _osd_item_bar(s, row, 63, 53, sel);
        const uint8_t base = sel ? OSD_SEL : OSD_PANEL, dim = sel ? OSD_SEL_DIM : OSD_PANEL_DIM;
        snprintf(buf, sizeof(buf), OSD_T("Banque %d", "Bank %d"), bank);
        osd_puts(s, row, 65, buf, sel ? OSD_SEL_ACC : OSD_PANEL_ACC, -1);
        if (m->bank_kind[bank] == OSD_BANK_EMPTY) {
            osd_puts(s, row, 76, OSD_T("— vide —", "— empty —"), dim, -1);
        } else {
            osd_puts(s, row, 76, m->bank[bank], base, 28);
            osd_puts(s, row, 106, origin[m->bank_kind[bank]], dim, -1);
        }
    }

}

// Clé USB et volume Réseau
static inline void _osd_draw_usb(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    _osd_panel(s, 18, 2, 4, 56, OSD_USB_L, OSD_T("Clé USB", "USB key"));
    if (m->usb_present) {
        int ndsk = 0, nrom = 0, ntap = 0, nsta = 0;
        for (int i = 0; i < m->nfiles; i++) {
            if (m->files[i].vol != OSD_SRC_USB) continue;
            if (m->files[i].kind == OSD_FILE_DSK) ndsk++;
            else if (m->files[i].kind == OSD_FILE_TAP) ntap++;
            else if (m->files[i].kind == OSD_FILE_STA) nsta++;
            else nrom++;
        }
        osd_puts(s, 19, 5, m->usb_label[0] ? m->usb_label : OSD_T("Clé montée", "Key mounted"), OSD_PANEL, m->net_present ? 24 : 50);
        snprintf(buf, sizeof(buf), "%d .dsk   %d .tap   %d .rom   %d .sta%s", ndsk, ntap, nrom, nsta,
                 m->truncated ? OSD_T("   (liste tronquée)", "   (list truncated)") : "");
        osd_puts(s, 20, 5, buf, OSD_PANEL_DIM, -1);
    } else {
        osd_puts(s, 19, 5, OSD_T("Aucune clé", "No key"), OSD_PANEL, -1);
        osd_puts(s, 20, 5, OSD_T("Brancher une clé FAT : .dsk, .tap, .rom, .sta à la racine", "Plug in a FAT key: .dsk, .tap, .rom, .sta at its root"),
                 OSD_PANEL_DIM, -1);
    }
    // Volume Réseau prêt : sur la ligne de la clé (son libellé écourté)
    if (m->net_present) {
        osd_putc(s, 19, 30, OSD_DOT, OSD_PANEL_OK);
        const int nl = osd_puts(s, 19, 32, OSD_T("Réseau", "Network"), OSD_PANEL_ACC, -1);
        if (m->net_label) osd_puts(s, 19, 33 + nl, m->net_label, OSD_PANEL_DIM, 55 - 33 - nl);
    }

}

// Périphériques : instantanés, imprimante, modem, cassette (Entrée : activer / couper)
static inline void _osd_draw_devices(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    _osd_panel(s, 23, 2, 5, 116, 0, OSD_T("Périphériques", "Devices"));
    {
        // Instantanés (Entrée : enregistrer, reprendre)
        const bool sel_s = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_STATE;
        _osd_item_bar(s, 24, 4, 55, sel_s);
        osd_puts(s, 24, 6, OSD_T("Instantanés", "Snapshots"), sel_s ? OSD_SEL_ACC : OSD_PANEL_ACC, -1);
        const uint8_t dim_s = sel_s ? OSD_SEL_DIM : OSD_PANEL_DIM;
        const int sn = osd_puts(s, 24, 18, OSD_T("enregistrer, reprendre", "save, restore"), sel_s ? OSD_SEL : OSD_PANEL, -1);
        if (m->state_last[0]) {
            snprintf(buf, sizeof(buf), "→ %s", m->state_last);
            osd_puts(s, 24, 19 + sn, buf, dim_s, 58 - 19 - sn);
        }
        // Réseau (TNFS) : serveur, Wi-Fi et mot de passe (Entrée : page de réglage)
        const bool sel_n = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_NET;
        _osd_item_bar(s, 24, 61, 56, sel_n);   // jusqu'à la colonne 116 (cadre en 117) : détail de 33 colonnes
        osd_puts(s, 24, 63, OSD_T("Réseau", "Network"), sel_n ? OSD_SEL_ACC : OSD_PANEL_ACC, -1);
        if (m->net_status && m->net_status->detail[0]) {
            // « ●Wi-Fi ●TNFS détail » : point vert (connecté), croix rouge (non connecté), point atténué (en cours)
            const uint8_t ok_n = sel_n ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK;
            const uint8_t ko_n = sel_n ? OSD_ATTR(OSD_RED, OSD_BLUE) : OSD_PANEL_ERR;
            const uint8_t dim_n = sel_n ? OSD_SEL_DIM : OSD_PANEL_DIM, txt_n = sel_n ? OSD_SEL : OSD_PANEL;
            const int8_t sign[2] = {m->net_status->wifi, m->net_status->tnfs};
            static const char* const what[2] = {"Wi-Fi", "TNFS"};
            for (int k = 0; k < 2; k++) {
                const int col = k ? 78 : 71;   // une colonne après « Network » (7 lettres)
                if (sign[k] == NET_SIGN_OK) osd_putc(s, 24, col, OSD_DOT, ok_n);
                else if (sign[k] == NET_SIGN_KO) osd_putc(s, 24, col, OSD_CROSS, ko_n);
                else if (sign[k] == NET_SIGN_BUSY) osd_putc(s, 24, col, '.', dim_n);
                osd_puts(s, 24, col + 1, what[k], sign[k] == NET_SIGN_OK ? ok_n : sign[k] == NET_SIGN_KO ? ko_n : dim_n, -1);
            }
            osd_puts(s, 24, 84, m->net_status->detail, txt_n, 33);
        } else {
            osd_puts(s, 24, 71, m->net_present ? OSD_T("serveur TNFS, Wi-Fi", "TNFS server, Wi-Fi")
                                               : OSD_T("serveur TNFS, Wi-Fi, mot de passe", "TNFS server, Wi-Fi, password"),
                     sel_n ? OSD_SEL : OSD_PANEL, 44);
        }
    }
    {
        const bool sel_p = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_PRINTER;
        const bool sel_m = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_MODEM;
        _osd_item_bar(s, 25, 4, 55, sel_p);
        _osd_item_bar(s, 25, 61, 55, sel_m);
        const uint8_t acc_p = sel_p ? OSD_SEL_ACC : OSD_PANEL_ACC, dim_p = sel_p ? OSD_SEL_DIM : OSD_PANEL_DIM;
        const uint8_t acc_m = sel_m ? OSD_SEL_ACC : OSD_PANEL_ACC, dim_m = sel_m ? OSD_SEL_DIM : OSD_PANEL_DIM;
        const uint8_t on_p = sel_p ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK;
        const uint8_t off_p = sel_p ? OSD_ATTR(OSD_RED, OSD_BLUE) : OSD_PANEL_ERR;
        const uint8_t on_m = sel_m ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK;
        const uint8_t off_m = sel_m ? OSD_ATTR(OSD_RED, OSD_BLUE) : OSD_PANEL_ERR;
        osd_puts(s, 25, 6, OSD_T("Imprimante", "Printer"), acc_p, -1);
        osd_putc(s, 25, 18, m->printer_on ? OSD_DOT : OSD_CROSS, m->printer_on ? on_p : off_p);
        const char* pstate = !m->printer_on ? OSD_T("coupée", "off") : m->printer_model ? m->printer_model : OSD_T("activée", "on");
        const int pn = osd_puts(s, 25, 20, pstate, m->printer_on ? on_p : off_p, -1);
        if (m->printer_on && m->printer_file[0]) {
            snprintf(buf, sizeof(buf), "→ %s", m->printer_file);
            osd_puts(s, 25, 21 + pn, buf, dim_p, 58 - 21 - pn);
        }
        osd_puts(s, 25, 63, "Modem", acc_m, -1);
        osd_putc(s, 25, 70, m->modem_on ? OSD_DOT : OSD_CROSS, m->modem_on ? on_m : off_m);
        osd_puts(s, 25, 72, m->modem_on ? OSD_T("activé", "on") : OSD_T("coupé", "off"), m->modem_on ? on_m : off_m, -1);
        if (m->modem_state) {
            snprintf(buf, sizeof(buf), "PicoWiFiModemUSB : %s", m->modem_state);
            osd_puts(s, 25, 80, buf, dim_m, 35);
        }
        // Cassette : lecture accélérée, moteur
        const bool sel_t = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_TURBO;
        const bool sel_o = m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_MOTOR;
        _osd_item_bar(s, 26, 4, 55, sel_t);
        _osd_item_bar(s, 26, 61, 55, sel_o);
        const uint8_t acc_t = sel_t ? OSD_SEL_ACC : OSD_PANEL_ACC, dim_t = sel_t ? OSD_SEL_DIM : OSD_PANEL_DIM;
        const uint8_t acc_o = sel_o ? OSD_SEL_ACC : OSD_PANEL_ACC, dim_o = sel_o ? OSD_SEL_DIM : OSD_PANEL_DIM;
        const uint8_t on_t = sel_t ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK;
        const uint8_t off_t = sel_t ? OSD_ATTR(OSD_RED, OSD_BLUE) : OSD_PANEL_ERR;
        const uint8_t on_o = sel_o ? OSD_ATTR(OSD_GREEN, OSD_BLUE) : OSD_PANEL_OK;
        osd_puts(s, 26, 6, OSD_T("Cassette", "Tape"), acc_t, -1);
        osd_putc(s, 26, 18, m->tape_turbo ? OSD_DOT : OSD_CROSS, m->tape_turbo ? on_t : off_t);
        osd_puts(s, 26, 20, m->tape_turbo ? OSD_T("rapide", "fast") : OSD_T("vitesse réelle", "real speed"), m->tape_turbo ? on_t : off_t, -1);
        if (m->tape_turbo) osd_puts(s, 26, 28, OSD_T("(CLOAD du BASIC 1.1)", "(BASIC 1.1 CLOAD)"), dim_t, -1);
        osd_puts(s, 26, 63, OSD_T("Moteur", "Motor"), acc_o, -1);
        osd_putc(s, 26, 70, OSD_DOT, on_o);
        osd_puts(s, 26, 72, m->tape_motor_always ? OSD_T("toujours en marche", "always on") : OSD_T("relais (PB6)", "relay (PB6)"), on_o, -1);
        if (m->tape_motor_always) osd_puts(s, 26, 91, OSD_T("câble sans relais", "cable without relay"), dim_o, -1);
    }

}

// Boutons d'action et message
static inline void _osd_draw_actions(const osd_menu_t* m, osd_surface_t* s) {
    const int btn_row = 29;
    const char* const labels[3] = {OSD_T("Redémarrer (RESET)", "Restart (RESET)"),
                                   OSD_T("Enregistrer la configuration", "Save the configuration"), OSD_T("Reprendre", "Resume")};
    const int cols[3] = {2, 40, 82}, widths[3] = {34, 38, 36};
    for (int i = 0; i < 3; i++)
        _osd_button(s, btn_row, cols[i], widths[i], labels[i], m->page == OSD_PAGE_MAIN && m->cursor == OSD_ITEM_RESET + i);

    // Message
    if (m->message[0]) {
        osd_putc(s, 31, 3, m->message_error ? OSD_CROSS : OSD_CHECK,
                 OSD_ATTR(m->message_error ? OSD_RED : OSD_GREEN, OSD_BLACK));
        osd_puts(s, 31, 5, m->message, OSD_ATTR(m->message_error ? OSD_RED : OSD_YELLOW, OSD_BLACK), -1);
    }
}

// Pied : aide des touches
static inline void _osd_draw_footer(const osd_menu_t* m, osd_surface_t* s) {
    osd_fill(s, 32, 0, 2, OSD_COLS, OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER));
    const uint8_t key = OSD_ATTR(OSD_BLACK, OSD_CYAN), txt = OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER);
    const char* const help_main[][2] = {{OSD_T(" Flèches ", " Arrows "), OSD_T("choisir", "select")},
                                       {OSD_T(" Entrée ", " Enter "), OSD_T("ouvrir, activer", "open, toggle")},
                                       {OSD_T(" Suppr ", " Del "), OSD_T("éjecter, d'origine", "eject, original")},
                                       {OSD_T(" Échap ", " Esc "), OSD_T("reprendre", "resume")}};
    const char* const help_browse[][2] = {{OSD_T(" Flèches ", " Arrows "), OSD_T("choisir", "select")},
                                         {OSD_T(" Entrée ", " Enter "), OSD_T("valider", "confirm")},
                                         {OSD_T(" Lettre ", " Letter "), OSD_T("aller à", "jump to")},
                                         {OSD_T(" Échap ", " Esc "), OSD_T("retour", "back")}};
    const char* const help_net[][2] = {{OSD_T(" Flèches ", " Arrows "), OSD_T("champ", "field")},
                                      {OSD_T(" Entrée ", " Enter "), OSD_T("suivant", "next")},
                                      {OSD_T(" Retour ", " Backspace "), OSD_T("effacer", "erase")},
                                      {OSD_T(" Échap ", " Esc "), OSD_T("abandonner", "cancel")}};
    const char* const(*help)[2] = m->page == OSD_PAGE_MAIN ? help_main : m->page == OSD_PAGE_NET ? help_net : help_browse;
    int c = 3;
    for (int i = 0; i < 4; i++) {
        c += osd_puts(s, 32, c, help[i][0], key, -1) + 1;
        c += osd_puts(s, 32, c, help[i][1], txt, -1) + 3;
    }
    osd_puts(s, 32, OSD_COLS - 22, OSD_T("F1 : ouvrir ce menu", "F1: open this menu"), OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER), -1);
    // Langue (page principale) : la touche L bascule français / anglais
    if (m->page == OSD_PAGE_MAIN)
        osd_puts(s, 33, OSD_COLS - 22, OSD_T("L : English", "L: français"), OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER), -1);
}

// Cadre du sélecteur de fichiers ou du choix de la source, par-dessus la page
#define OSD_BR_TOP   6
#define OSD_BR_LEFT  22
#define OSD_BR_WIDTH 76

static inline void _osd_draw_browser_frame(const osd_menu_t* m, osd_surface_t* s) {
    char buf[96];
    const int item = m->browse_target;
    const bool drive = _osd_item_is_drive(item), tape = item == OSD_ITEM_TAPE, state = item == OSD_ITEM_STATE,
               boot = item == OSD_ITEM_BOOT;
    if (drive) snprintf(buf, sizeof(buf), OSD_T("Disquette pour le lecteur %c", "Floppy disk for drive %c"), 'A' + item);
    else if (tape) snprintf(buf, sizeof(buf), "%s", OSD_T("Cassette (la même : rembobinée)", "Tape (the same one: rewound)"));
    else if (state) snprintf(buf, sizeof(buf), "%s", OSD_T("Instantanés (reprendre : la machine revient à cet instant)", "Snapshots (restore: the machine goes back to that moment)"));
    else if (boot) snprintf(buf, sizeof(buf), "%s", OSD_T("Démarrer sur…", "Start with…"));
    else snprintf(buf, sizeof(buf), OSD_T("Cartouche pour la banque %d", "Cartridge for bank %d"), _osd_item_bank(item));
    const bool source = m->page == OSD_PAGE_SOURCE, net = !source && m->browse_vol == OSD_SRC_NET;
    if (net || source) {
        const size_t l = strlen(buf);
        snprintf(buf + l, sizeof(buf) - l, "%s", net ? OSD_T(" — réseau", " — network") : OSD_T(" — source ?", " — source?"));
    }
    const int top = OSD_BR_TOP, left = OSD_BR_LEFT, width = OSD_BR_WIDTH, height = source ? 6 : OSD_BROWSE_VISIBLE + 4;
    osd_fill(s, top + 1, left + 2, height, width, OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER));  // ombre
    osd_fill(s, top, left, height, width, OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_frame(s, top, left, height, width, OSD_ATTR(OSD_YELLOW, OSD_BLACK), NULL, 0);
    osd_putc(s, top, left + 2, ' ', OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    const uint8_t icon = drive ? OSD_FLOP_L : tape ? OSD_TAPE_L : state ? OSD_USB_L : OSD_CART_L;
    osd_putc(s, top, left + 3, icon, OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    osd_putc(s, top, left + 4, (uint8_t)(icon + 1), OSD_ATTR(OSD_YELLOW, OSD_BLACK));
    const int tl = osd_puts(s, top, left + 6, buf, OSD_ATTR(OSD_WHITE, OSD_BLACK), -1);
    osd_putc(s, top, left + 6 + tl, ' ', OSD_ATTR(OSD_YELLOW, OSD_BLACK));
}

// Choix de la source : Clé USB / Réseau
static inline void _osd_draw_source_rows(const osd_menu_t* m, osd_surface_t* s) {
    const int top = OSD_BR_TOP, left = OSD_BR_LEFT, width = OSD_BR_WIDTH;
    for (int k = 0; k < 2; k++) {
        const int row = top + 2 + k;
        const bool sel = k == m->source_cursor;
        const uint8_t base = sel ? OSD_ATTR(OSD_BLACK, OSD_CYAN) : OSD_ATTR(OSD_WHITE, OSD_BLACK);
        const uint8_t dim = sel ? OSD_ATTR(OSD_BLUE, OSD_CYAN) : OSD_ATTR(OSD_CYAN, OSD_BLACK);
        osd_fill(s, row, left + 2, 1, width - 5, base);
        osd_puts(s, row, left + 4, k ? OSD_T("Réseau", "Network") : OSD_T("Clé USB", "USB key"), base, -1);
        const char* info = k ? m->net_label
                           : !m->usb_present ? OSD_T("aucune clé", "no key")
                           : m->usb_label[0] ? m->usb_label
                                             : OSD_T("Clé montée", "Key mounted");
        if (info) osd_puts(s, row, left + 16, info, dim, width - 22);
    }
}

// Liste du sélecteur de fichiers
static inline void _osd_draw_file_rows(const osd_menu_t* m, osd_surface_t* s) {
    const int top = OSD_BR_TOP, left = OSD_BR_LEFT, width = OSD_BR_WIDTH;
    const int item = m->browse_target;
    const bool drive = _osd_item_is_drive(item), tape = item == OSD_ITEM_TAPE, state = item == OSD_ITEM_STATE,
               boot = item == OSD_ITEM_BOOT, net = m->browse_vol == OSD_SRC_NET;
    const int n = m->browse_count + 1;
    for (int k = 0; k < OSD_BROWSE_VISIBLE && m->browse_scroll + k < n; k++) {
        const int idx = m->browse_scroll + k, row = top + 2 + k;
        const bool sel = idx == m->browse_cursor;
        const uint8_t base = sel ? OSD_ATTR(OSD_BLACK, OSD_CYAN) : OSD_ATTR(OSD_WHITE, OSD_BLACK);
        const uint8_t dim = sel ? OSD_ATTR(OSD_BLUE, OSD_CYAN) : OSD_ATTR(OSD_CYAN, OSD_BLACK);
        osd_fill(s, row, left + 2, 1, width - 5, base);
        if (idx == 0) {
            osd_puts(s, row, left + 4,
                     drive   ? OSD_T("Éjecter la disquette", "Eject the disk")
                     : tape  ? OSD_T("Éjecter la cassette", "Eject the tape")
                     : state ? OSD_T("Enregistrer un nouvel instantané (ETATnnnn.STA)", "Save a new snapshot (ETATnnnn.STA)")
                     : boot  ? OSD_T("Configuration de la clé (TELESTRA.CFG)", "Configuration of the key (TELESTRA.CFG)")
                             : OSD_T("Contenu d'origine de la banque", "Original contents of the bank"),
                     dim,
                     -1);
            continue;
        }
        const int entry = m->browse_list[idx - 1];
        if (entry <= -100) {  // profil de démarrage
            osd_puts(s, row, left + 4, m->profile[-100 - entry], base, width - 8);
            continue;
        }
        if (entry < 0) {  // ROM intégrée
            osd_puts(s, row, left + 4, m->builtin[-2 - entry], base, 50);
            osd_puts(s, row, left + width - 13, OSD_T("intégrée", "built-in"), dim, -1);
            continue;
        }
        const osd_file_t* f = &m->files[entry];
        osd_puts(s, row, left + 4, _osd_shown_name(f->name), base, 50);
        char size[16];
        _osd_size(size, sizeof(size), f->size);
        osd_puts(s, row, left + width - 5 - osd_strlen(size), size, dim, -1);
        // Image déjà dans un autre lecteur (la choisir est refusé)
        for (int d = 0; drive && d < 4; d++) {
            if (d == item - OSD_ITEM_DRIVE0 || strcmp(m->drive[d], f->name)) continue;
            char tag[8];
            snprintf(tag, sizeof(tag), OSD_T("en %c", "in %c"), 'A' + d);
            osd_puts(s, row, left + width - 18, tag, sel ? OSD_ATTR(OSD_RED, OSD_CYAN) : OSD_ATTR(OSD_YELLOW, OSD_BLACK), -1);
        }
    }
    if (m->browse_count == 0)
        osd_puts(s, top + 4, left + 4,
                 net     ? (drive ? OSD_T("Aucune image .dsk sur le réseau", "No .dsk image on the network")
                            : tape ? OSD_T("Aucune cassette .tap sur le réseau", "No .tap tape on the network")
                                   : OSD_T("Aucun fichier de ce type sur le réseau", "No file of this type on the network"))
                 : drive ? OSD_T("Aucune image .dsk sur la clé", "No .dsk image on the key")
                 : tape  ? OSD_T("Aucune cassette .tap sur la clé", "No .tap tape on the key")
                 : state ? OSD_T("Aucun instantané .sta sur la clé", "No .sta snapshot on the key")
                         : OSD_T("Aucune image .rom sur la clé", "No .rom image on the key"),
                 OSD_ATTR(OSD_RED, OSD_BLACK), -1);
    // Liste incomplète : le fichier cherché peut manquer
    if (m->truncated) {
        char note[48];
        snprintf(note, sizeof(note), OSD_T("Liste tronquée : %d fichiers au plus", "List truncated: %d files at most"), OSD_MENU_FILES);
        osd_puts(s, top + OSD_BROWSE_VISIBLE + 2, left + 4, note, OSD_ATTR(OSD_YELLOW, OSD_BLACK), -1);
    }
    // Barre de défilement
    if (n > OSD_BROWSE_VISIBLE) {
        const int bar = left + width - 2;
        for (int k = 0; k < OSD_BROWSE_VISIBLE; k++) osd_putc(s, top + 2 + k, bar, OSD_SHADE, OSD_ATTR(OSD_BLUE, OSD_BLACK));
        const int thumb = m->browse_scroll * (OSD_BROWSE_VISIBLE - 1) / (n - OSD_BROWSE_VISIBLE);
        osd_putc(s, top + 2 + thumb, bar, OSD_FULL, OSD_ATTR(OSD_CYAN, OSD_BLACK));
    }
}

static inline void osd_menu_draw(const osd_menu_t* m, osd_surface_t* s) {
    osd_clear(s, OSD_BG);
    _osd_draw_header(m, s);
    _osd_draw_media(m, s);
    _osd_draw_banks(m, s);
    _osd_draw_usb(m, s);
    _osd_draw_devices(m, s);
    _osd_draw_actions(m, s);
    _osd_draw_footer(m, s);
    if (m->page == OSD_PAGE_MAIN) return;
    if (m->page == OSD_PAGE_NET) {   // page Réseau, par-dessus
        osd_netcfg_draw(s, &m->netcfg, 8, (OSD_COLS - OSD_NETCFG_COLS) / 2);
        return;
    }
    // Sélecteur de fichiers ou choix de la source, par-dessus
    _osd_draw_browser_frame(m, s);
    if (m->page == OSD_PAGE_SOURCE) _osd_draw_source_rows(m, s);
    else _osd_draw_file_rows(m, s);
}

// Bandeau de la cassette, incrusté sous l'image du Telestrat pendant que le
// moteur tourne : icône, « Lecture » ou « Écriture », nom, barre, pour cent
// percent < 0 : longueur inconnue (STORE d'un tableau), octets écrits affichés
static inline void osd_tape_banner_ex(osd_row_t* r, const char* label, const char* name, int percent, uint32_t bytes) {
    const uint8_t base = OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER);
    osd_row_clear(r, base);
    const uint8_t acc = OSD_ATTR(OSD_YELLOW, OSD_BLUE | OSD_DITHER);
    r->ch[16] = label[0] == 'L' ? OSD_TRI_R : OSD_DOT;  // lecture : ▶ ; écriture : ●
    r->attr[16] = label[0] == 'L' ? acc : OSD_ATTR(OSD_RED, OSD_BLUE | OSD_DITHER);
    r->ch[18] = OSD_TAPE_L;
    r->ch[19] = OSD_TAPE_R;
    r->attr[18] = r->attr[19] = acc;
    // label : « Lecture » ou « Écriture » (repères des appelants), affiché dans la langue du menu
    osd_row_puts(r, 21, label[0] == 'L' ? OSD_T("Lecture", "Playing") : OSD_T("Écriture", "Recording"), acc);
    char buf[64];
    snprintf(buf, sizeof(buf), "%.34s", name);
    osd_row_puts(r, 30, buf, base);
    const int bar = 66, cells = 30;
    if (percent < 0) {
        snprintf(buf, sizeof(buf), OSD_T("%lu octets", "%lu bytes"), (unsigned long)bytes);
        osd_row_puts(r, bar, buf, acc);
        return;
    }
    for (int i = 0; i < cells; i++) {
        r->ch[bar + i] = i * 100 / cells < percent ? OSD_FULL : OSD_SHADE;
        r->attr[bar + i] = OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER);
    }
    snprintf(buf, sizeof(buf), "%3d %%", percent);
    osd_row_puts(r, bar + cells + 2, buf, acc);
}

static inline void osd_tape_banner(osd_row_t* r, const char* label, const char* name, int percent) {
    osd_tape_banner_ex(r, label, name, percent, 0);
}

// Bandeau du volume (touches multimédia du clavier) : jauge de max cases, ou « Son coupé »
static inline void osd_volume_banner(osd_row_t* r, int level, int max, bool muted) {
    const uint8_t base = OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER);
    const uint8_t acc = OSD_ATTR(OSD_YELLOW, OSD_BLUE | OSD_DITHER);
    osd_row_clear(r, base);
    osd_row_puts(r, 21, "Volume", acc);
    if (muted) {
        r->ch[30] = OSD_CROSS;
        r->attr[30] = OSD_ATTR(OSD_RED, OSD_BLUE | OSD_DITHER);
        osd_row_puts(r, 32, OSD_T("Son coupé", "Muted"), base);
        return;
    }
    for (int i = 0; i < max; i++) {
        r->ch[30 + 2 * i] = i < level ? OSD_FULL : OSD_SHADE;
        r->attr[30 + 2 * i] = OSD_ATTR(OSD_CYAN, OSD_BLUE | OSD_DITHER);
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", level, max);
    osd_row_puts(r, 30 + 2 * max + 1, buf, acc);
}
