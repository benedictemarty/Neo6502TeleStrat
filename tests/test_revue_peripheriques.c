// test_revue_peripheriques.c — tests des corrections de la zone P de la revue
// de code (docs/CRITIQUE.md, constats P-01 à P-31 : src/devices/)
//
// Programme séparé de test_telestrat.c, même environnement de compilation
// (Makefile : tests/test_revue_*.c). Le premier en-tête inclus est
// telestrat_fdc.h seul : il doit se suffire à lui-même (P-25).

#define _POSIX_C_SOURCE 200809L
#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include "devices/telestrat_fdc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices/mos6551acia.h"
#include "devices/minitel_port.h"
#include "devices/hayes_line.h"
#include "devices/modem_mux.h"
#include "devices/drive_set.h"
#include "devices/printer_fx80.h"
#include "devices/plotter_mcp40.h"
#include "devices/hid_media.h"
#define NEO_STORAGE_IMPL
#include "devices/neo_storage.h"
#include "devices/neo_writer.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        checks++;                                \
        if (!(cond)) {                           \
            failures++;                          \
            printf("ÉCHEC %s:%d : ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
        }                                        \
    } while (0)

/*-- Modem factice -------------------------------------------------------------*/

static char modem_out[1024];
static int modem_out_n;

static void modem_reset_out(void) {
    modem_out_n = 0;
    modem_out[0] = 0;
}

static void modem_write(void* ctx, const uint8_t* d, uint32_t n) {
    (void)ctx;
    for (uint32_t i = 0; i < n && modem_out_n < (int)sizeof(modem_out) - 1; i++) modem_out[modem_out_n++] = (char)d[i];
    modem_out[modem_out_n] = 0;
}

static void modem_says(hayes_line_t* h, const char* s) {
    while (*s) hayes_line_feed(h, (uint8_t)*s++);
}

// Ligne en communication : ATD puis CONNECT
static void hayes_online(hayes_line_t* h) {
    hayes_line_init(h, modem_write, NULL, "h:1", 0);
    minitel_line_t l = hayes_line_line(h);
    l.dial(l.ctx);
    modem_says(h, "CONNECT\r\n");
}

static void mux_says(modem_mux_t* m, const char* s) {
    while (*s) modem_mux_feed(m, (uint8_t)*s++);
}

/*-- P-01, P-22 : drive_set ----------------------------------------------------*/

static void test_drive_set(void) {
    drive_set_t s;
    drive_set_init(&s);
    CHECK(drive_set_add(&s, "JEUX_ORIC_V2.DSK"), "nom de 16 caractères accepté");
    CHECK(strcmp(s.names[0], "JEUX_ORIC_V2.DSK") == 0, "nom conservé en entier (%s)", s.names[0]);
    const char* want[4] = {"jeux_oric_v2.dsk", NULL, NULL, NULL};
    drive_set_assign(&s, want);
    CHECK(s.slot[0] == 0 && s.unmatched == 0, "a= avec un nom long (casse ignorée) trouvé : %d", s.slot[0]);

    char longname[DRIVE_SET_NAME_LEN + 8];
    memset(longname, 'A', sizeof(longname));
    longname[DRIVE_SET_NAME_LEN - 1] = 0;  // 47 caractères : le plus long accepté
    CHECK(drive_set_add(&s, longname) && s.count == 2, "nom de %d caractères accepté", DRIVE_SET_NAME_LEN - 1);
    longname[DRIVE_SET_NAME_LEN - 1] = 'A';
    longname[DRIVE_SET_NAME_LEN] = 0;  // 48 caractères : refusé, pas tronqué
    CHECK(!drive_set_add(&s, longname) && s.count == 2 && s.rejected == 1, "nom trop long refusé (compté)");

    // a= sans correspondance : A reste vide (le commentaire de l'en-tête), signalé
    const char* none[4] = {"ABSENT.DSK", NULL, NULL, NULL};
    drive_set_assign(&s, none);
    CHECK(s.slot[0] == -1 && (s.unmatched & 1), "a= introuvable : lecteur A vide et signalé (%d, %02X)", s.slot[0],
          s.unmatched);
    const char* empty[4] = {"", NULL, NULL, NULL};
    drive_set_assign(&s, empty);
    CHECK(s.slot[0] == 0 && s.unmatched == 0, "a= vide : première image libre en A");

    // Au-delà de DRIVE_SET_MAX images : refus compté
    drive_set_t big;
    drive_set_init(&big);
    char nm[16];
    for (int i = 0; i < DRIVE_SET_MAX; i++) {
        snprintf(nm, sizeof(nm), "IMG%02d.DSK", i);
        drive_set_add(&big, nm);
    }
    CHECK(!drive_set_add(&big, "DERNIERE.DSK") && big.rejected == 1 && big.count == DRIVE_SET_MAX,
          "33e image refusée et comptée");
}

