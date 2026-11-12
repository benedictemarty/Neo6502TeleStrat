// test_revue_firmware.c — vérifications de la revue de code, zone F (firmware
// RP2040 et OSD, docs/CRITIQUE.md F-01 à F-35) : la logique qui ne dépend pas
// de la carte, essayée sur PC (src/osd/osd_config.h, osd_menu.h, rom_pool.h,
// rom_builtin.h). Le code de platforms/rp2040/src/telestrat.c lui-même n'est
// vérifié que par sa compilation ARM sans avertissement (make uf2).
//
// Programme séparé de test_telestrat.c, même environnement.

#define _POSIX_C_SOURCE 200809L
#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chips/chips_common.h"
#include "chips/w65c02cpu.h"
#include "chips/via6522.h"
#include "chips/ay38910psg.h"
#include "chips/kbd.h"
#include "chips/clk.h"
#include "devices/wd1793.h"
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "devices/minitel_port.h"
#include "devices/hayes_line.h"
#include "devices/modem_mux.h"
#include "devices/drive_set.h"
#include "devices/byte_fifo.h"
#include "devices/printer_fx80.h"
#include "devices/plotter_mcp40.h"
#include "osd/osd_menu.h"
#include "osd/osd_config.h"
#include "systems/telestrat.h"
#include "systems/telestrat_state.h"
#include "osd/rom_pool.h"
#include "roms/telestrat_roms.h"
#include "osd/rom_builtin.h"
#include "telestrat_frame.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        checks++;                                         \
        if (!(cond)) {                                    \
            failures++;                                   \
            printf("ÉCHEC %s:%d : ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

/*-- F-03 : TELESTRA.CFG long, lignes longues ---------------------------------*/

// Fichier texte en mémoire lu par osd_config_line
typedef struct {
    const char* data;
    uint32_t size;
    int reads;
} memfile_t;

static bool memfile_read(void* ctx, uint32_t pos, uint8_t* buf, uint32_t len) {
    memfile_t* f = ctx;
    if (pos + len > f->size) return false;
    memcpy(buf, f->data + pos, len);
    f->reads++;
    return true;
}

static bool read_fail(void* ctx, uint32_t pos, uint8_t* buf, uint32_t len) {
    (void)ctx;
    (void)pos;
    (void)buf;
    (void)len;
    return false;
}

static void test_config_merge_long(void) {
    static char old[6000], out[8000];
    const char* drives[4] = {"STRATSED.DSK", NULL, NULL, NULL};
    const char* banks[8] = {NULL};
    // Fichier de plus de 2047 octets : 12 lignes de 250 caractères, dont un profil
    size_t n = 0;
    char longval[260];
    for (int k = 0; k < 250; k++) longval[k] = (char)('a' + k % 26);
    longval[250] = 0;
    n += (size_t)snprintf(old + n, sizeof(old) - n, "a=VIEUX.DSK\n");
    for (int k = 0; k < 12; k++) n += (size_t)snprintf(old + n, sizeof(old) - n, "profil=P%d;bank7=%s\n", k, longval);
    n += (size_t)snprintf(old + n, sizeof(old) - n, "dial=go.minipavi.fr:516  \r\n");
    CHECK(n > 2047 + 1000, "fichier d'essai plus long que l'ancien tampon (%zu octets)", n);
    size_t len = osd_config_merge_ex(old, drives, banks, NULL, out, sizeof(out));
    CHECK(len > 0 && len == strlen(out), "fusion d'un fichier de %zu octets réussie", n);
    // Rien n'est tronqué : chaque ligne de profil ressort entière
    int whole = 0;
    for (int k = 0; k < 12; k++) {
        char want[300];
        snprintf(want, sizeof(want), "profil=P%d;bank7=%s\n", k, longval);
        whole += strstr(out, want) != NULL;
    }
    CHECK(whole == 12, "12 lignes de plus de 127 caractères intactes (%d)", whole);
    CHECK(strstr(out, "dial=go.minipavi.fr:516\n") && !strstr(out, "VIEUX") && strstr(out, "a=STRATSED.DSK\n"),
          "ligne finale gardée (espaces de fin retirés), ancienne ligne a= remplacée");
    // Un tampon trop petit est refusé (0), jamais rempli à moitié
    CHECK(osd_config_merge_ex(old, drives, banks, NULL, out, n / 2) == 0, "tampon trop petit : refus (0)");
    CHECK(osd_config_merge_ex(old, drives, banks, NULL, out, 100) == 0, "tampon de 100 octets : refus (0)");
    // Ligne longue qui commence par une clé du menu : remplacée, pas recopiée
    snprintf(old, sizeof(old), "bank5=%s\nbank4=garde.rom\n", longval);
    const char* banks2[8] = {NULL, NULL, NULL, NULL, NULL, "neuf.rom", NULL, NULL};
    len = osd_config_merge_ex(old, drives, banks2, NULL, out, sizeof(out));
    CHECK(len > 0 && !strstr(out, longval) && strstr(out, "bank5=neuf.rom\n"), "ligne bank5= longue remplacée");
}

static void test_config_line(void) {
    // Lignes de 10, 200 et 5 caractères (CRLF, espaces de fin), dernière sans fin de ligne
    char text[600];
    char l200[201];
    memset(l200, 'x', 200);
    l200[200] = 0;
    int tn = snprintf(text, sizeof(text), "0123456789  \r\n%s\nabcde", l200);
    memfile_t f = {text, (uint32_t)tn, 0};
    uint32_t pos = 0;
    char line[160];
    bool cut;
    CHECK(osd_config_line(memfile_read, &f, f.size, &pos, line, sizeof(line), &cut) && !cut && !strcmp(line, "0123456789"),
          "première ligne : fin de ligne et espaces retirés (« %s »)", line);
    CHECK(osd_config_line(memfile_read, &f, f.size, &pos, line, sizeof(line), &cut) && cut && strlen(line) == 159,
          "ligne de 200 caractères : coupée à 159 et signalée (%zu)", strlen(line));
    CHECK(osd_config_line(memfile_read, &f, f.size, &pos, line, sizeof(line), &cut) && !cut && !strcmp(line, "abcde"),
          "la suite de la ligne longue est sautée, pas lue comme une ligne (« %s »)", line);
    CHECK(!osd_config_line(memfile_read, &f, f.size, &pos, line, sizeof(line), &cut), "fin du fichier");
    // Lecture en morceaux : peu d'appels de lecture
    CHECK(f.reads <= 12, "lecture par morceaux de 32 octets (%d lectures pour %d octets)", f.reads, tn);
    // Ligne vide
    memfile_t g = {"\n\nx\n", 4, 0};
    pos = 0;
    CHECK(osd_config_line(memfile_read, &g, g.size, &pos, line, sizeof(line), &cut) && !line[0] &&
              osd_config_line(memfile_read, &g, g.size, &pos, line, sizeof(line), &cut) && !line[0] &&
              osd_config_line(memfile_read, &g, g.size, &pos, line, sizeof(line), &cut) && !strcmp(line, "x"),
          "lignes vides");
    // Lecture qui échoue
    memfile_t h = {"abc\n", 4, 0};
    pos = 0;
    CHECK(!osd_config_line(read_fail, &h, h.size, &pos, line, sizeof(line), &cut), "lecture en échec : arrêt");
}

static void test_config_port(void) {
    int p = 0;
    CHECK(osd_config_port("3615", &p) && p == 3615 && osd_config_port("65535", &p) && p == 65535 && osd_config_port("1", &p) &&
              p == 1,
          "ports valides");
    p = -7;
    CHECK(!osd_config_port("0", &p) && !osd_config_port("65536", &p) && !osd_config_port("", &p) &&
              !osd_config_port("12ab", &p) && !osd_config_port("-5", &p) && !osd_config_port("99999999999999999999", &p) && p == -7,
          "ports invalides refusés, valeur gardée (F-22)");
}

/*-- F-04, F-13, F-14 : vérification préalable des profils --------------------*/

static telestrat_t sys;

// Pool de la variante standard (4 ROM intégrées : banques 2, 3, 6, 7, plus 1
// emplacement supplémentaire) ou de la RAM 64 Ko (2 ROM : 6, 7, aucun de plus)
static uint8_t slots[5][OSD_BANK_BYTES];
static rom_pool_t pool_make(bool ram64k) {
    rom_pool_t p;
    telestrat_desc_t d = {0};
    d.banks[0].type = TELESTRAT_BANK_RAM;
    if (ram64k) {
        rom_pool_init(&p, slots, 2);
        for (int i = 1; i <= 4; i++) d.banks[i].type = TELESTRAT_BANK_RAM;
    } else {
        rom_pool_init(&p, slots, 5);
        d.banks[2] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&p, 2, telestrat_teleass)};
        d.banks[3] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&p, 3, telestrat_telematic)};
    }
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&p, 6, telestrat_hyperbas)};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&p, 7, telestrat_telemon24)};
    telestrat_init(&sys, &d);
    return p;
}