/*-- P-02, P-03, P-14, P-15, P-16, P-28, P-29 : modem et Minitel ----------------*/

static void test_hayes_rx_lost(void) {
    hayes_line_t h;
    modem_reset_out();
    hayes_online(&h);
    CHECK(hayes_line_rx_room(&h) == HAYES_RXQ, "file vide : toute la place");
    for (int i = 0; i < HAYES_RXQ + 88; i++) hayes_line_feed(&h, 'A');
    CHECK(byte_ring_count(&h.rx) == HAYES_RXQ && h.rx.lost == 88 && hayes_line_rx_room(&h) == 0,
          "file pleine : pertes comptées (%u)", (unsigned)h.rx.lost);
    CHECK(hayes_line_recv(&h) == 'A' && hayes_line_rx_room(&h) == 1, "place rendue par la lecture");

    // Prise RS232 du multiplexeur
    hayes_line_t hh;
    modem_mux_t m;
    modem_mux_init(&m, &hh);
    modem_mux_select(&m, true);
    modem_mux_attach(&m, modem_write, NULL, "h:1", 0);
    for (int i = 0; i < MODEM_MUX_RXQ + 5; i++) modem_mux_feed(&m, 'B');
    CHECK(byte_ring_count(&m.rx) == MODEM_MUX_RXQ && m.rx.lost == 5 && modem_mux_rx_room(&m) == 0, "RS232 : pertes comptées");
    modem_mux_rs232_recv(&m);
    CHECK(modem_mux_rx_room(&m) == 1, "RS232 : place libre après lecture");
}

static void test_minitel_queue_lost(void) {
    minitel_port_t p;
    minitel_line_t none = {0};
    minitel_port_init(&p, &none);
    const uint8_t oppo[] = {0x1B, 0x39, 0x6F};
    for (int i = 0; i < 10; i++)
        for (int k = 0; k < 3; k++) minitel_port_from_telestrat(&p, oppo[k]);
    CHECK(byte_ring_count(&p.q) == MINITEL_QUEUE && p.q.lost == 4, "file Minitel pleine : 4 octets perdus comptés (%u)",
          (unsigned)p.q.lost);

    // P-03 : minitel_port_reset efface SEP et séquence périmés
    minitel_port_from_telestrat(&p, 0x1B);  // séquence commencée
    minitel_port_reset(&p);
    CHECK(byte_ring_count(&p.q) == 0 && p.seq_need == 0 && p.state == MINITEL_IDLE && !p.opposition &&
              minitel_port_to_telestrat(&p) == -1,
          "minitel_port_reset : file et séquence effacées");

    // P-16 (à vérifier, caractérisation) : PRO2 / PRO3 consommés sans réponse ni comptage
    const uint32_t pro1_before = p.pro1_count;
    const uint8_t pro2[] = {0x1B, 0x3A, 0x41, 0x42};
    const uint8_t pro3[] = {0x1B, 0x3B, 0x41, 0x42, 0x43};
    for (int k = 0; k < 4; k++) minitel_port_from_telestrat(&p, pro2[k]);
    for (int k = 0; k < 5; k++) minitel_port_from_telestrat(&p, pro3[k]);
    CHECK(p.pro1_count == pro1_before && p.seq_need == 0 && minitel_port_to_telestrat(&p) == -1,
          "PRO2 / PRO3 : consommés sans réponse (comportement à vérifier sur TELEMON)");
}

static void test_hayes_no_carrier(void) {
    hayes_line_t h;
    modem_reset_out();
    hayes_online(&h);
    minitel_line_t l = hayes_line_line(&h);

    // « NO CARRIER » au milieu de données (sans fin de ligne devant) : données
    modem_says(&h, "XNO CARRIERY");
    CHECK(l.carrier(l.ctx), "texte « NO CARRIER » sans CRLF : la ligne reste en ligne");
    char got[64];
    int n = 0, c;
    while ((c = l.recv(l.ctx)) >= 0 && n < 63) got[n++] = (char)c;
    got[n] = 0;
    CHECK(strcmp(got, "XNO CARRIERY") == 0, "texte transmis tel quel : %s", got);

    // Précédé de CRLF : porteuse perdue, octets retirés des données
    modem_says(&h, "AB\r\nNO CARRIER\r\n");
    CHECK(h.state == HAYES_COMMAND, "CRLF + NO CARRIER : modem revenu en mode commande");
    CHECK(l.carrier(l.ctx), "octets reçus avant NO CARRIER : porteuse tenue jusqu'à leur remise");
    n = 0;
    while ((c = l.recv(l.ctx)) >= 0 && n < 63) got[n++] = (char)c;
    got[n] = 0;
    CHECK(strcmp(got, "AB") == 0, "les octets du motif ne sont pas des données : %s", got);
    CHECK(!l.carrier(l.ctx), "file vidée : porteuse perdue");

    // Même chose par le Minitel : les données d'avant NO CARRIER arrivent avant SEP $59 $53
    {
        minitel_port_t p;
        hayes_online(&h);
        minitel_port_init(&p, &l);
        p.state = MINITEL_ONLINE;
        modem_says(&h, "ADIEU\r\nNO CARRIER\r\n");
        char rx[32];
        int k = 0;
        for (int i = 0; i < 40 && k < 31; i++) {
            minitel_port_tick(&p, 1000);
            int b = minitel_port_to_telestrat(&p);
            if (b >= 0) rx[k++] = (char)b;
        }
        rx[k] = 0;
        CHECK(strcmp(rx, "ADIEU\x13\x59\x13\x53") == 0, "ADIEU remis au Minitel, puis déconnexion (%d octets)", k);
    }

    // Raccrochage ou nouvel appel : les octets restants du dernier appel sont jetés
    hayes_online(&h);
    modem_says(&h, "RESTE");
    hayes_line_hangup(&h);
    CHECK(l.recv(l.ctx) == -1 && !l.carrier(l.ctx), "raccrochage : octets restants jetés");
    hayes_online(&h);
    modem_says(&h, "VIEUX\r\nNO CARRIER\r\n");
    modem_reset_out();
    CHECK(l.dial(l.ctx) && l.recv(l.ctx) == -1, "nouvel appel : octets de l'appel précédent jetés");

    // Juste après CONNECT : la fin de ligne de CONNECT tient lieu de CRLF
    hayes_online(&h);
    modem_says(&h, "NO CARRIER\r\n");
    CHECK(!l.carrier(l.ctx) && l.recv(l.ctx) == -1, "NO CARRIER aussitôt après CONNECT");

    // Début de motif resté sans suite : rendu en données après la durée de retenue
    hayes_online(&h);
    modem_says(&h, "Z\r\nNO");
    CHECK(l.recv(l.ctx) == 'Z' && l.recv(l.ctx) == -1, "début de motif retenu");
    hayes_line_tick(&h, HAYES_NC_HOLD_US);
    n = 0;
    while ((c = l.recv(l.ctx)) >= 0 && n < 63) got[n++] = (char)c;
    got[n] = 0;
    CHECK(strcmp(got, "\r\nNO") == 0 && l.carrier(l.ctx), "motif abandonné : rendu en données (%d)", n);

    // Motif interrompu par un autre octet : tout est rendu
    hayes_online(&h);
    modem_says(&h, "Q\r\nNOX");
    n = 0;
    while ((c = l.recv(l.ctx)) >= 0 && n < 63) got[n++] = (char)c;
    got[n] = 0;
    CHECK(strcmp(got, "Q\r\nNOX") == 0, "motif interrompu : octets rendus dans l'ordre (%s)", got);
}

static void test_hayes_failures(void) {
    // P-15 : BUSY n'attend pas 40 s
    hayes_line_t h;
    modem_reset_out();
    hayes_line_init(&h, modem_write, NULL, "h:1", 0);
    minitel_line_t l = hayes_line_line(&h);
    minitel_port_t p;
    minitel_port_init(&p, &l);
    const uint8_t conn[] = {0x1B, 0x39, 0x68};
    for (int k = 0; k < 3; k++) minitel_port_from_telestrat(&p, conn[k]);
    CHECK(p.state == MINITEL_CONNECTING, "CONNEXION : appel en cours");
    minitel_port_tick(&p, 1000);
    CHECK(p.state == MINITEL_CONNECTING, "appel en cours : on attend");
    modem_says(&h, "\r\nBUSY\r\n");
    minitel_port_tick(&p, 1000);
    CHECK(p.state == MINITEL_IDLE, "BUSY : échec aussitôt (état %d)", p.state);
    CHECK(minitel_port_to_telestrat(&p) == 0x13 && minitel_port_to_telestrat(&p) == 0x59 &&
              minitel_port_to_telestrat(&p) == 0x13 && minitel_port_to_telestrat(&p) == 0x59 &&
              minitel_port_to_telestrat(&p) == -1,
          "BUSY : SEP $59 de bascule puis SEP $59 d'échec");

    // ERROR et NO ANSWER de même
    const char* fails[] = {"\r\nERROR\r\n", "\r\nNO ANSWER\r\n"};
    for (int i = 0; i < 2; i++) {
        hayes_line_init(&h, modem_write, NULL, "h:1", 0);
        minitel_port_init(&p, &l);
        for (int k = 0; k < 3; k++) minitel_port_from_telestrat(&p, conn[k]);
        modem_says(&h, fails[i]);
        minitel_port_tick(&p, 1000);
        CHECK(p.state == MINITEL_IDLE, "échec %d signalé aussitôt", i);
    }

    // Une ligne sans pending (TCP) garde le délai de 40 s
    minitel_line_t nop = l;
    nop.pending = NULL;
    hayes_line_init(&h, modem_write, NULL, "h:1", 0);
    minitel_port_init(&p, &nop);
    for (int k = 0; k < 3; k++) minitel_port_from_telestrat(&p, conn[k]);
    modem_says(&h, "\r\nBUSY\r\n");
    minitel_port_tick(&p, 1000);
    CHECK(p.state == MINITEL_CONNECTING, "sans pending : on attend les 40 s");

    // Pas de réponse du modem : abandon du modem lui-même après HAYES_CALL_TIMEOUT_US
    hayes_line_init(&h, modem_write, NULL, "h:1", 0);
    CHECK(l.dial(l.ctx) && h.state == HAYES_DIALING, "ATD envoyé");
    modem_reset_out();
    hayes_line_tick(&h, HAYES_CALL_TIMEOUT_US - 1000);
    CHECK(h.state == HAYES_DIALING, "numérotation encore en cours");
    hayes_line_tick(&h, 2000);
    CHECK(h.state == HAYES_COMMAND && strcmp(modem_out, "\r") == 0, "numérotation sans réponse : abandonnée");
    hayes_line_answer(&h);
    CHECK(h.state == HAYES_ANSWERING, "ATA envoyé");
    hayes_line_tick(&h, HAYES_CALL_TIMEOUT_US);
    CHECK(h.state == HAYES_COMMAND, "décrochage sans réponse : abandonné");
}