// Contenu des banques 1 à 7 : l'état d'origine est-il intact ?
static bool banks_pristine(const rom_pool_t* p) {
    for (int b = 1; b < 8; b++)
        if (p->name[b][0]) return false;
    return sys.bank_rd[7] && !memcmp(sys.bank_rd[7], telestrat_telemon24, OSD_BANK_BYTES);
}

static int probe_calls;
static size_t probe_size = 0x4000;
static bool probe_missing;
static bool probe_cb(void* ctx, const char* name, size_t* size) {
    (void)ctx;
    probe_calls++;
    if (probe_missing || !strcmp(name, "absent.rom")) return false;
    *size = !strcmp(name, "gros.rom") ? 0x8000 : probe_size;
    return true;
}

static bool load_ok_cb(void* ctx, int bank, const char* name, const char** err) {
    (void)ctx;
    (void)bank;
    (void)name;
    (void)err;
    return true;
}

static bool load_fail_cb(void* ctx, int bank, const char* name, const char** err) {
    (void)ctx;
    (void)bank;
    (void)name;
    *err = "fichier illisible";
    return false;
}

static void test_profile_check(void) {
    const char* err = "";
    // Variante standard : STRATORIC (banques 7, 6 et 5) tient (5 : emplacement supplémentaire)
    rom_pool_t std = pool_make(false);
    const int stratoric = rom_profile_find("stratoric"), atmos = rom_profile_find("atmos"), telestrat = rom_profile_find("telestrat");
    CHECK(rom_profile_check(&std, stratoric, &err), "standard : profil stratoric réalisable (%s)", err);
    CHECK(rom_profile_check(&std, atmos, &err) && rom_profile_check(&std, telestrat, &err), "standard : atmos et telestrat réalisables");
    CHECK(!rom_profile_check(&std, 99, &err) && strstr(err, "profil inconnu") && !rom_profile_check(&std, -1, &err),
          "profil hors liste : refusé avec une cause");
    // RAM 64 Ko : aucun emplacement pour la banque 5 de STRATORIC
    rom_pool_t r64 = pool_make(true);
    err = "";
    CHECK(!rom_profile_check(&r64, stratoric, &err) && strstr(err, "plus de place"), "RAM 64 Ko : stratoric refusé, cause « %s »", err);
    CHECK(rom_profile_check(&r64, atmos, &err) && rom_profile_check(&r64, telestrat, &err), "RAM 64 Ko : atmos (banque 7) et telestrat tiennent");
    // Refus avant toute modification : banques d'origine
    CHECK(!rom_profile_apply(&r64, &sys, stratoric, &err) && strstr(err, "plus de place") && banks_pristine(&r64),
          "RAM 64 Ko : stratoric refusé sans toucher aux banques (F-04)");
    // Profil réussi : la cartouche complète est en place
    r64 = pool_make(true);
    CHECK(rom_profile_apply(&r64, &sys, atmos, &err) && !strcmp(r64.name[7], "@atmos"), "RAM 64 Ko : atmos appliqué (%s)", err);
    std = pool_make(false);
    CHECK(rom_profile_apply(&std, &sys, stratoric, &err) && !strcmp(std.name[7], "@stratoric") && !strcmp(std.name[5], "@basic10"),
          "standard : stratoric appliqué (%s)", err);
    // Les profils intégrés ont tous un libellé non vide et une cause quand ils échouent (F-13)
    err = "";
    CHECK(!rom_profile_apply(&std, &sys, 99, &err) && err[0], "échec : la cause n'est jamais vide");
}