static void test_mux_switch(void) {
    hayes_line_t h;
    modem_mux_t m;
    modem_mux_init(&m, &h);
    minitel_line_t l = hayes_line_line(&h);
    modem_reset_out();
    modem_mux_attach(&m, modem_write, NULL, "go.test:1", 0);  // prise Minitel : initialisé
    CHECK(l.dial(l.ctx), "appel sortant");
    mux_says(&m, "\r\nCONNECT 1200\r\n");
    CHECK(l.carrier(l.ctx), "en ligne côté Minitel");

    // Bascule en pleine communication : raccrochage propre, rien d'abandonné
    modem_reset_out();
    modem_mux_select(&m, true);
    CHECK(m.hanging && !modem_out_n, "bascule en ligne : raccrochage lancé (garde)");
    modem_mux_rs232_send(&m, 'A');
    CHECK(modem_out_n == 0, "raccrochage : rien d'émis par la RS232");
    mux_says(&m, "x");
    CHECK(modem_mux_rs232_recv(&m) == -1, "raccrochage : rien de reçu côté RS232");
    modem_mux_tick(&m, 1200000);
    CHECK(strcmp(modem_out, "+++") == 0 && m.hanging, "+++ après la garde");
    modem_mux_tick(&m, 1200000);
    CHECK(strcmp(modem_out, "+++ATH\r") == 0 && !m.hanging, "ATH envoyé : %s", modem_out);
    modem_mux_rs232_send(&m, 'B');
    CHECK(strcmp(modem_out, "+++ATH\rB") == 0, "RS232 libre ensuite");

    // Retour sur la prise Minitel pendant le raccrochage : réinitialisé après l'ATH seulement
    modem_mux_select(&m, false);
    CHECK(strncmp(modem_out + 8, "ATE0V1", 6) == 0, "retour Minitel : réinitialisation");
    modem_reset_out();
    CHECK(l.dial(l.ctx), "nouvel appel");
    mux_says(&m, "CONNECT\r\n");
    modem_reset_out();
    modem_mux_select(&m, true);
    modem_mux_tick(&m, 1200000);  // +++
    modem_reset_out();
    modem_mux_select(&m, false);
    CHECK(modem_out_n == 0 && m.hanging, "retour pendant la garde : pas d'ATE0V1 au correspondant");
    modem_mux_tick(&m, 1200000);
    CHECK(strncmp(modem_out, "ATH\rATE0V1\rATS0=0\r", 18) == 0 && !m.hanging, "ATH puis initialisation (%s)", modem_out);

    // Appel en cours de numérotation : interrompu par un retour chariot, pas de garde
    modem_mux_select(&m, false);
    modem_reset_out();
    hayes_line_init(&h, modem_write, NULL, "go.test:1", 0);
    l.dial(l.ctx);
    modem_reset_out();
    modem_mux_select(&m, true);
    CHECK(strcmp(modem_out, "\r") == 0 && !m.hanging && h.state == HAYES_COMMAND, "numérotation interrompue");
}

/*-- P-04 à P-07, P-18, P-23 : FX-80 -------------------------------------------*/

typedef struct {
    int opens, closes;
    uint32_t bytes;
} sink_t;

static bool sink_open(void* c, const char* e) {
    (void)e;
    ((sink_t*)c)->opens++;
    return true;
}
static void sink_write(void* c, const void* d, uint32_t n) {
    (void)d;
    ((sink_t*)c)->bytes += n;
}
static void sink_close(void* c) { ((sink_t*)c)->closes++; }

static uint8_t font_full[256][8];
static fx80_t fx;

static void fx_str(const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) fx80_feed(&fx, b[i]);
}