static void test_user_profile_check(void) {
    const char* err = "";
    rom_pool_t std = pool_make(false);
    probe_calls = 0;
    probe_missing = false;
    probe_size = 0x4000;
    CHECK(rom_user_profile_check(&std, "Jeux;bank7=@atmos;bank4=jeu.rom", probe_cb, NULL, &err) && probe_calls == 1,
          "profil de la clé valide : fichier sondé (%s)", err);
    // Fichier absent : refusé avant toute modification, avec le nom
    CHECK(!rom_user_profile_check(&std, "X;bank4=absent.rom", probe_cb, NULL, &err) && strstr(err, "absent.rom") &&
              strstr(err, "illisible"),
          "fichier absent nommé dans la cause (%s)", err);
    // Taille refusée
    CHECK(!rom_user_profile_check(&std, "X;bank4=gros.rom", probe_cb, NULL, &err) && strstr(err, "taille"), "taille invalide (%s)", err);
    // ROM intégrée inconnue
    CHECK(!rom_user_profile_check(&std, "X;bank7=@amiga", probe_cb, NULL, &err) && strstr(err, "inconnue"), "ROM intégrée inconnue (%s)", err);
    // Trop d'emplacements : le standard n'en a qu'un de plus ; banques 1, 4 et 5 sans ROM intégrée
    CHECK(!rom_user_profile_check(&std, "X;bank1=a.rom;bank4=b.rom;bank5=c.rom", probe_cb, NULL, &err) && strstr(err, "plus de place"),
          "trois cartouches pour un emplacement : refusé (%s)", err);
    CHECK(rom_user_profile_check(&std, "X;bank6=a.rom;bank7=b.rom;bank2=c.rom;bank3=d.rom;bank5=e.rom", probe_cb, NULL, &err),
          "banques 2, 3, 6, 7 (ROM intégrées) + 5 : tient (%s)", err);
    // RAM 64 Ko : aucune cartouche en banque 5 (F-04)
    rom_pool_t r64 = pool_make(true);
    CHECK(!rom_user_profile_check(&r64, "X;bank5=a.rom", probe_cb, NULL, &err) && strstr(err, "plus de place"),
          "RAM 64 Ko : banque 5 sans emplacement (%s)", err);
    CHECK(rom_user_profile_check(&r64, "X;bank7=a.rom;bank6=b.rom", probe_cb, NULL, &err), "RAM 64 Ko : banques 6 et 7 tiennent");
    // Sans sonde : seule la forme et la place sont vérifiées
    CHECK(rom_user_profile_check(&std, "X;bank4=quelconque.rom", NULL, NULL, &err), "sans sonde, nom non vérifié");
    // Application : refus complet avant modification (F-04)
    std = pool_make(false);
    CHECK(!rom_user_profile_apply(&std, &sys, "X;bank7=@atmos;bank1=a.rom;bank4=b.rom;bank5=c.rom", load_ok_cb, NULL, &err) &&
              strstr(err, "plus de place") && std.name[7][0] == 0 && !memcmp(sys.bank_rd[7], telestrat_telemon24, OSD_BANK_BYTES),
          "application refusée avant de toucher à la banque 7 (%s)", err);
    // Échec de lecture en cours de route : le résultat est faux, l'appelant sait que des banques ont bougé
    std = pool_make(false);
    CHECK(!rom_user_profile_apply(&std, &sys, "X;bank7=@atmos;bank4=jeu.rom", load_fail_cb, NULL, &err) && strstr(err, "illisible") &&
              !strcmp(std.name[7], "@atmos"),
          "lecture échouée après la banque 7 : false, cause « %s »", err);
}