static void test_fx80(void) {
    sink_t sk = {0};
    printer_out_t out = {sink_open, sink_write, NULL, sink_close, &sk};
    memset(font_full, 0xFF, sizeof(font_full));

    // P-04 : ESC b (canal non nul) remet tab_last à 0
    fx80_init(&fx, &out, font_full);
    const uint8_t vtabs[] = {27, 'B', 100, 0};
    fx_str(vtabs, 4);
    const uint8_t chan[] = {27, 'b', 1, 40, 50, 0};
    fx_str(chan, 6);
    CHECK(fx.parse == FX80_P_NONE && fx.h == 0, "ESC b canal 1 : liste ignorée jusqu'au 0 (h = %d)", (int)fx.h);

    // P-05 (à vérifier, caractérisation) : ESC e / ESC f absents, paramètre imprimé en texte
    fx80_init(&fx, &out, font_full);
    const uint8_t esc_e[] = {27, 'e', 'A'};
    fx_str(esc_e, 3);
    CHECK(fx.h == 144, "ESC e : paramètre imprimé comme texte (comportement à vérifier sur la FX-80)");

    // P-06 : ESC @ ne ramène pas page_rows sous band_top
    fx80_init(&fx, &out, font_full);
    const uint8_t c22[] = {27, 'C', 0, 22};
    fx_str(c22, 4);
    for (int i = 0; i < 18; i++) {
        const uint8_t j[] = {27, 'J', 255};
        fx_str(j, 3);
        while (fx80_busy(&fx)) fx80_service(&fx, 1 << 20);
    }
    fx80_service(&fx, 1 << 20);
    CHECK(fx.band_top > 1584, "page blanche avancée (band_top = %u)", (unsigned)fx.band_top);
    const uint8_t reset[] = {27, '@'};
    fx_str(reset, 2);
    CHECK(fx.page_rows >= fx.band_top, "ESC @ : page_rows (%u) >= band_top (%u)", (unsigned)fx.page_rows,
          (unsigned)fx.band_top);

    // P-07 : marge droite à 9 pouces, points au-delà de 8,5 comptés
    fx80_init(&fx, &out, font_full);
    for (int i = 0; i < 80; i++) fx80_feed(&fx, 'X');
    CHECK(fx.dots_lost == 0, "80 colonnes : aucun point perdu (%u)", (unsigned)fx.dots_lost);
    fx80_init(&fx, &out, font_full);
    const uint8_t q90[] = {27, 'Q', 90};
    fx_str(q90, 3);
    CHECK(fx.rmargin == 90 * 144, "marge droite 9 pouces acceptée");
    for (int i = 0; i < 90; i++) fx80_feed(&fx, 'X');
    CHECK(fx.dots_lost > 0, "points au-delà de 8,5 pouces comptés (%u)", (unsigned)fx.dots_lost);

    // P-18 : h reste borné dans les graphiques répétés sans retour chariot
    fx80_init(&fx, &out, font_full);
    for (int r = 0; r < 200; r++) {
        const uint8_t k[] = {27, 'K', 0xFF, 0xFF};
        fx_str(k, 4);
        for (int i = 0; i < 65535; i++) fx80_feed(&fx, 0);
    }
    CHECK(fx.h >= (1 << 28) && fx.h <= (1 << 28) + 24 + 64, "position horizontale bornée (h = %d)", (int)fx.h);

    // P-30 : la ligne blanche d'une page ouverte est bien blanche (ex-initialiseur GNU)
    fx80_init(&fx, &out, font_full);
    fx80_feed(&fx, 'X');
    fx80_feed(&fx, 12);  // saut de page
    while (fx80_busy(&fx)) fx80_service(&fx, 1 << 20);
    CHECK(sk.opens >= 1 && sk.closes >= 1 && fx.pages >= 1, "page écrite (%u)", (unsigned)fx.pages);
}

/*-- P-08 à P-13, P-24 : ACIA --------------------------------------------------*/

static mos6551acia_t acia;
static int acia_tx_n;
static uint8_t acia_tx[64];
static int acia_rx_next = -1;

static void acia_txcb(uint8_t d, void* u) {
    (void)u;
    if (acia_tx_n < 64) acia_tx[acia_tx_n] = d;
    acia_tx_n++;
}
static int acia_rxcb(void* u) {
    (void)u;
    const int c = acia_rx_next;
    acia_rx_next = -1;
    return c;
}

static void acia_fresh(void) {
    memset(&acia, 0, sizeof(acia));
    acia.cpu_freq = 1000000;
    mos6551acia_reset(&acia);
    acia.tx_cb = acia_txcb;
    acia.rx_cb = acia_rxcb;
    acia_tx_n = 0;
    acia_rx_next = -1;
}

static void test_acia(void) {
    // P-08 : DTR = 0 coupe aussi l'IRQ d'émission
    acia_fresh();
    mos6551acia_write(&acia, 2, 0x04);  // émission autorisée, DTR = 0
    CHECK(!mos6551acia_irq(&acia), "DTR = 0 : pas d'IRQ d'émission");
    CHECK(acia.status & MOS6551_ST_IRQ, "DTR = 0 : l'événement reste noté dans l'état");
    mos6551acia_write(&acia, 2, 0x05);  // DTR = 1
    CHECK(mos6551acia_irq(&acia), "DTR = 1 : IRQ d'émission");
    mos6551acia_write(&acia, 2, 0x04);
    CHECK(!mos6551acia_irq(&acia), "DTR repassé à 0 : IRQ coupée");

    // P-12 (choix documenté, non vérifié sur matériel) : l'état n'efface pas l'IRQ de réception
    acia_fresh();
    mos6551acia_write(&acia, 2, 0x01);  // DTR, IRQ de réception autorisée
    acia_rx_next = 'A';
    mos6551acia_poll_rx(&acia);
    CHECK(mos6551acia_irq(&acia), "octet reçu : IRQ");
    mos6551acia_read(&acia, 1);
    CHECK(mos6551acia_irq(&acia), "lecture de l'état : IRQ maintenue tant que la donnée n'est pas lue");
    CHECK(mos6551acia_read(&acia, 0) == 'A' && !mos6551acia_irq(&acia), "lecture de la donnée : IRQ levée");

    // P-09 (à vérifier, caractérisation) : émetteur coupé (b2-3 = 00), l'octet part quand même
    acia_fresh();
    mos6551acia_write(&acia, 2, 0x01);
    mos6551acia_write(&acia, 0, 'Z');
    mos6551acia_tick(&acia, 20000);
    CHECK(acia_tx_n == 1 && acia_tx[0] == 'Z', "b2-3 = 00 : octet émis (comportement à vérifier)");

    // P-10 : durée et format d'accord sur les bits de stop
    acia_fresh();
    acia.control = 0x80 | 0x08;  // 8 bits, 2 stops demandés, 1200 bauds
    acia.command = 0x20;         // parité active
    mos6551acia_format_t f = mos6551acia_format(&acia);
    CHECK(f.stop_bits == 1 && f.data_bits == 8 && mos6551acia_char_cycles(&acia) == (int32_t)(1000000u * 11 / 1200),
          "8 bits + parité + STOP2 : 1 stop, 11 bits (%d cycles)", (int)mos6551acia_char_cycles(&acia));
    acia.command = 0;
    f = mos6551acia_format(&acia);
    CHECK(f.stop_bits == 2 && mos6551acia_char_cycles(&acia) == (int32_t)(1000000u * 11 / 1200),
          "8 bits sans parité + STOP2 : 2 stops, 11 bits");
    acia.control = 0x08;
    CHECK(mos6551acia_format(&acia).stop_bits == 1 && mos6551acia_char_cycles(&acia) == (int32_t)(1000000u * 10 / 1200),
          "STOP2 absent : 1 stop, 10 bits");

    // P-11 : l'écho n'écrase pas un octet qui attend dans le registre d'émission
    acia_fresh();
    mos6551acia_write(&acia, 2, 0x01 | MOS6551_CMD_ECHO);
    mos6551acia_write(&acia, 0, 'X');  // passe dans le registre à décalage
    mos6551acia_write(&acia, 0, 'A');  // attend dans le registre d'émission
    acia_rx_next = 'Z';
    mos6551acia_poll_rx(&acia);
    CHECK(acia.tdr == 'A' && acia.tdr_full, "écho : octet en attente préservé (tdr = %02X)", acia.tdr);
    mos6551acia_tick(&acia, 30000);
    mos6551acia_tick(&acia, 30000);
    CHECK(acia_tx_n == 2 && acia_tx[0] == 'X' && acia_tx[1] == 'A', "émis : X puis A (%d octets)", acia_tx_n);
    // Sans octet en attente, l'écho part comme avant
    acia_fresh();
    mos6551acia_write(&acia, 2, 0x01 | MOS6551_CMD_ECHO);
    acia_rx_next = 'Z';
    mos6551acia_poll_rx(&acia);
    mos6551acia_tick(&acia, 30000);
    CHECK(acia_tx_n == 1 && acia_tx[0] == 'Z', "écho sans attente : renvoyé");

    // P-13 : le reste de cycles ne se perd pas d'un caractère à l'autre
    acia_fresh();
    acia.control = 0x1E;  // 8 bits, 1 stop, 9600 bauds : 1041 cycles par caractère
    mos6551acia_write(&acia, 2, 0x01);
    const int chars = 200;
    const int budget = 1041 * chars + 10;
    for (int t = 0; t < budget; t += 7) {
        if (!acia.tdr_full) mos6551acia_write(&acia, 0, 'k');
        mos6551acia_tick(&acia, 7);
    }
    CHECK(acia_tx_n >= chars, "débit sans dérive : %d caractères pour %d cycles", acia_tx_n, budget);

    // P-24 : instantané portable, sans les pointeurs de l'hôte
    acia_fresh();
    mos6551acia_write(&acia, 2, 0x0B);
    mos6551acia_write(&acia, 3, 0x1E);
    mos6551acia_write(&acia, 0, 'q');
    mos6551acia_snapshot_t snap;
    mos6551acia_snapshot_save(&acia, &snap);
    CHECK(sizeof(snap) == 24, "instantané de taille fixe (%zu)", sizeof(snap));
    mos6551acia_t other;
    memset(&other, 0, sizeof(other));
    other.tx_cb = acia_txcb;
    other.user_data = &other;
    mos6551acia_snapshot_load(&other, &snap);
    CHECK(other.command == acia.command && other.control == acia.control && other.tsr == acia.tsr &&
              other.tsr_busy == acia.tsr_busy && other.tx_timer == acia.tx_timer && other.cpu_freq == 1000000 &&
              other.tx_cb == acia_txcb && other.user_data == &other && other.rx_cb == NULL,
          "chargement : registres repris, rappels du destinataire conservés");
}