static void test_user_profile_item_length(void) {
    const char* err = "";
    rom_pool_t std = pool_make(false);
    // Élément de 63 caractères (« bank4= » compris) : accepté ; 64 : refusé (F-14)
    char v[200], name63[80], name64[80];
    snprintf(name63, sizeof(name63), "%.*s", 63 - 6 - 4, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    strcat(name63, ".rom");
    snprintf(name64, sizeof(name64), "%.*s", 64 - 6 - 4, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    strcat(name64, ".rom");
    snprintf(v, sizeof(v), "X;bank4=%s", name63);
    CHECK(strlen(v + 2) == 63 && rom_user_profile_check(&std, v, probe_cb, NULL, &err), "élément de 63 caractères accepté (%s)", err);
    snprintf(v, sizeof(v), "X;bank4=%s", name64);
    CHECK(strlen(v + 2) == 64 && !rom_user_profile_check(&std, v, probe_cb, NULL, &err) && strstr(err, "trop long"),
          "élément de 64 caractères refusé avec une cause (%s)", err);
    // Même un élément trop long qui ne serait pas une banque est signalé, et rien n'est modifié
    std = pool_make(false);
    snprintf(v, sizeof(v), "X;bank7=@atmos;%s", "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");
    CHECK(!rom_user_profile_apply(&std, &sys, v, load_ok_cb, NULL, &err) && strstr(err, "trop long") && banks_pristine(&std),
          "profil à élément trop long : refusé sans toucher aux banques");
}

static void test_builtin_atomic(void) {
    const char* err = "";
    // Deux ROM intégrées seulement (6, 7) et aucun emplacement libre : STRATORIC (7, 6, 5) manque de place
    rom_pool_t r64 = pool_make(true);
    const rom_builtin_t* s = rom_builtin_find("@stratoric");
    CHECK(s && !rom_pool_load_builtin(&r64, &sys, 7, s, &err) && strstr(err, "plus de place"), "cartouche complète sans place : refusée (%s)", err);
    CHECK(banks_pristine(&r64) && !memcmp(sys.bank_rd[6], telestrat_hyperbas, OSD_BANK_BYTES),
          "refus avant tout chargement : banques 7 et 6 intactes (F-04)");
    CHECK(!rom_pool_load_builtin(&r64, &sys, 0, s, &err) && strstr(err, "banque"), "banque 0 refusée");
    // Avec la place : les trois banques
    rom_pool_t std = pool_make(false);
    CHECK(rom_pool_load_builtin(&std, &sys, 7, s, &err) && !strcmp(std.name[5], "@basic10"), "cartouche complète chargée avec la place (%s)", err);
}

static int named_calls;
static bool named_cb(void* ctx, int bank, const char* name, const char** err) {
    (void)ctx;
    (void)bank;
    (void)name;
    named_calls++;
    *err = "fichier illisible";
    return false;
}

static void test_load_named(void) {
    const char* err = "";
    rom_pool_t std = pool_make(false);
    named_calls = 0;
    // F-12 : « @inconnu » garde sa cause, le chargeur de fichier n'est pas appelé
    CHECK(!rom_load_named(&std, &sys, 4, "@amiga", named_cb, NULL, &err) && strstr(err, "ROM intégrée inconnue") && named_calls == 0,
          "ROM intégrée inconnue : cause conservée (%s)", err);
    CHECK(!rom_load_named(&std, &sys, 4, "jeu.rom", named_cb, NULL, &err) && named_calls == 1 && strstr(err, "illisible"),
          "fichier : chargeur de la plate-forme");
    CHECK(rom_load_named(&std, &sys, 7, "@atmos", named_cb, NULL, &err) && !strcmp(std.name[7], "@atmos") && named_calls == 1,
          "ROM intégrée connue : chargée par le pool");
}

/*-- F-11, F-25, F-26 : liste de fichiers du menu -------------------------------*/

static void test_name_ignored(void) {
    CHECK(osd_name_ignored("._STRATSED.DSK", false) && osd_name_ignored("X.DSK", true) && !osd_name_ignored("STRATSED.DSK", false) &&
              !osd_name_ignored(".HIDDEN.DSK", false) && !osd_name_ignored("A._B.DSK", false) && !osd_name_ignored("_X.DSK", false),
          "AppleDouble (« ._ ») et entrées cachées ignorées, les autres noms listés");
}

// Texte d'une rangée de la surface (caractères ASCII seulement : les autres sont des glyphes)
static void surface_rows(const osd_surface_t* s, char rows[OSD_ROWS][OSD_COLS + 1]) {
    for (int r = 0; r < OSD_ROWS; r++) {
        for (int c = 0; c < OSD_COLS; c++) rows[r][c] = s->ch[r][c] >= 32 && s->ch[r][c] < 127 ? (char)s->ch[r][c] : '~';
        rows[r][OSD_COLS] = 0;
    }
}

static bool surface_has(const osd_surface_t* s, const char* text) {
    static char rows[OSD_ROWS][OSD_COLS + 1];
    surface_rows(s, rows);
    for (int r = 0; r < OSD_ROWS; r++)
        if (strstr(rows[r], text)) return true;
    return false;
}

static void test_menu_lists(void) {
    static osd_menu_t m;
    static osd_surface_t surf;
    osd_menu_init(&m);
    m.version = "v0";
    m.usb_present = true;
    snprintf(m.usb_label, sizeof(m.usb_label), "Cle montee");
    // Liste pleine : OSD_MENU_FILES .dsk, signalée « tronquée »
    for (int i = 0; i < OSD_MENU_FILES; i++) {
        snprintf(m.files[i].name, sizeof(m.files[i].name), "JEU%03d.DSK", i);
        m.files[i].kind = OSD_FILE_DSK;
        m.files[i].vol = OSD_SRC_USB;
        m.files[i].size = 360448;
    }
    m.nfiles = OSD_MENU_FILES;
    osd_menu_draw(&m, &surf);
    CHECK(!surface_has(&surf, "liste tronqu") && !surface_has(&surf, "(liste tronqu"), "liste complète : aucun avertissement");
    m.truncated = true;
    osd_menu_draw(&m, &surf);
    CHECK(surface_has(&surf, "(liste tronqu"), "page principale : liste tronquée signalée dans le panneau de la clé (F-25)");
    m.cursor = OSD_ITEM_DRIVE0;
    osd_menu_key(&m, OSD_KEY_ENTER);  // sélecteur du lecteur A
    osd_menu_draw(&m, &surf);
    char want[64];
    snprintf(want, sizeof(want), "Liste tronqu~e : %d fichiers au plus", OSD_MENU_FILES);
    CHECK(surface_has(&surf, want), "sélecteur : liste tronquée signalée (« %s »)", want);
    m.truncated = false;
    osd_menu_draw(&m, &surf);
    CHECK(!surface_has(&surf, "Liste tronqu"), "sélecteur : plus d'avertissement quand la liste est complète");

    // F-26 : message de liste vide selon le type de fichier
    osd_menu_init(&m);
    m.version = "v0";
    static const struct {
        int item;
        const char* text;
        const char* not_text;
    } cases[] = {
        {OSD_ITEM_DRIVE0, "Aucune image .dsk sur la cl", "instantan"},
        {OSD_ITEM_TAPE, "Aucune cassette .tap sur la cl", "instantan"},
        {OSD_ITEM_BANK7, "Aucune image .rom sur la cl", "instantan"},
        {OSD_ITEM_STATE, "Aucun instantan", "image .rom"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        osd_menu_init(&m);
        m.version = "v0";
        m.cursor = cases[i].item;
        osd_menu_key(&m, OSD_KEY_ENTER);
        osd_menu_draw(&m, &surf);
        CHECK(surface_has(&surf, cases[i].text) && !surface_has(&surf, cases[i].not_text),
              "liste vide, élément %d : message adapté (« %s »)", cases[i].item, cases[i].text);
    }
    // Réseau : le message du réseau ne parle pas de la clé
    osd_menu_init(&m);
    m.version = "v0";
    m.net_present = true;
    m.cursor = OSD_ITEM_STATE;
    osd_menu_key(&m, OSD_KEY_ENTER);  // choix de la source
    osd_menu_key(&m, OSD_KEY_END);
    osd_menu_key(&m, OSD_KEY_ENTER);  // réseau
    osd_menu_draw(&m, &surf);
    CHECK(surface_has(&surf, "sur le r~seau") && !surface_has(&surf, "sur la cl"), "liste vide du réseau");
}

/*-- F-33 : le découpage de osd_menu_key / osd_menu_draw garde le comportement --*/

static void test_menu_key_split(void) {
    static osd_menu_t m;
    osd_menu_init(&m);
    m.version = "v0";
    // Page principale : Entrée sur RESET rend l'action, flèches circulent
    m.cursor = OSD_ITEM_RESET;
    osd_action_t a = osd_menu_key(&m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_RESET, "page principale : RESET");
    m.cursor = OSD_ITEM_DRIVE0;
    a = osd_menu_key(&m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_NONE && m.page == OSD_PAGE_BROWSE && m.browse_target == OSD_ITEM_DRIVE0, "Entrée sur un lecteur : sélecteur");
    a = osd_menu_key(&m, OSD_KEY_ENTER);  // curseur sur « éjecter »
    CHECK(a.type == OSD_ACT_EJECT && a.target == 0 && m.page == OSD_PAGE_MAIN, "sélecteur : éjecter le lecteur A");
    m.net_present = true;
    m.cursor = OSD_ITEM_DRIVE0;
    osd_menu_key(&m, OSD_KEY_ENTER);
    CHECK(m.page == OSD_PAGE_SOURCE, "réseau prêt : choix de la source");
    a = osd_menu_key(&m, 'r');
    a = osd_menu_key(&m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_SOURCE && a.file == OSD_SRC_NET && m.page == OSD_PAGE_BROWSE && m.browse_vol == OSD_SRC_NET,
          "source Réseau choisie");
}

/*-- O-29 : telestrat_frame.h (ligne de tampon DVI : menu, image, bandeau, marges) --*/

static int plane_bit(const uint32_t* pl, int x) { return (pl[x >> 5] >> (x & 31)) & 1; }

static void test_frame_line(void) {
    telestrat_video_init();
    enum { W = 960, L = 272, WORDS = W / 32 };
    static uint8_t fb[224 * TELESTRAT_VIDEO_BYTES_PER_LINE];
    static osd_surface_t menu;
    static osd_row_t banner;
    uint32_t r[WORDS], g[WORDS], b[WORDS], r2[WORDS], g2[WORDS], b2[WORDS];
#define VIDE(x) memset(x, 0xA5, sizeof(x))

    // Image blanche (couleur 7, deux pixels par octet) : 720 pixels centrés dans 960
    memset(fb, 0x77, sizeof(fb));
    const int top = (L - 224) / 2;
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(top, W, L, fb, NULL, NULL, r, g, b);
    CHECK(plane_bit(r, 119) == 0 && plane_bit(r, 120) == 1 && plane_bit(r, 839) == 1 && plane_bit(r, 840) == 0 &&
              plane_bit(g, 120) == 1 && plane_bit(b, 839) == 1 && plane_bit(b, 119) == 0,
          "image : 720 pixels de x = 120 à 839, marges noires");
    // Bords de l'image : première et dernière ligne source, au-delà bordure noire
    memset(&fb[223 * TELESTRAT_VIDEO_BYTES_PER_LINE], 0x11, TELESTRAT_VIDEO_BYTES_PER_LINE);  // rouge seul
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(top + 223, W, L, fb, NULL, NULL, r, g, b);
    CHECK(plane_bit(r, 120) == 1 && plane_bit(g, 120) == 0 && plane_bit(b, 120) == 0,
          "dernière ligne de l'image (rouge) à y = %d", top + 223);
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(top + 224, W, L, fb, NULL, NULL, r, g, b);
    int zero = 1;
    for (int i = 0; i < WORDS; i++) zero &= !r[i] && !g[i] && !b[i];
    CHECK(zero, "sous l'image : bordure noire, les %d mots écrits", WORDS);
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(top - 1, W, L, fb, NULL, NULL, r, g, b);
    zero = 1;
    for (int i = 0; i < WORDS; i++) zero &= !r[i] && !g[i] && !b[i];
    CHECK(zero, "au-dessus de l'image : bordure noire");

    // Bandeau : 8 lignes, 16 lignes avant le bas ; ailleurs l'image (ou la bordure)
    osd_row_clear(&banner, OSD_ATTR(OSD_WHITE, OSD_BLUE));
    osd_row_puts(&banner, 0, "BANDEAU", OSD_ATTR(OSD_WHITE, OSD_BLUE));
    const int bl = L - 16;
    for (int k = 0; k < 8; k += 7) {  // première et dernière lignes du bandeau
        VIDE(r), VIDE(g), VIDE(b), VIDE(r2), VIDE(g2), VIDE(b2);
        telestrat_frame_line(bl + k, W, L, fb, NULL, &banner, r, g, b);
        osd_render_cells(banner.ch, banner.attr, banner.big, k, (bl + k) & 1, r2, g2, b2);
        CHECK(!memcmp(r, r2, sizeof(r)) && !memcmp(g, g2, sizeof(g)) && !memcmp(b, b2, sizeof(b)),
              "bandeau : ligne %d du bandeau rendue comme osd_render_cells", k);
    }
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(bl - 1, W, L, fb, NULL, &banner, r, g, b);
    zero = 1;
    for (int i = 0; i < WORDS; i++) zero &= !r[i] && !g[i] && !b[i];
    CHECK(zero, "ligne juste au-dessus du bandeau : marge sous l'image (noire)");
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(top + 223, W, L, fb, NULL, &banner, r, g, b);
    CHECK(plane_bit(r, 120) == 1, "bandeau affiché : dernière ligne de l'image inchangée");
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(bl + 8, W, L, fb, NULL, &banner, r, g, b);
    zero = 1;
    for (int i = 0; i < WORDS; i++) zero &= !r[i] && !g[i] && !b[i];
    CHECK(zero, "ligne juste sous le bandeau : bordure noire");
    VIDE(r), VIDE(g), VIDE(b);
    telestrat_frame_line(bl, W, L, fb, NULL, NULL, r, g, b);
    zero = 1;
    for (int i = 0; i < WORDS; i++) zero &= !r[i] && !g[i] && !b[i];
    CHECK(zero, "sans bandeau : la zone du bandeau reste une bordure noire");

    // Menu : prioritaire sur l'image et sur le bandeau, rendu par osd_render_line
    osd_clear(&menu, OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_putc(&menu, 0, 0, 'M', OSD_ATTR(OSD_WHITE, OSD_BLACK));
    for (int y = 0; y < L; y += 3) {
        VIDE(r), VIDE(g), VIDE(b), VIDE(r2), VIDE(g2), VIDE(b2);
        telestrat_frame_line(y, W, L, fb, &menu, &banner, r, g, b);
        osd_render_line(&menu, y, r2, g2, b2);
        if (memcmp(r, r2, sizeof(r)) || memcmp(g, g2, sizeof(g)) || memcmp(b, b2, sizeof(b))) {
            CHECK(0, "menu : ligne %d différente de osd_render_line", y);
            break;
        }
    }

    // 800 x 480 (lines = 240) : 25 mots par plan seulement, image à x = 40
    enum { W8 = 800, L8 = 240, WORDS8 = W8 / 32 };
    uint32_t pr[WORDS8 + 2], pg[WORDS8 + 2], pb[WORDS8 + 2];
    memset(pr, 0xA5, sizeof(pr)), memset(pg, 0xA5, sizeof(pg)), memset(pb, 0xA5, sizeof(pb));
    telestrat_frame_line((L8 - 224) / 2, W8, L8, fb, NULL, NULL, pr, pg, pb);
    CHECK(plane_bit(pr, 39) == 0 && plane_bit(pr, 40) == 1 && plane_bit(pr, 759) == 1 && plane_bit(pr, 760) == 0,
          "800 x 480 : image de x = 40 à 759");
    CHECK(pr[WORDS8] == 0xA5A5A5A5u && pg[WORDS8 + 1] == 0xA5A5A5A5u && pb[WORDS8] == 0xA5A5A5A5u,
          "800 x 480 : rien écrit au-delà de width / 32 mots");
    memset(pr, 0xA5, sizeof(pr)), memset(pg, 0xA5, sizeof(pg)), memset(pb, 0xA5, sizeof(pb));
    telestrat_frame_line(0, W8, L8, fb, NULL, NULL, pr, pg, pb);
    zero = 1;
    for (int i = 0; i < WORDS8; i++) zero &= !pr[i] && !pg[i] && !pb[i];
    CHECK(zero && pr[WORDS8] == 0xA5A5A5A5u, "800 x 480 : bordure noire sur width / 32 mots exactement");
#undef VIDE
}

int main(void) {
    test_frame_line();
    test_config_merge_long();
    test_config_line();
    test_config_port();
    test_profile_check();
    test_user_profile_check();
    test_user_profile_item_length();
    test_builtin_atomic();
    test_load_named();
    test_name_ignored();
    test_menu_lists();
    test_menu_key_split();
    printf("test_revue_firmware : %d/%d vérifications réussies\n", checks - failures, checks);
    return failures ? 1 : 0;
}