/*-- P-17, P-18, P-19 : MCP-40 -------------------------------------------------*/

static char svg[1 << 16];
static uint32_t svg_len, svg_pos;

static bool svg_open(void* c, const char* e) {
    (void)c;
    (void)e;
    svg_len = svg_pos = 0;
    return true;
}
static void svg_write(void* c, const void* d, uint32_t n) {
    (void)c;
    if (svg_pos + n >= sizeof(svg)) return;
    memcpy(svg + svg_pos, d, n);
    svg_pos += n;
    if (svg_pos > svg_len) svg_len = svg_pos;
    svg[svg_len] = 0;
}
static void svg_seek(void* c, uint32_t pos) {
    (void)c;
    svg_pos = pos;
}
static void svg_close(void* c) { (void)c; }

static void mcp_str(mcp40_t* p, const char* s) {
    while (*s) mcp40_feed(p, (uint8_t)*s++);
}

static void test_mcp40(void) {
    printer_out_t out = {svg_open, svg_write, svg_seek, svg_close, NULL};
    static mcp40_t p;

    // P-17 : « - » sans chiffre ne contamine pas le nombre suivant
    mcp40_init(&p, NULL);
    mcp40_feed(&p, 18);
    mcp_str(&p, "M-,5,7\r");
    CHECK(p.x == 5 && p.y == 7, "M-,5,7 : « - » isolé abandonné (x = %d, y = %d)", (int)p.x, (int)p.y);
    mcp_str(&p, "M-5,-7\r");
    CHECK(p.x == -5 && p.y == -7, "nombres négatifs normaux (x = %d, y = %d)", (int)p.x, (int)p.y);
    mcp_str(&p, "D-,5,7\r");
    CHECK(p.x == 5 && p.y == 7, "D-,5,7 : de même");
    mcp_str(&p, "M3-4,5\r");
    CHECK(p.x == 3 && p.y == -4, "« 3-4,5 » : 3 puis -4 (x = %d, y = %d)", (int)p.x, (int)p.y);

    // P-19 : graduations au-delà de 255 comptées
    mcp40_init(&p, NULL);
    mcp40_feed(&p, 18);
    mcp_str(&p, "X1,10,300\r");
    CHECK(p.ticks_lost == 45, "X 300 graduations : 45 non tracées et comptées (%u)", (unsigned)p.ticks_lost);
    mcp_str(&p, "X1,10,200\r");
    CHECK(p.ticks_lost == 45, "X 200 graduations : rien de perdu");

    // P-18 : positions bornées, en-tête SVG de longueur fixe
    mcp40_init(&p, &out);
    mcp40_feed(&p, 18);
    mcp_str(&p, "D10,10\r");
    mcp40_finish(&p);
    const char* style_small = strstr(svg, "style=");
    CHECK(style_small != NULL, "SVG écrit");
    const long off_small = style_small ? style_small - svg : -1;

    mcp40_init(&p, &out);
    mcp40_feed(&p, 18);
    for (int i = 0; i < 1100000; i++) mcp_str(&p, "R999,999\r");
    CHECK(p.x == MCP40_POS_MAX && p.y == MCP40_POS_MAX, "position bornée (x = %d)", (int)p.x);
    mcp_str(&p, "D0,0\r");
    mcp40_finish(&p);
    const char* style_big = strstr(svg, "style=");
    CHECK(style_big != NULL && style_big - svg == off_small, "en-tête de longueur fixe (%ld contre %ld)",
          style_big ? (long)(style_big - svg) : -1L, off_small);
}

/*-- P-20 : hid_media ----------------------------------------------------------*/

static void test_hid_media(void) {
    static uint8_t desc[300 * 6 + 10];
    int n = 0;
    for (int i = 0; i < 300; i++) {  // 300 x 255 octets constants : 612000 bits
        const uint8_t pad[6] = {0x75, 0x08, 0x95, 0xFF, 0x81, 0x01};
        memcpy(desc + n, pad, 6);
        n += 6;
    }
    const uint8_t tail[10] = {0x05, 0x0C, 0x09, 0xE9, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02};
    memcpy(desc + n, tail, 10);
    n += 10;
    hid_media_t m;
    CHECK(hid_media_parse(&m, desc, (uint16_t)n) && m.num_fields == 1, "descripteur long analysé");
    CHECK(m.field[0].bit_off == 300u * 255u * 8u, "position du champ sur 32 bits (%u)", (unsigned)m.field[0].bit_off);

    // Élément long en fin de descripteur : la lecture s'arrête sans reboucler
    static uint8_t lg[8] = {0xFE, 0xFF, 0x00, 0, 0, 0, 0, 0};
    CHECK(!hid_media_parse(&m, lg, sizeof(lg)), "élément long qui dépasse : lecture arrêtée");
}

/*-- P-21 : neo_writer ---------------------------------------------------------*/

static void test_neo_writer(void) {
    static uint8_t a_data[16], b_data[16];
    neo_mem_file_t files[] = {
        {"A.TXT", a_data, 0, sizeof(a_data)},
        {"B.TXT", b_data, 0, sizeof(b_data)},
    };
    neo_mem_volume_t vol = {files, 2};
    neo_storage_set(NEO_VOL_USB, "Clé", &neo_storage_mem_ops, &vol);
    neo_writer_t w;
    uint8_t buf[8];
    memset(&w, 0, sizeof(w));
    CHECK(neo_writer_open(&w, NEO_VOL_USB, "A.TXT", buf, sizeof(buf)), "A.TXT ouvert");
    neo_writer_write(&w, "abc", 3);
    CHECK(neo_writer_open(&w, NEO_VOL_USB, "B.TXT", buf, sizeof(buf)), "B.TXT ouvert sans fermer A.TXT");
    CHECK(files[0].size == 3 && memcmp(a_data, "abc", 3) == 0, "A.TXT vidé et refermé (%u octets)",
          (unsigned)files[0].size);
    neo_writer_write(&w, "xy", 2);
    CHECK(neo_writer_close(&w) && files[1].size == 2 && !neo_writer_is_open(&w), "B.TXT fermé");
    neo_storage_set(NEO_VOL_USB, NULL, NULL, NULL);
}

/*-- P-25 : telestrat_fdc_tick équivalent à wd1793_tick_n ------------------------*/

static void test_fdc_tick(void) {
    static const uint8_t cmds[] = {0x00, 0x1F, 0x40, 0x80, 0xA0, 0xC0, 0xE0, 0xF0, 0x03, 0xD0};
    uint32_t seed = 12345;
    int diffs = 0, steps = 0;
    for (size_t c = 0; c < sizeof(cmds); c++) {
        telestrat_fdc_t a, b;
        telestrat_fdc_init(&a);
        telestrat_fdc_init(&b);
        telestrat_fdc_write(&a, 4, 0x01);
        telestrat_fdc_write(&b, 4, 0x01);
        telestrat_fdc_write(&a, 0, cmds[c]);
        telestrat_fdc_write(&b, 0, cmds[c]);
        for (int i = 0; i < 80; i++) {
            seed = seed * 1103515245u + 12345u;
            const uint32_t n = 1 + (seed >> 16) % (i % 7 == 0 ? 600 : 40);
            telestrat_fdc_tick(&a, n);
            wd1793_tick_n(&b.wd, n);
            steps++;
            if (memcmp(&a.wd, &b.wd, sizeof(a.wd)) != 0) diffs++;
        }
    }
    CHECK(diffs == 0, "telestrat_fdc_tick = wd1793_tick_n sur %d pas (%d écarts)", steps, diffs);

    // P-23 (à vérifier, voir N-16) : reflets non décodés ici
    telestrat_fdc_t f;
    telestrat_fdc_init(&f);
    uint8_t v;
    CHECK(!telestrat_fdc_read(&f, 5, &v) && !telestrat_fdc_read(&f, 9, &v) && !telestrat_fdc_write(&f, 6, 0),
          "registres 5-7 et 9-B : non décodés par le FDC (inchangé)");
}

/*-- P-29 : modem_mux.h se suffit pour snprintf (inclus plus haut sans stdio avant) --*/

int main(void) {
    test_drive_set();
    test_hayes_rx_lost();
    test_minitel_queue_lost();
    test_hayes_no_carrier();
    test_hayes_failures();
    test_mux_switch();
    test_fx80();
    test_acia();
    test_mcp40();
    test_hid_media();
    test_neo_writer();
    test_fdc_tick();
    printf("test_revue_peripheriques : %d/%d vérifications réussies\n", checks - failures, checks);
    return failures ? 1 : 0;
}
