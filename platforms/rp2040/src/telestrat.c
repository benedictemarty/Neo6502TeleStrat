// telestrat.c — Oric Telestrat pour Olimex Neo6502 (RP2040 + vrai W65C02S)
//
// Le 65C02 du Neo6502 exécute TELEMON ; le RP2040 sert la mémoire (banques),
// émule VIA 1 et 2, AY-3-8912, Microdisc intégré, ACIA et vidéo ULA (DVI).
// Dérivé de platforms/rp2040/systems/oric/src/oric.c de reload-emulator.
//
// Clé USB (FAT/exFAT) : images .dsk (MFM_DISK) dans les lecteurs A à D, lues
// et écrites piste par piste (wd1793_insert_streamed_file), images .rom en banque
// (cartouches, en RAM) ; menu à l'écran (F1, src/osd) et TELESTRA.CFG. Les
// fichiers passent par neo_storage du socle (volume 0 = la clé, pilote FatFs).
//
// Réseau (variante standard) : volume 1 de neo_storage, client TNFS du socle
// par le second port série (TNFS) du modem Wi-Fi Neo6502picowifi ; serveur
// réglé par « reseau=hôte[:port] » dans TELESTRA.CFG, fichiers nommés
// « net:/NOM » (menu, a= … d=, bank1= … bank7=, instantanés).
//
// Télématique : un PicoWiFiModemUSB (modem Hayes en USB CDC, port série 0 de
// neo_cdc_serial.c du socle) sert de ligne au
// Minitel émulé sur la prise de l'ACIA (devices/minitel_port.h) : appels
// entrants (RING -> sonnerie sur CB1 du VIA 2, TELEMATIC en serveur) et
// sortants (ATD, émulation Minitel). Réglages facultatifs dans TELESTRA.CFG à
// la racine de la clé : « dial=hôte:port » et « listen=port ».
//
// Touches : F1 = menu (disquettes, cartouches), F2 = enregistrer un instantané,
// F3 = reprendre le dernier, F11 = NMI, F12 = RESET, Windows gauche = FUNCT,
// Pause = retour au firmware Neo6502 (multi-boot, fichiers refermés d'abord).
// Manette USB = joystick droit (la 2e : joystick gauche).
//
// ## Licence zlib/libpng
//
// Copyright (c) 2023 Veselin Sladkov (oric.c, dont ce fichier est dérivé)
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

#define CHIPS_IMPL

#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>

#include <pico/platform.h>
#include "pico/stdlib.h"

#include "roms/telestrat_roms.h"

// Code exécuté à chaque cycle en RAM : depuis la flash (XIP), il déborderait
// le cache de 16 Ko (même choix que le BBC de reload-emulator)
#define TELESTRAT_HOT __attribute__((section(".time_critical.telestrat")))
#define CHIPS_HOT     __attribute__((section(".time_critical.telestrat")))
// Journal des premiers accès en $03xx (comparaison avec la trace du banc) :
// 8 Ko de RAM, seulement si compilé avec -DTELESTRAT_DIAG_IO
#ifdef TELESTRAT_DIAG_IO
#define DIAG_IO_N 1024
volatile uint32_t diag_io[DIAG_IO_N][2];  // cycle ; adresse | R/W << 16 | donnée << 24
volatile uint32_t diag_io_n;
#define TELESTRAT_IO_HOOK(t, a, rw, d)                                                      \
    do {                                                                                    \
        if (diag_io_n < DIAG_IO_N) {                                                        \
            diag_io[diag_io_n][0] = (t);                                                    \
            diag_io[diag_io_n][1] = (a) | ((uint32_t)(rw) << 16) | ((uint32_t)(d) << 24);   \
            diag_io_n++;                                                                    \
        }                                                                                   \
    } while (0)
#endif
#include "chips/chips_common.h"
// Bus du vrai 65C02 (Olimex Neo6502) : pilote du socle (v0.16.33, étape 5 de
// la fusion), cycle en accès SIO directs intégré au tick (wdc65C02bus.h ;
// WDC65C02_BUS_PIO : par la PIO, à mesurer sur carte)
#include "chips/wdc65C02cpu.h"
#include "chips/wdc65C02bus.h"
#define MOS6502CPU_DESC_T int
#include "chips/via6522.h"
// AY en flash (appelé tous les 64 cycles) : en RAM, +1 Ko sans gain de charge mesuré
#define AY38910_HOT
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
#include "devices/hid_media.h"
// Police du menu en RAM : lue par le cœur 1 à chaque ligne affichée
#define OSD_FONT_SECTION __attribute__((section(".time_critical.osd_font")))
// Rendu du menu (osd.h du socle) en RAM, un seul exemplaire : en ligne, il
// était recopié dans core1_main à chaque appel (+1,4 Ko de RAM)
#define OSD_NOINLINE
#define OSD_HOT __attribute__((section(".time_critical.osd")))
#include "osd/osd_menu.h"
#include "osd/osd_config.h"
#include "systems/telestrat.h"
#include "systems/telestrat_state.h"
#include "osd/rom_pool.h"
#include "osd/rom_builtin.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdio_uart.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"

#include "tmds_encode.h"

#define TELESTRAT_VIDEO_RAM __not_in_flash("video")
#include "telestrat_video.h"
#include "telestrat_frame.h"

#include "common_dvi_pin_configs.h"
#include "dvi.h"
#include "neo_video.h"   // Démarrage commun : horloge, DVI (verrous dédiés), cœur 1 (socle)
#include "dvi_serialiser.h"

#include "audio.h"

#include "tusb.h"
#ifdef TELESTRAT_FLASH_DISK_H
#include "telestrat_flash_disk.h"
#endif
#include "neo_multiboot.h"
// Fichiers de la clé : volumes neo_storage du socle (volume 0 = la clé USB)
#define NEO_STORAGE_IMPL
#include "devices/neo_storage.h"
#include "devices/neo_writer.h"
extern const neo_storage_ops_t neo_storage_fatfs_ops;  // neo_storage_fatfs.c du socle
// Volume Réseau (TNFS) : variante standard seulement (la RAM 64 Ko n'a pas la
// place : client, trames et dialogue AT, ~3 Ko ; un seul port série USB)
#ifndef TELESTRAT_RAM64K
#define TELESTRAT_NET 1
#define NEO_TNFS_IMPL
#include "devices/neo_tnfs.h"
#define NEO_ESP_AT_IMPL
#include "devices/neo_esp_at.h"
#define NEO_DGRAM_SERIAL_IMPL
#include "devices/neo_dgram_serial.h"
#endif
// Modem sur le hub USB : ports série de neo_cdc_serial.c (socle), numérotés par
// interface : 0 = le modem AT (ligne Minitel, RS232), 1 = son port TNFS
bool neo_cdc_ready(void);
bool neo_cdc_port_ready(int port);
uint32_t neo_cdc_mount_count(void);
bool neo_cdc_write(void *ctx, const uint8_t *p, int n);
int neo_cdc_read_byte(void *ctx, uint32_t timeout_us);

// Mode vidéo : 960x544 à 372 MHz sous 1,30 V par défaut (réglage de
// reload-emulator, validé sur la carte Neo6502 par le BBC : +26 % de temps de
// calcul) ; -DTELESTRAT_VIDEO_480 : 800x480 à 295,2 MHz sous 1,20 V.
#ifdef TELESTRAT_VIDEO_480
#define FRAME_WIDTH  800
#define FRAME_HEIGHT 480
#define VREG_VSEL    VREG_VOLTAGE_1_20
#define DVI_TIMING   dvi_timing_800x480p_60hz
#else
#define FRAME_WIDTH  960
#define FRAME_HEIGHT 544
#define VREG_VSEL    VREG_VOLTAGE_1_30
#define DVI_TIMING   dvi_timing_960x544p_60hz
#endif

// Menu à l'écran : pleine résolution de sortie (960 x 272 lignes de tampon),
// pas en 800x480. Sans lui (TELESTRAT_OSD absent), ni menu, ni instantanés par
// les touches F1 à F3, ni bandeaux : osd_open reste faux.
#if FRAME_WIDTH >= OSD_WIDTH && FRAME_HEIGHT / 2 >= OSD_LINES
#define TELESTRAT_OSD 1
#endif

typedef struct {
    telestrat_t telestrat;
} state_t;

// Mis à zéro au démarrage par crt0 (.bss). L'attribut __not_in_flash() le
// faisait passer en .data : environ 102 Ko de zéros stockés dans l'image
// flash, puis recopiés au démarrage (F-06).
state_t state;

/*-- Télématique : Minitel sur l'ACIA, ligne sur le modem USB ---------------*/

static minitel_port_t minitel;
static hayes_line_t modem;
static modem_mux_t mux;  // le modem suit PA4 : prise Minitel (Hayes géré ici) ou RS232 (octets bruts)
static bool modem_present = false;  // port 0 du modem monté (modem_watch)
static char cfg_dial[64] = "";
static int cfg_listen = 0;
static bool cfg_rs232_uext = false;  // TELESTRA.CFG rs232=uext : prise RS232 sur l'UART de l'UEXT
// (ces réglages reviennent à leur valeur par défaut à chaque relecture de TELESTRA.CFG)

// Octets vers le modem : mis en file, envoyés par modem_flush entre deux
// tranches d'émulation. neo_cdc_write peut attendre jusqu'à 1 s en faisant
// tourner tuh_task (rappels du clavier : RESET, instantané…) : jamais depuis
// l'ACIA, au milieu d'un cycle du 65C02
static byte_fifo_t modem_txq;

static void modem_write(void *ctx, const uint8_t *data, uint32_t len) {
    (void)ctx;
    if (!modem_present) return;
    for (uint32_t i = 0; i < len; i++) byte_fifo_push(&modem_txq, data[i]);
}

// À VÉRIFIER (F-20) : neo_cdc_write attend jusqu'à 1 s (rappel de lecture tuh_task
// compris) et modem_flush est appelée 20 fois par trame (une par tranche de
// 1 ms) : avec un modem qui ne lit plus ses octets, la trame pourrait dépasser
// 20 ms. Non mesuré. Essai : modem branché mais figé (ou tx rempli), relever
// diag_frame_us_max par la sonde SWD (docs/SONDE_SWD.md) pendant que le
// Telestrat émet sur la prise Minitel.
static void modem_flush(void) {
    uint32_t n;
    const uint8_t *p;
    while (modem_present && (p = byte_fifo_peek(&modem_txq, &n), n > 0)) {
        if (!neo_cdc_write((void *)0, p, (int)n)) break;  // modem parti : file vidée au retrait
        byte_fifo_drop(&modem_txq, n);
    }
    if (!modem_present) byte_fifo_init(&modem_txq);
}

// À chaque trame, hors de tuh_task (neo_cdc_serial.c a les rappels de
// montage) : modem branché -> ligne initialisée ; retiré -> ligne coupée
static void modem_watch(void) {
    const bool ready = neo_cdc_ready();
    if (ready == modem_present) return;
    modem_present = ready;
    if (ready) {
        modem_mux_attach(&mux, modem_write, NULL, cfg_dial, cfg_listen);
        printf("Modem USB branché\n");
    } else {
        modem_mux_detach(&mux);
    }
}

// Périphériques activés (menu, TELESTRA.CFG « impression=oui|non »,
// « modem=oui|non ») : modem coupé = ligne coupée (ni sonnerie, ni appel,
// ni données, prise Minitel et RS232 vers le modem) ; imprimante coupée =
// sortie jetée
static bool modem_enabled = true;
// Cassette (menu, TELESTRA.CFG « cassette_rapide=oui|non »,
// « cassette_moteur=relais|toujours ») : CLOAD du BASIC 1.1 accéléré (ROM des
// emplacements patchée, oric_tape_turbo.h) ; moteur toujours en marche
// (câble sans relais)
static bool tape_turbo = false;
// TELESTRA.CFG « demarrage=choix » (page de démarrage) ou « demarrage=ID »
// (profil appliqué au premier montage de la clé : rom_builtin.h)
static char cfg_boot[ROM_USER_LABEL];
// Profils de la clé (« profil=Libellé;bank7=…;… ») : libellés ; la ligne
// est relue dans TELESTRA.CFG quand on en choisit un
static char user_label[ROM_USER_PROFILES][ROM_USER_LABEL];
static int user_n;
static bool tape_motor_always = false;
static bool printer_enabled = true;

// Octets reçus du modem (port 0), sans attente
static void modem_poll(void) {
    if (!modem_present) return;
    // Pas plus d'octets que la file ne peut en garder (P-02) : le reste attend
    // dans le CDC au lieu d'être jeté
    while (modem_mux_rx_room(&mux) > 0) {
        const int c = neo_cdc_read_byte((void *)0, 0);
        if (c < 0) break;
        modem_mux_feed(&mux, (uint8_t)c);
    }
}

static void read_config(void);

static void minitel_tx(uint8_t data, void *user_data) {
    (void)user_data;
    minitel_port_from_telestrat(&minitel, data);
}

static int minitel_rx(void *user_data) {
    (void)user_data;
    return minitel_port_to_telestrat(&minitel);
}

/*-- Prise RS232 --------------------------------------------------------------*/
// Par défaut vers le modem USB (PicoWiFiModemUSB), partagé avec la prise
// Minitel par modem_mux.h : le logiciel du Telestrat lui parle Hayes
// directement. TELESTRA.CFG rs232=uext : vers l'UART0 du connecteur UEXT,
// broches de la carte olimex_neo6502 (pico-sdk ; firmware officiel du
// Neo6502, serial.cpp) : TX GPIO 28 (UEXT 3), RX GPIO 29 (UEXT 4). L'UART
// prend le format programmé dans l'ACIA (TELEMON : 9600 bauds 8N1), qui
// cadence déjà émission et réception : la FIFO ne déborde pas et l'émission
// ne bloque pas.
#ifdef TELESTRAT_RS232_UART
#define RS232_UART   uart0
#define RS232_TX_PIN 28
#define RS232_RX_PIN 29

static uint16_t rs232_regs = 0xFFFF;  // commande << 8 | contrôle appliqués
static bool rs232_uart_on = false;

// L'UART cesse de porter les messages (stdio) pour devenir la prise RS232
static void rs232_uart_init(void) {
    if (rs232_uart_on) return;
    rs232_uart_on = true;
    stdio_set_driver_enabled(&stdio_uart, false);
    uart_init(RS232_UART, 9600);
    gpio_set_function(RS232_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RS232_RX_PIN, GPIO_FUNC_UART);
    uart_set_fifo_enabled(RS232_UART, true);
}

// Prise RS232 rendue au modem : l'UART reprend les messages (stdio) à
// 115200 bauds, valeur par défaut du SDK. À confirmer sur carte (changement de
// clé avec et sans rs232=uext).
static void rs232_uart_exit(void) {
    if (!rs232_uart_on) return;
    rs232_uart_on = false;
    rs232_regs = 0xFFFF;
    uart_deinit(RS232_UART);
    stdio_uart_init();
}

static void rs232_uart_config(void) {
    const mos6551acia_t *a = &state.telestrat.acia;
    uint16_t regs = (uint16_t)(a->command << 8 | a->control);
    if (regs == rs232_regs) return;
    rs232_regs = regs;
    mos6551acia_format_t f = mos6551acia_format(a);
    // Marque et espace : absents de l'UART du RP2040, émis sans parité
    static const uart_parity_t parity[5] = {UART_PARITY_NONE, UART_PARITY_ODD, UART_PARITY_EVEN, UART_PARITY_NONE,
                                            UART_PARITY_NONE};
    uart_set_baudrate(RS232_UART, f.baud);
    uart_set_format(RS232_UART, f.data_bits, f.stop_bits, parity[f.parity]);
}
#endif

static void rs232_tx(uint8_t data, void *user_data) {
    (void)user_data;
#ifdef TELESTRAT_RS232_UART
    if (cfg_rs232_uext) {
        rs232_uart_config();
        uart_putc_raw(RS232_UART, (char)data);
        return;
    }
#endif
    if (modem_enabled) modem_mux_rs232_send(&mux, data);
}

static int rs232_rx(void *user_data) {
    (void)user_data;
#ifdef TELESTRAT_RS232_UART
    if (cfg_rs232_uext) {
        rs232_uart_config();
        return uart_is_readable(RS232_UART) ? (uint8_t)uart_getc(RS232_UART) : -1;
    }
#endif
    return modem_enabled ? modem_mux_rs232_recv(&mux) : -1;
}

/*-- Imprimante : fichier de la clé -------------------------------------------*/
// Octets de l'imprimante (VIA 1 : STROBE, ACK) mis en file par l'émulation,
// ajoutés à la trame suivante au fichier TELESTRA.CFG « imprimante=NOM »
// (IMPRIM.TXT par défaut ; vide : pas d'impression) à la racine de la clé.
static byte_fifo_t printer_fifo;
static char cfg_printer[48] = "IMPRIM.TXT";
static neo_file_t printer_file;
static bool printer_open = false;

static void printer_out(uint8_t data, void *user_data) {
    (void)user_data;
    if (!printer_enabled) return;
    byte_fifo_push(&printer_fifo, data);
}

// File presque pleine : ACK retenu, l'Oric attend (telestrat_printer_resume)
#define PRINTER_FIFO_HIGH (BYTE_FIFO_SIZE - 16)
static bool printer_busy(void *user_data) {
    (void)user_data;
    return byte_fifo_count(&printer_fifo) >= PRINTER_FIFO_HIGH;
}

/*-- Imprimante rendue : Epson FX-80 (PNG), table traçante MCP-40 (SVG) --------*/
// TELESTRA.CFG « imprimante_type=texte|fx80|mcp40 » (menu : Entrée sur
// Imprimante). Fichiers IMPR0001.PNG, IMPR0002.SVG… à la racine de la clé,
// écrits au fil de l'eau ; fin de travail (page ou tracé terminé) au saut de
// page, après 10 s sans octet, à l'ouverture du menu. Variante RAM 64 Ko :
// texte seulement (la bande de la FX-80 prend 5 Ko).
#ifdef TELESTRAT_RAM64K
#define PRINTER_TYPES 1
#else
#define PRINTER_TYPES OSD_PRINTER_TYPES
#define PRINTER_RENDER
#endif
#define PRINTER_IDLE_FRAMES 500
static int printer_type = OSD_PRINTER_TEXT;
#ifdef PRINTER_RENDER
static union {
    fx80_t fx;
    mcp40_t mcp;
} prn;
static neo_writer_t render_w;
static uint8_t render_buf[512];  // une écriture de la clé (synchronisée) par 512 octets
static bool render_open = false;
static char render_name[16];    // dernier fichier ouvert
static uint16_t render_next = 1;
static int printer_idle = -1;   // trames sans octet (-1 : pas de travail en cours)

static bool usb_exists(const char *name);

static bool render_file_open(void *ctx, const char *ext) {
    (void)ctx;
    for (; render_next <= 9999; render_next++) {
        char png[16], svg[16];
        snprintf(png, sizeof(png), "IMPR%04u.PNG", render_next);
        snprintf(svg, sizeof(svg), "IMPR%04u.SVG", render_next);
        if (usb_exists(png) || usb_exists(svg)) continue;
        snprintf(render_name, sizeof(render_name), "IMPR%04u.%s", render_next, ext);
        render_next++;
        render_open = neo_writer_open(&render_w, NEO_VOL_USB, render_name, render_buf, sizeof(render_buf));
        if (!render_open) render_name[0] = 0;
        return render_open;
    }
    return false;
}

static void render_file_write(void *ctx, const void *data, uint32_t len) {
    (void)ctx;
    if (render_open) neo_writer_write(&render_w, data, len);
}

static void render_file_seek(void *ctx, uint32_t pos) {
    (void)ctx;
    if (render_open) neo_writer_seek(&render_w, pos);
}

static void render_file_close(void *ctx) {
    (void)ctx;
    if (render_open) neo_writer_close(&render_w);
    render_open = false;
}

static const printer_out_t render_out = {render_file_open, render_file_write, render_file_seek, render_file_close, NULL};

static void render_init(void) {
    if (printer_type == OSD_PRINTER_FX80) fx80_init(&prn.fx, &render_out, osd_font);
    else if (printer_type == OSD_PRINTER_MCP40) mcp40_init(&prn.mcp, &render_out);
    printer_idle = -1;
}

// Fin de travail ; false : la FX-80 écrit encore (réessayer à la trame suivante)
static bool render_job_end(void) {
    if (printer_idle < 0) return true;
    if (printer_type == OSD_PRINTER_FX80 && !fx80_finish(&prn.fx)) return false;
    if (printer_type == OSD_PRINTER_MCP40) mcp40_finish(&prn.mcp);
    printer_idle = -1;
    return true;
}
#endif

// Modèle choisi (menu, TELESTRA.CFG) : travail en cours abandonné tel quel
static void printer_select(int type) {
    if (type < 0 || type >= PRINTER_TYPES || type == printer_type) return;
#ifdef PRINTER_RENDER
    if (printer_type == OSD_PRINTER_MCP40) mcp40_finish(&prn.mcp);
    render_file_close(NULL);
    printer_type = type;
    render_init();
#else
    printer_type = type;
#endif
}

// Volume : celui de audio.c de reload (touches multimédia du clavier par
// hid_media_key_down, gain appliqué dans audio_push_sample)
static uint32_t volume_serial = 0;    // audio_volume_serial() déjà vu
static int volume_banner_frames = 0;  // bandeau du volume encore affiché (trames)

static void audio_callback(const uint8_t sample, void *user_data) {
    (void)user_data;
    audio_push_sample(sample);
}

// Emplacements de banque en RAM (src/osd/rom_pool.h) : un par ROM intégrée
// (copiée depuis la flash au démarrage), plus un supplémentaire pour une
// cartouche de la clé dans une banque vide. Variante RAM 64 Ko : pas de
// supplémentaire (sa RAM de banques prend la place).
#ifdef TELESTRAT_RAM64K
#define ROM_BUILTIN     2
#define ROM_EXTRA_SLOTS 0
#else
#define ROM_BUILTIN     4
#define ROM_EXTRA_SLOTS 1
#endif
static uint8_t rom_slots[ROM_BUILTIN + ROM_EXTRA_SLOTS][OSD_BANK_BYTES];
static rom_pool_t pool;

// Banques : notice « Extension RAM 64 Ko » (F. Broche, 1987), chapitre IV
static telestrat_desc_t telestrat_desc(void) {
    telestrat_desc_t d = {
        // Cadence réelle du PWM (diviseur au 1/16 : 22 017 Hz à 372 MHz) : à
        // 22 050 exacts, le tampon se remplirait et jetterait des échantillons
        .audio = {.callback = {.func = audio_callback}, .sample_rate = (int)((audio_pwm_rate_q8(22050) + 128) >> 8)},
        .minitel = {.tx = minitel_tx, .rx = minitel_rx},
        .rs232 = {.tx = rs232_tx, .rx = rs232_rx},
        .printer = {.func = printer_out, .busy = printer_busy, .user_data = NULL},
    };
    rom_pool_init(&pool, rom_slots, ROM_BUILTIN + ROM_EXTRA_SLOTS);
    d.banks[0].type = TELESTRAT_BANK_RAM;
#ifdef TELESTRAT_RAM64K
    // Cartouche RAM 64 Ko dans le port droit (banques 1-4)
    for (int i = 1; i <= 4; i++) d.banks[i].type = TELESTRAT_BANK_RAM;
#else
    d.banks[2] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&pool, 2, telestrat_teleass)};
    d.banks[3] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&pool, 3, telestrat_telematic)};
#endif
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&pool, 6, telestrat_hyperbas)};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&pool, 7, telestrat_telemon24)};
    return d;
}

/*-- Clé USB : disquettes, cartouches, menu (F1) ------------------------------*/
// La clé (stockage de masse USB, FAT/exFAT, monté par msc_app.c de reload)
// n'est pas vue par le Telestrat : ses images .dsk vont dans les lecteurs A à
// D du Microdisc (lues et écrites piste par piste), ses images .rom dans les
// banques (cartouches, copiées en RAM). Réglages dans TELESTRA.CFG : a= … d=,
// bank1= … bank7= (src/osd/osd_config.h), réécrits par le menu.


static osd_menu_t menu;
static volatile bool osd_open = false;
// Fonctions du menu et des bandeaux, appelées seulement avec TELESTRAT_OSD
#ifdef TELESTRAT_OSD
#define OSD_ONLY
#else
#define OSD_ONLY __attribute__((unused))
#endif
// Menu et bandeau vus par le cœur 1. Chaque dessin se fait dans un tampon que
// le cœur 1 n'affiche pas ; il n'est publié (pointeur, derrière une barrière
// du compilateur) qu'une fois fini, et le cœur 1 prend ces pointeurs au début
// de chaque image : plus de menu vu à moitié redessiné (F-17, F-18). Les
// détails de matériel (ordre des écritures entre cœurs, durée du noir à
// l'ouverture et à la fermeture) sont à confirmer sur carte.
static volatile uint32_t core1_frames;  // images rendues par le cœur 1 (lui seul l'incrémente)
#ifdef TELESTRAT_OSD
// Deux surfaces de menu dans l'image du Telestrat (inutile tant que l'émulation
// est en pause : elle est redessinée à la fermeture). Les 2 x 12 Ko laissent
// ~2,4 Ko ; les zones de travail (TELESTRA.CFG, instantanés) prennent la
// surface qui n'est pas affichée, redessinée en fin d'action.
#define MENU_SURF_BYTES ((sizeof(osd_surface_t) + 7) & ~(size_t)7)
_Static_assert(2 * MENU_SURF_BYTES <= sizeof(state.telestrat.fb), "deux surfaces de menu hors de l'image");
static osd_surface_t *volatile osd_shown;  // surface affichée (NULL : image du Telestrat)
static volatile bool osd_blank;            // cœur 1 : lignes noires (image ou surfaces en cours de réécriture)
static uint32_t osd_flip_mark;             // core1_frames à la dernière publication
#define MENU_SURF(i) ((osd_surface_t *)(state.telestrat.fb + (i) * MENU_SURF_BYTES))

// Attend que le cœur 1 ait fini n images (borné : cœur 1 arrêté ou sondes SWD)
static void core1_wait_frames(uint32_t n) {
    const uint32_t start = core1_frames, t0 = time_us_32();
    while (core1_frames - start < n && time_us_32() - t0 < 100000) tight_loop_contents();
}

// Surface où dessiner : celle que le cœur 1 n'affiche pas
static osd_surface_t *menu_back(void) { return osd_shown == MENU_SURF(0) ? MENU_SURF(1) : MENU_SURF(0); }

// Publication d'une surface dessinée
static void menu_publish(osd_surface_t *surf) {
    __compiler_memory_barrier();
    osd_flip_mark = core1_frames;
    osd_shown = surf;
}
#endif
#ifdef TELESTRAT_NET
static char cfg_net[64];    // TELESTRA.CFG reseau= (volume Réseau, plus bas)
static char net_label[64];  // serveur monté (menu)
static void net_scan(int item);
#endif
// Cassette (.tap de la clé) et son bandeau, incrusté sous l'image pendant que
// le moteur tourne (dessiné par le cœur 0, affiché par le cœur 1)
static neo_file_t tape_file;
static char tape_name[OSD_NAME_LEN];
// Bandeau (cassette, volume) : deux rangées, l'une affichée, l'autre dessinée
static osd_row_t banner_rows[2];
static int banner_front;                          // rangée affichée si banner_shown n'est pas NULL
static const osd_row_t *volatile banner_shown;    // NULL : pas de bandeau
static bool usb_scanned = false;
static neo_file_t drive_file[4];
static char drive_name[4][OSD_NAME_LEN];  // "" : vide ; image en flash : nom réservé
static const char FLASH_NAME[] = "(image en flash)";
static char cfg_drive[4][OSD_NAME_LEN];   // TELESTRA.CFG : a= … d=
static char cfg_bank[8][OSD_NAME_LEN];    // TELESTRA.CFG : bank1= … bank7=

extern bool msc_inquiry_complete;
void msc_poll(void);  // msc_app.c du socle : montage de la clé hors de tuh_task

// Fichier présent sur la clé (ouvert puis refermé)
static bool usb_exists(const char *name) {
    neo_file_t f;
    if (!neo_file_open(&f, NEO_VOL_USB, name, NEO_READ)) return false;
    neo_file_close(&f);
    return true;
}

// Une entrée de la racine ; false (fin du parcours) quand la liste est pleine
// et qu'un fichier de plus est trouvé (menu.truncated). Ni les entrées cachées
// ni les « ._NOM » d'AppleDouble ne sont listées (F-11).
// user : volume (OSD_VOL_NET : nom préfixé « net:/ »)
static bool usb_scan_entry(const neo_dirent_t *e, void *user) {
    const int vol = (int)(intptr_t)user;
    const char *prefix = vol == OSD_VOL_NET ? OSD_NET_PREFIX : "";
    if (e->dir || osd_name_ignored(e->name, e->hidden)) return true;
    const bool dsk = neo_name_has_ext(e->name, ".dsk"), rom = neo_name_has_ext(e->name, ".rom"), tap = neo_name_has_ext(e->name, ".tap"),
               sta = neo_name_has_ext(e->name, ".sta");
    if ((!dsk && !rom && !tap && !sta) || strlen(prefix) + strlen(e->name) >= OSD_NAME_LEN) return true;
    if (menu.nfiles >= OSD_MENU_FILES) {
        menu.truncated = true;
        return false;
    }
    osd_file_t *f = &menu.files[menu.nfiles++];
    snprintf(f->name, sizeof(f->name), "%s%s", prefix, e->name);
    f->size = e->size;
    f->kind = dsk ? OSD_FILE_DSK : tap ? OSD_FILE_TAP : sta ? OSD_FILE_STA : OSD_FILE_ROM;
    f->vol = (uint8_t)vol;
    return true;
}

// Tri par nom des fichiers from à menu.nfiles - 1 (insertion : OSD_MENU_FILES au plus)
static void files_sort(int from) {
    for (int i = from + 1; i < menu.nfiles; i++) {
        osd_file_t t = menu.files[i];
        int j = i - 1;
        while (j >= from && strcmp(menu.files[j].name, t.name) > 0) {
            menu.files[j + 1] = menu.files[j];
            j--;
        }
        menu.files[j + 1] = t;
    }
}

// Fichiers .dsk, .rom, .tap et .sta de la racine (noms longs jusqu'à OSD_NAME_LEN - 1)
static void usb_scan(void) {
    menu.nfiles = 0;
    menu.truncated = false;
    menu.usb_present =
        msc_inquiry_complete && neo_storage_list(NEO_VOL_USB, "/", usb_scan_entry, (void *)(intptr_t)OSD_VOL_USB);
    if (!menu.usb_present) return;
    files_sort(0);
    snprintf(menu.usb_label, sizeof(menu.usb_label), "Clé montée");
}

static void drive_eject(int d) {
    wd1793_eject(&state.telestrat.fdc.wd, d);  // réécrit la piste en attente
    neo_file_close(&drive_file[d]);
    drive_name[d][0] = 0;
}

// Lecteur dont le volume est parti : la piste en cache est jetée (elle ne
// pourrait plus être écrite, et une écriture sur TNFS attendrait), le fichier
// fermé (F-09)
static void drive_drop(int d) {
    wd1793_t *wd = &state.telestrat.fdc.wd;
    if (wd->cache.disk == &wd->disk[d]) wd->cache.dirty = false;
    drive_eject(d);
}

static void insert_flash_disk(void);

// Image de la clé (ou du réseau : « net:/NOM ») dans un lecteur ; false si
// illisible, invalide ou déjà ailleurs. L'image est ouverte et vérifiée avant
// d'éjecter l'ancienne : un échec la laisse en place (F-10). Même image que
// celle du lecteur : fermée d'abord (un fichier FatFs n'est pas ouvert deux
// fois en écriture), et l'image en flash de A remise si l'ouverture échoue.
static bool drive_insert(int d, const char *name) {
    for (int o = 0; o < 4; o++)
        if (o != d && !strcmp(drive_name[o], name)) return false;
    if (!strcmp(drive_name[d], name)) drive_eject(d);
    neo_file_t nf;
    const bool rw = neo_file_open_path(&nf, name, NEO_READ | NEO_WRITE);
    if (!rw && !neo_file_open_path(&nf, name, NEO_READ)) {
        if (!drive_name[0][0]) insert_flash_disk();
        return false;
    }
    uint8_t header[ORIC_DSK_HEADER_SIZE];
    int sides, tracks;
    if (nf.size < sizeof(header) || !neo_file_read(&nf, 0, header, sizeof(header)) ||
        !oric_dsk_parse_header(header, nf.size, &sides, &tracks)) {
        neo_file_close(&nf);
        if (!drive_name[0][0]) insert_flash_disk();
        return false;
    }
    drive_eject(d);
    drive_file[d] = nf;
    neo_file_t *f = &drive_file[d];
    if (!wd1793_insert_streamed(&state.telestrat.fdc.wd, d, header, f->size, neo_file_read_cb,
                                rw ? neo_file_write_cb : NULL, f)) {
        neo_file_close(f);
        if (!drive_name[0][0]) insert_flash_disk();
        return false;
    }
    snprintf(drive_name[d], sizeof(drive_name[d]), "%s", name);
    printf("Lecteur %c : %s%s\n", 'A' + d, name, rw ? "" : " (protégée)");
    return true;
}

// Image intégrée à la flash (lecture seule), dans le lecteur A
static void insert_flash_disk(void) {
#ifdef TELESTRAT_FLASH_DISK_H
    if (wd1793_insert_mem(&state.telestrat.fdc.wd, 0, (uint8_t *)telestrat_flash_disk, sizeof(telestrat_flash_disk), true)) {
        snprintf(drive_name[0], sizeof(drive_name[0]), "%s", FLASH_NAME);
        printf("Lecteur A : image en flash (%u octets, protégée)\n", (unsigned)sizeof(telestrat_flash_disk));
    }
#endif
}

static void bank_restore(int bank) { rom_pool_restore(&pool, &state.telestrat, bank); }

// Cartouche de la clé (ou du réseau) en banque ; *err : raison d'un refus
static bool bank_load(int bank, const char *name, const char **err) {
    neo_file_t f;
    if (!neo_file_open_path(&f, name, NEO_READ)) {
        *err = "fichier illisible";
        return false;
    }
    const size_t size = f.size;
    uint8_t *dst = rom_pool_claim(&pool, &state.telestrat, bank, size, err);
    bool ok = dst != NULL;
    if (ok && !neo_file_read(&f, 0, dst, (uint32_t)size)) {
        *err = "lecture impossible";
        rom_pool_abort(&pool, &state.telestrat, bank);
        ok = false;
    }
    if (ok) {
        rom_pool_commit(&pool, &state.telestrat, bank, size, name);
        printf("Banque %d : %s\n", bank, name);
    }
    neo_file_close(&f);
    return ok;
}

static bool bank_load_cb(void *ctx, int bank, const char *name, const char **err) {
    (void)ctx;
    return bank_load(bank, name, err);
}

// Cartouche désignée par un nom de TELESTRA.CFG ou d'un instantané : ROM
// intégrée (« @… ») ou fichier de la clé ou du réseau (F-12, F-32)
static bool bank_load_any(int bank, const char *name, const char **err) {
    return rom_load_named(&pool, &state.telestrat, bank, name, bank_load_cb, NULL, err);
}

// Taille d'une cartouche de la clé ou du réseau sans la charger (vérification
// préalable d'un profil, F-04)
static bool bank_probe_cb(void *ctx, const char *name, size_t *size) {
    (void)ctx;
    neo_file_t f;
    if (!neo_file_open_path(&f, name, NEO_READ)) return false;
    *size = f.size;
    neo_file_close(&f);
    return true;
}

static const char *bank_label(int b) {
    const uint8_t *r = pool.builtin[b];
    if (r == telestrat_telemon24) return "TELEMON 2.4";
    if (r == telestrat_hyperbas) return "HYPER-BASIC";
#ifndef TELESTRAT_RAM64K
    if (r == telestrat_teleass) return "TELE-ASS";
    if (r == telestrat_telematic) return "TELEMATIC";
#endif
    return state.telestrat.bank_type_orig[b] == TELESTRAT_BANK_RAM ? (b == 0 ? "RAM interne" : "RAM 16 Ko") : "";
}

static void tape_eject(void) {
    telestrat_tape_insert(&state.telestrat, 0, NULL, NULL);
    neo_file_close(&tape_file);
    tape_name[0] = 0;
}

// Cassette de la clé ou du réseau (la même : rembobinée), lue en flux
static bool tape_insert(const char *name) {
    tape_eject();
    if (!neo_file_open_path(&tape_file, name, NEO_READ)) return false;
    snprintf(tape_name, sizeof(tape_name), "%s", name);
    telestrat_tape_insert(&state.telestrat, tape_file.size, neo_file_read_cb, &tape_file);
    printf("Cassette : %s\n", name);
    return true;
}

// CSAVE : NOM.TAP à la racine de la clé (oric_tape_rec.h), écrit octet par
// octet pendant l'émulation : tamponné (une écriture de la clé par 64 octets)
static neo_writer_t rec_w;
static uint8_t rec_buf[64];
static bool rec_open(void *ctx, const char *name) {
    (void)ctx;
    neo_writer_close(&rec_w);
    const bool ok = neo_writer_open(&rec_w, NEO_VOL_USB, name, rec_buf, sizeof(rec_buf));
    printf("Cassette : enregistrement de %s%s\n", name, ok ? "" : " impossible");
    return ok;
}
static void rec_write(void *ctx, const uint8_t *data, uint32_t len) {
    (void)ctx;
    neo_writer_write(&rec_w, data, len);
}
static void rec_close(void *ctx) {
    (void)ctx;
    neo_writer_close(&rec_w);
}

// Bandeau : à chaque trame, pendant une lecture ou un enregistrement. Dessiné
// dans la rangée non affichée, publié seulement s'il a changé (F-17 : il
// était refait à chaque trame sous la lecture du cœur 1).
static OSD_ONLY void banner_update(void) {
    osd_row_t *back = &banner_rows[!banner_front];
    if (audio_volume_serial() != volume_serial) {  // touche de volume, même aux butées : 2 s
        volume_serial = audio_volume_serial();
        volume_banner_frames = 100;
    }
    if (volume_banner_frames > 0) {
        volume_banner_frames--;
        osd_volume_banner(back, audio_volume_level(), HID_VOLUME_MAX, audio_volume_muted());
    } else {
        const oric_tape_t *t = &state.telestrat.tape;
        const oric_tape_rec_t *r = &state.telestrat.tape_rec;
        if (oric_tape_rec_active(r)) {
            const uint32_t total = r->written + r->remaining;
            const int percent = !oric_tape_rec_length_known(r) ? -1 : total ? (int)(r->written * 100 / total) : 0;
            osd_tape_banner_ex(back, "Écriture", r->file, percent, r->written);
        } else if (oric_tape_running(t)) {
            osd_tape_banner(back, "Lecture", tape_name, oric_tape_percent(t));
        } else {
            banner_shown = NULL;
            return;
        }
    }
    if (banner_shown == &banner_rows[banner_front] && !memcmp(back, &banner_rows[banner_front], sizeof(*back))) return;
    __compiler_memory_barrier();  // rangée écrite avant que le cœur 1 en reçoive l'adresse
    banner_front = !banner_front;
    banner_shown = &banner_rows[banner_front];
}

// Profil de chaque entrée de menu.profile : >= 0 profil intégré (rom_profiles),
// -1 - k profil de la clé k
static int profile_map[OSD_PROFILES];

static void menu_refresh(void) {
    const telestrat_t *sys = &state.telestrat;
    menu.printer_on = printer_enabled;
    menu.printer_model = osd_printer_names[printer_type];
#ifdef PRINTER_RENDER
    if (printer_type != OSD_PRINTER_TEXT) snprintf(menu.printer_file, sizeof(menu.printer_file), "%s", render_name);
    else
#endif
        snprintf(menu.printer_file, sizeof(menu.printer_file), "%s", cfg_printer);
    menu.modem_on = modem_enabled;
    menu.net_present = neo_storage_ready(NEO_VOL_NET);
#ifdef TELESTRAT_NET
    menu.net_label = net_label;
#endif
    menu.tape_turbo = tape_turbo;
    menu.tape_motor_always = tape_motor_always;
    menu.modem_state = !modem_present           ? "absent"
                       : mux.rs232              ? "prise RS232"
                       : hayes_line_carrier(&modem) ? "en ligne"
                       : hayes_line_incoming(&modem) ? "sonnerie"
                                                    : "prêt";
    snprintf(menu.tape, sizeof(menu.tape), "%.47s", sys->tape.inserted ? tape_name : "");
    menu.tape_percent = oric_tape_percent(&sys->tape);
    menu.tape_motor = sys->tape.inserted && sys->tape.motor;
    for (int k = 0; k < ROM_BUILTINS && k < OSD_BUILTINS; k++) menu.builtin[k] = rom_builtins[k].label;
    // Profils proposés : intégrés réalisables dans ce firmware (pas de ROM
    // intégrée dans la version slot, pas d'emplacement de plus en RAM 64 Ko :
    // F-04, F-13), puis ceux de la clé
    int np = 0;
    for (int k = 0; k < ROM_PROFILES && np < OSD_PROFILES; k++) {
        const char *why;
        if (!rom_profile_check(&pool, k, &why)) continue;
        profile_map[np] = k;
        menu.profile[np++] = rom_profiles[k].label;
    }
    for (int k = 0; k < user_n && np < OSD_PROFILES; k++) {
        profile_map[np] = -1 - k;
        menu.profile[np++] = user_label[k];
    }
    while (np < OSD_PROFILES) menu.profile[np++] = NULL;
    for (int d = 0; d < 4; d++) {
        snprintf(menu.drive[d], sizeof(menu.drive[d]), "%.47s", drive_name[d]);
        menu.drive_ro[d] = drive_name[d][0] && sys->fdc.wd.disk[d].write_protected;
    }
    for (int b = 0; b < 8; b++) {
        if (pool.name[b][0]) {
            snprintf(menu.bank[b], sizeof(menu.bank[b]), "%.47s", rom_builtin_label(pool.name[b]));
            menu.bank_kind[b] = pool.name[b][0] == '@'     ? OSD_BANK_ROM
                                : osd_is_net(pool.name[b]) ? OSD_BANK_ROM_NET
                                                           : OSD_BANK_ROM_USB;
        } else {
            snprintf(menu.bank[b], sizeof(menu.bank[b]), "%s", bank_label(b));
            menu.bank_kind[b] = sys->bank_type[b] == TELESTRAT_BANK_RAM   ? OSD_BANK_RAM
                                : sys->bank_type[b] == TELESTRAT_BANK_ROM ? OSD_BANK_ROM
                                                                          : OSD_BANK_EMPTY;
        }
    }
}

// Messages du menu (format printf)
static void menu_say(bool error, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void menu_say(bool error, const char *fmt, ...) {
    char msg[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    osd_menu_message(&menu, error, msg);
}

// TELESTRA.CFG relu en entier (CFG_BUF - 1 octets au plus) puis réécrit : un
// fichier plus long, ou illisible, est refusé au lieu d'être tronqué (F-03). Appelée menu
// ouvert : ses deux tampons sont pris dans la surface du menu qui n'est pas
// affichée (redessinée en fin d'action).
#define CFG_BUF 4096

// Zone de travail des actions du menu : la surface qui n'est pas affichée
static void *menu_work(void) {
#ifdef TELESTRAT_OSD
    return menu_back();
#else
    return state.telestrat.fb;  // sans menu : jamais appelée
#endif
}
#define MENU_WORK_BYTES (sizeof(state.telestrat.fb) / 2)  // une surface (12 240 octets) au moins
_Static_assert(2 * CFG_BUF <= MENU_WORK_BYTES, "tampons de TELESTRA.CFG hors de la zone de travail");

static void config_save(void) {
    char *old = (char *)menu_work();
    char *out = old + CFG_BUF;
    neo_file_t f;
    uint32_t n = 0;
    if (neo_file_open(&f, NEO_VOL_USB, "TELESTRA.CFG", NEO_READ)) {  // absent : fichier créé
        n = f.size;
        const bool fits = n < CFG_BUF, ok = fits && (n == 0 || neo_file_read(&f, 0, (uint8_t *)old, n));
        neo_file_close(&f);
        if (!fits || !ok) {
            if (!fits) menu_say(true, "TELESTRA.CFG fait plus de %d octets : non réécrit", CFG_BUF - 1);
            else menu_say(true, "TELESTRA.CFG illisible : non réécrit");
            return;
        }
    }
    old[n] = 0;
    const char *drives[4], *banks[8];
    for (int d = 0; d < 4; d++) drives[d] = strcmp(drive_name[d], FLASH_NAME) ? drive_name[d] : NULL;
    for (int b = 0; b < 8; b++) banks[b] = pool.name[b];
    const osd_options_t opt = {printer_enabled, printer_type, modem_enabled, tape_turbo, tape_motor_always, audio_volume_level()};
    const size_t len = osd_config_merge_ex(old, drives, banks, &opt, out, CFG_BUF);
    if (!len) {
        menu_say(true, "TELESTRA.CFG trop long une fois réécrit : non enregistré");
        return;
    }
    const bool saved = neo_file_save(NEO_VOL_USB, "TELESTRA.CFG", (const uint8_t *)out, (uint32_t)len);
    menu_say(!saved, saved ? "Configuration enregistrée dans TELESTRA.CFG" : "TELESTRA.CFG : écriture impossible");
}

static void menu_draw(void);

// Piste en cache écrite si le contrôleur est au repos (le socle l'écrit déjà à
// la fin de chaque commande d'écriture, WD1793_FLUSH_AT_END ; ceci couvre une
// image qui aurait été modifiée autrement, et l'arrêt par Pause). F-02.
static void disks_flush(void) {
    if (!telestrat_fdc_busy(&state.telestrat.fdc)) wd1793_flush(&state.telestrat.fdc.wd);
}

static OSD_ONLY void menu_open(void) {
#ifdef PRINTER_RENDER
    render_job_end();  // pages et tracés terminés : lisibles sur la clé
#endif
    disks_flush();
    usb_scan();
    menu_refresh();
    menu.page = OSD_PAGE_MAIN;
    menu.cursor = OSD_ITEM_RESUME;
    menu.message[0] = 0;
#ifdef TELESTRAT_OSD
    // Noir le temps que le cœur 1 quitte l'image et que la surface soit dessinée
    // dans l'image (sinon : image relue comme un menu)
    osd_blank = true;
    __compiler_memory_barrier();
    core1_wait_frames(2);
    menu_draw();
    __compiler_memory_barrier();
    osd_open = true;
    osd_blank = false;
#else
    osd_open = true;  // jamais atteint : sans menu, aucun appelant (F-05)
#endif
}

// Liste de fichiers ou état des supports changés menu ouvert (clé retirée ou
// remise, modem perdu) : page principale (les index du sélecteur ne valent
// plus) et redessin (F-16)
static void menu_files_changed(void) {
    if (!osd_open) return;
    menu.page = OSD_PAGE_MAIN;
    menu_refresh();
    menu_draw();
}

// Options de la cassette appliquées : système, BASIC 1.1 des emplacements
// patché ou remis (aussi après chaque changement de cartouche) ; retourne le
// nombre de BASIC 1.1 trouvés
static int tape_options_apply(void) {
    telestrat_tape_options(&state.telestrat, tape_turbo, tape_motor_always);
    return oric_turbo_apply_all(rom_slots, ROM_BUILTIN + ROM_EXTRA_SLOTS, tape_turbo);
}

/*-- Instantanés (src/systems/telestrat_state.h) -------------------------------*/
// Fichiers ETATnnnn.STA à la racine de la clé. Menu ouvert : le fichier, sa
// position et le texte des cartouches sont pris dans la zone de travail du
// menu (comme config_save), sans RAM de plus.
typedef struct {
    neo_file_t file;
    uint32_t pos;  // lecture : position suivante
    char info[TELESTRAT_STATE_INFO_MAX + 1];
} state_work_t;

_Static_assert(8 + sizeof(state_work_t) <= MENU_WORK_BYTES, "zone de travail des instantanés hors de la zone de travail");

static state_work_t *state_work(void) { return (state_work_t *)(((uintptr_t)menu_work() + 7) & ~(uintptr_t)7); }

static bool state_write(void *ctx, void *data, uint32_t len) {
    return neo_file_append(&((state_work_t *)ctx)->file, data, len);
}

static bool state_read(void *ctx, void *data, uint32_t len) {
    state_work_t *w = ctx;
    if (!neo_file_read(&w->file, w->pos, data, len)) return false;
    w->pos += len;
    return true;
}

// Cartouches (bank1= … bank7=, vide : origine) et supports (pour information)
static void state_info(char *out, size_t cap) {
    size_t n = 0;
    for (int b = 1; b < 8 && n < cap; b++) n += (size_t)snprintf(out + n, cap - n, "bank%d=%s\n", b, pool.name[b]);
    for (int d = 0; d < 4 && n < cap; d++)
        if (drive_name[d][0]) n += (size_t)snprintf(out + n, cap - n, "%c=%s\n", 'a' + d, drive_name[d]);
    if (n < cap && neo_file_is_open(&tape_file)) snprintf(out + n, cap - n, "cassette=%s\n", tape_name);
}

// Nom du fichier écrit dans name (« ETATnnnn.STA ») ; en cas d'échec name est
// rendu tel quel à l'appelant, qui ne doit pas le garder (F-24)
static bool state_save(char *name, size_t cap, const char **err) {
    state_work_t *w = state_work();
    bool free_name = false;
    for (int k = 1; k <= 9999 && !free_name; k++) {
        snprintf(name, cap, "ETAT%04d.STA", k);
        free_name = !usb_exists(name);
    }
    if (!free_name || !neo_file_open(&w->file, NEO_VOL_USB, name, NEO_WRITE | NEO_CREATE)) {
        *err = "écriture impossible";
        return false;
    }
    state_info(w->info, sizeof(w->info));
    const bool ok = telestrat_state_save(&state.telestrat, w->info, state_write, w, err);
    neo_file_close(&w->file);
    if (!ok) neo_file_remove(NEO_VOL_USB, name);
    return ok;
}

// Instantané de la clé, ou du réseau (« net:/NOM »)
static bool state_load(const char *name, const char **err) {
    state_work_t *w = state_work();
    if (!neo_file_open_path(&w->file, name, NEO_READ)) {
        *err = "illisible";
        return false;
    }
    w->pos = 0;
    bool ok = telestrat_state_load_info(state_read, w, w->info, sizeof(w->info), err);
    // Fermé pendant le chargement des cartouches (un fichier ouvert à la fois
    // pour les actions du menu), rouvert ensuite à la même position
    neo_file_close(&w->file);
    for (char *line = ok ? strtok(w->info, "\n") : NULL; ok && line; line = strtok(NULL, "\n")) {
        for (int b = 1; b < 8; b++) {
            char key[8];
            snprintf(key, sizeof(key), "bank%d", b);
            const char *v = osd_config_value(line, key);
            if (!v || !strcmp(v, pool.name[b])) continue;
            if (!v[0]) {
                rom_pool_restore(&pool, &state.telestrat, b);
            } else if (!bank_load_any(b, v, err)) {
                if (v[0] != '@')  // une ROM intégrée garde sa cause (inconnue, pas de place…)
                    *err = osd_is_net(v) ? "cartouche de l'instantané absente du réseau"
                                         : "cartouche de l'instantané absente de la clé";
                ok = false;
            }
        }
    }
    if (ok && !neo_file_open_path(&w->file, name, NEO_READ)) {
        *err = "illisible";
        ok = false;
    }
    if (ok && !telestrat_state_load_machine(&state.telestrat, state_read, w, err)) {
        telestrat_cold_reset(&state.telestrat);  // machine incohérente
        ok = false;
    }
    neo_file_close(&w->file);
    return ok;
}

static void menu_close(void) {
    tape_options_apply();  // cartouches peut-être changées
    state.telestrat.screen_dirty = true;  // l'image a servi de surface au menu
#ifdef TELESTRAT_OSD
    // L'image est redessinée avant de reparaître (pas une trame de menu relu
    // comme une image) ; noir le temps que le cœur 1 quitte les surfaces
    osd_blank = true;
    __compiler_memory_barrier();
    core1_wait_frames(2);
    osd_shown = NULL;
    telestrat_screen_update(&state.telestrat);
    __compiler_memory_barrier();
    osd_open = false;
    osd_blank = false;
#else
    osd_open = false;
#endif
}

// Ligne de TELESTRA.CFG lue par osd_config_line : fin de ligne et espaces de
// fin retirés ; une ligne plus longue que le tampon est coupée et sa suite
// sautée (*cut), jamais lue comme une autre ligne (F-15, F-32)
#define CFG_LINE 160
static bool cfg_line(neo_file_t *f, uint32_t *pos, char *line, bool *cut) {
    return osd_config_line(neo_file_read_cb, f, f->size, pos, line, CFG_LINE, cut);
}

// Valeur de la k-ième ligne « profil= » de TELESTRA.CFG, copiée dans line
// (CFG_LINE octets) ; NULL et *err sinon
static const char *user_profile_line(int k, char *line, const char **err) {
    neo_file_t f;
    *err = "profil absent de TELESTRA.CFG";
    if (!neo_file_open(&f, NEO_VOL_USB, "TELESTRA.CFG", NEO_READ)) return NULL;
    const char *v = NULL;
    uint32_t pos = 0;
    int n = 0;
    bool cut;
    while (cfg_line(&f, &pos, line, &cut)) {
        const char *p = osd_config_value(line, "profil");
        if (!p || n++ != k) continue;
        if (cut) *err = "ligne de profil trop longue (159 caractères au plus)";
        else v = p;
        break;
    }
    // Fichier fermé avant de charger les cartouches (un seul ouvert à la fois)
    neo_file_close(&f);
    return v;
}

// Démarrage sur un profil : intégré (builtin >= 0, indice de rom_profiles) ou
// de la clé (user >= 0, k-ième ligne « profil= »). Vérifié en entier avant
// toute modification (F-04) ; *touched : des banques ont changé, le démarrage
// à froid est à faire même si le profil a échoué en route.
static bool profile_run(int builtin, int user, bool *touched, const char **err) {
    *touched = false;
    if (builtin >= 0) {
        if (!rom_profile_check(&pool, builtin, err)) return false;
        *touched = true;
        return rom_profile_apply(&pool, &state.telestrat, builtin, err);
    }
    char line[CFG_LINE];
    const char *v = user_profile_line(user, line, err);
    if (!v || !rom_user_profile_check(&pool, v, bank_probe_cb, NULL, err)) return false;
    *touched = true;
    return rom_user_profile_apply(&pool, &state.telestrat, v, bank_load_cb, NULL, err);
}

/*-- Actions du menu -----------------------------------------------------------*/

static void act_insert(osd_action_t a) {
    const char *name = menu.files[a.file].name;
    if (drive_insert(a.target, name)) menu_say(false, "Lecteur %c : %s", 'A' + a.target, name);
    else menu_say(true, "%s : image refusée (MFM_DISK, déjà en place ?)", name);
}

static void act_load_rom(osd_action_t a) {
    const char *err = "", *name = menu.files[a.file].name;
    if (bank_load(a.target, name, &err)) menu_say(false, "Banque %d : %s — RESET conseillé", a.target, name);
    else menu_say(true, "%s : %s", name, err);
}

static void act_load_builtin(osd_action_t a) {
    const char *err = "";
    if (a.file >= 0 && a.file < ROM_BUILTINS &&
        rom_pool_load_builtin(&pool, &state.telestrat, a.target, &rom_builtins[a.file], &err))
        menu_say(false, "Banque %d : %s — RESET conseillé", a.target, rom_builtins[a.file].label);
    else
        menu_say(true, "Banque %d : %s", a.target, err);
}

static void act_printer(void) {
    int type = printer_type;
    osd_printer_cycle(&printer_enabled, &type, PRINTER_TYPES);
    printer_select(type);
    if (printer_enabled) menu_say(false, "Imprimante : %s", osd_printer_names[printer_type]);
    else osd_menu_message(&menu, false, "Imprimante coupée");
}

static void act_modem(void) {
    if (modem_enabled) {
        hayes_line_hangup(&modem);  // communication en cours raccrochée (+++, ATH)
        modem_enabled = false;
    } else {
        modem_enabled = true;
        if (modem_present) modem_mux_attach(&mux, modem_write, NULL, cfg_dial, cfg_listen);
    }
    osd_menu_message(&menu, false, modem_enabled ? "Modem activé" : "Modem coupé (ligne raccrochée)");
}

// Démarrage sur un profil (page « Démarrer sur… ») ; true : appliqué, menu refermé
static bool act_profile(osd_action_t a) {
    if (a.file < 0) {
        menu_close();  // configuration de la clé
        return true;
    }
    const char *err = "";
    const int entry = a.file < OSD_PROFILES ? profile_map[a.file] : 0;
    bool touched = false;
    const bool ok = a.file < OSD_PROFILES && profile_run(entry >= 0 ? entry : -1, entry < 0 ? -1 - entry : -1, &touched, &err);
    if (ok) {
        telestrat_cold_reset(&state.telestrat);
        menu_close();
        return true;
    }
    if (touched) telestrat_cold_reset(&state.telestrat);  // banques changées à moitié : machine redémarrée
    menu_say(true, "Démarrage : %s%s", err[0] ? err : "échec", touched ? " (machine redémarrée)" : "");
    return false;
}

static void act_state_save(void) {
    char name[OSD_NAME_LEN];
    const char *err = "";
    if (state_save(name, sizeof(name), &err)) {
        snprintf(menu.state_last, sizeof(menu.state_last), "%s", name);
        menu_say(false, "Instantané enregistré : %s", name);
        usb_scan();
    } else {
        menu_say(true, "Instantané : %s", err);
    }
}

static void act_state_load(osd_action_t a) {
    const char *err = "", *name = menu.files[a.file].name;
    if (state_load(name, &err)) {
        snprintf(menu.state_last, sizeof(menu.state_last), "%s", name);
        menu_say(false, "Instantané repris : %s", name);
    } else {
        menu_say(true, "%.40s : %.50s", name, err);
    }
}

static void act_tape_insert(osd_action_t a) {
    const char *name = menu.files[a.file].name;
    if (tape_insert(name)) menu_say(false, "Cassette : %s (au début)", name);
    else menu_say(true, "%s : illisible", name);
}

static OSD_ONLY void menu_action(osd_action_t a) {
    switch (a.type) {
        case OSD_ACT_INSERT: act_insert(a); break;
        case OSD_ACT_EJECT:
            drive_eject(a.target);
            menu_say(false, "Lecteur %c vide", 'A' + a.target);
            break;
        case OSD_ACT_LOAD_ROM: act_load_rom(a); break;
        case OSD_ACT_RESTORE:
            bank_restore(a.target);
            menu_say(false, "Banque %d : contenu d'origine", a.target);
            break;
        case OSD_ACT_RESET:
            telestrat_cold_reset(&state.telestrat);  // à froid : TELEMON revoit les cartouches
            menu_close();
            return;
        case OSD_ACT_SAVE: config_save(); break;
        case OSD_ACT_PRINTER: act_printer(); break;
        case OSD_ACT_MODEM: act_modem(); break;
        case OSD_ACT_PROFILE:
            if (act_profile(a)) return;
            break;
        case OSD_ACT_STATE_SAVE: act_state_save(); break;
        case OSD_ACT_STATE_LOAD: act_state_load(a); break;
        case OSD_ACT_TAPE_TURBO:
            tape_turbo = !tape_turbo;
            osd_menu_message(&menu, false, oric_turbo_message(tape_turbo, tape_options_apply()));
            break;
        case OSD_ACT_TAPE_MOTOR:
            tape_motor_always = !tape_motor_always;
            tape_options_apply();
            osd_menu_message(&menu, false, tape_motor_always ? "Moteur toujours en marche : la cassette défile dès son insertion"
                                                             : "Moteur commandé par le relais (PB6)");
            break;
        case OSD_ACT_TAPE_INSERT: act_tape_insert(a); break;
        case OSD_ACT_TAPE_EJECT:
            tape_eject();
            osd_menu_message(&menu, false, "Cassette éjectée");
            break;
        case OSD_ACT_LOAD_BUILTIN: act_load_builtin(a); break;
#ifdef TELESTRAT_NET
        case OSD_ACT_SOURCE:
            if (a.file == OSD_VOL_NET) net_scan(a.target);
            break;
#endif
        case OSD_ACT_RESUME: menu_close(); return;
        default: break;
    }
    menu_refresh();
    menu_draw();
}

// Touche du clavier USB (codes de hid_app.c) vers le menu
static OSD_ONLY int menu_key(int code) {
    switch (code) {
        case 0x152: return OSD_KEY_UP;
        case 0x151: return OSD_KEY_DOWN;
        case 0x150: return OSD_KEY_LEFT;
        case 0x14F: return OSD_KEY_RIGHT;
        case 0x0D: return OSD_KEY_ENTER;
        case 0x1B: return OSD_KEY_ESC;
        case 0x08:
        case 0x7F: return OSD_KEY_DEL;
        case 0x14A: return OSD_KEY_HOME;
        case 0x14D: return OSD_KEY_END;
        case 0x14B: return OSD_KEY_PGUP;
        case 0x14E: return OSD_KEY_PGDN;
        default: return code < 0x100 ? code : 0;
    }
}

// À chaque trame (mode texte) : l'impression en attente est ajoutée au fichier
static void printer_flush(void) {
    uint32_t n;
    if (!byte_fifo_count(&printer_fifo)) return;
    if (!usb_scanned || !cfg_printer[0]) {
        byte_fifo_drop(&printer_fifo, byte_fifo_count(&printer_fifo));  // pas de clé ou pas d'impression
        return;
    }
    if (!printer_open) {
        // Ajout à la fin du fichier, créé s'il n'existe pas
        printer_open = neo_file_open(&printer_file, NEO_VOL_USB, cfg_printer, NEO_READ | NEO_WRITE) ||
                       neo_file_open(&printer_file, NEO_VOL_USB, cfg_printer, NEO_WRITE | NEO_CREATE);
        if (!printer_open) {
            byte_fifo_drop(&printer_fifo, byte_fifo_count(&printer_fifo));
            return;
        }
        printer_file.pos = printer_file.size;
    }
    // Une écriture (synchronisée) par morceau contigu de la file : deux au
    // plus par trame
    const uint8_t *p;
    while ((p = byte_fifo_peek(&printer_fifo, &n)), n) {
        if (!neo_file_append(&printer_file, p, n)) {
            // Clé pleine ou retirée : file jetée, fichier refermé (rouvert aux octets suivants)
            printf("Imprimante : écriture de %s impossible\n", cfg_printer);
            byte_fifo_drop(&printer_fifo, byte_fifo_count(&printer_fifo));
            neo_file_close(&printer_file);
            printer_open = false;
            return;
        }
        byte_fifo_drop(&printer_fifo, n);
    }
}

#ifdef PRINTER_RENDER
// À chaque trame (FX-80, MCP-40) : octets de la file interprétés, lignes de la
// page écrites, tant que la trame a du temps (start : début de la trame)
static void printer_render(uint32_t start) {
    if (!usb_scanned) {
        byte_fifo_drop(&printer_fifo, byte_fifo_count(&printer_fifo));  // pas de clé
        return;
    }
    const bool fx = printer_type == OSD_PRINTER_FX80;
    bool got = false;
    // Au moins un pas par trame, plus tant que la trame n'est pas finie
    for (bool first = true; first || time_us_32() - start < 18000; first = false) {
        if (fx && fx80_busy(&prn.fx)) {
            fx80_service(&prn.fx, 8);
            continue;
        }
        uint32_t n;
        const uint8_t *p = byte_fifo_peek(&printer_fifo, &n);
        if (!n) break;
        uint32_t k = 0;
        while (k < n && !(fx && fx80_busy(&prn.fx))) {
            if (fx) fx80_feed(&prn.fx, p[k++]);
            else mcp40_feed(&prn.mcp, p[k++]);
        }
        byte_fifo_drop(&printer_fifo, k);
        got = true;
    }
    if (got) printer_idle = 0;
    else if (printer_idle >= 0 && ++printer_idle >= PRINTER_IDLE_FRAMES) render_job_end();
}
#endif

static void printer_service(uint32_t start) {
#ifdef PRINTER_RENDER
    if (printer_type != OSD_PRINTER_TEXT) printer_render(start);
    else
#endif
        printer_flush();
    (void)start;
    if (!printer_busy(NULL)) telestrat_printer_resume(&state.telestrat);
}

// À chaque trame : à la première apparition de la clé, réglages, lecteurs
// (a= … d=, sinon la première image dans A) et cartouches (bank1= … bank7=) ;
// la clé l'emporte sur l'image en flash
static bool usb_first_mount = true;

// Clé présente (stockage de masse monté par TinyUSB)
static bool usb_key_present(void) {
    for (uint8_t a = 1; a <= CFG_TUH_DEVICE_MAX; a++)
        if (tuh_msc_mounted(a)) return true;
    return false;
}

// Clé ou modem retiré : lecteurs et cassette de ce volume abandonnés (ils ne
// sont plus accessibles : leur piste en cache est jetée, F-09), ceux de
// l'autre volume gardés
static void files_dropped(int vol) {
    for (int d = 0; d < 4; d++)
        if (neo_file_is_open(&drive_file[d]) && osd_is_net(drive_name[d]) == (vol == OSD_VOL_NET)) drive_drop(d);
    if (neo_file_is_open(&tape_file) && osd_is_net(tape_name) == (vol == OSD_VOL_NET)) tape_eject();
}

// Impression terminée et fichiers de sortie refermés (clé retirée, arrêt)
static void printer_close_all(void) {
    printer_service(time_us_32());  // octets encore en file
#ifdef PRINTER_RENDER
    for (int i = 0; i < 1000 && !render_job_end(); i++) fx80_service(&prn.fx, 8);
    render_file_close(NULL);
#endif
    neo_file_close(&printer_file);
    printer_open = false;
}

// Fichiers refermés avant de rendre la main au firmware Neo6502 (Pause) : piste
// en cache, cassette enregistrée, impression, lecteurs (F-01)
static void files_close_all(void) {
    for (int d = 0; d < 4; d++)
        if (neo_file_is_open(&drive_file[d])) drive_eject(d);  // piste en attente écrite d'abord
    oric_tape_rec_motor_off(&state.telestrat.tape_rec);
    neo_writer_close(&rec_w);
    tape_eject();
    printer_close_all();
}

#ifdef TELESTRAT_NET
// Fichiers du réseau retirés de la liste du menu (ceux de la clé gardés)
static void menu_drop_net_files(void) {
    int n = 0;
    for (int i = 0; i < menu.nfiles; i++)
        if (menu.files[i].vol == OSD_VOL_USB) menu.files[n++] = menu.files[i];
    menu.nfiles = n;
}
#endif

// Clé retirée : ses fichiers abandonnés, lecteurs vidés (l'image en flash
// revient dans A), cassette éjectée ; les cartouches déjà chargées restent
// (en RAM), les fichiers du réseau aussi
static void usb_unplugged(void) {
    files_dropped(OSD_VOL_USB);
    oric_tape_rec_motor_off(&state.telestrat.tape_rec);  // enregistrement interrompu
    neo_writer_close(&rec_w);
    neo_file_close(&printer_file);
    printer_open = false;
#ifdef PRINTER_RENDER
    render_file_close(NULL);  // travail en cours perdu (clé retirée)
    render_init();
#endif
    menu.nfiles = 0;
    menu.truncated = false;
    menu.usb_present = false;
    if (!drive_name[0][0]) insert_flash_disk();
    menu_files_changed();  // menu ouvert : liste vide affichée, plus de sélecteur périmé (F-16)
    printf("USB : clé retirée\n");
}

#ifdef TELESTRAT_NET
/*-- Volume Réseau : TNFS par le modem Wi-Fi Neo6502picowifi -----------------*/
// TELESTRA.CFG « reseau=hôte[:port] » : serveur TNFS (port 16384 par défaut).
// Les datagrammes passent par le second port série du modem (port TNFS,
// trames : longueur sur 2 octets puis le datagramme, neo_dgram_serial.h).
// Ce port est désactivé par défaut : activé une fois par AT$TNFSUSB=1 puis
// AT+RST (gardé dans la flash du modem), il apparaît à la nouvelle énumération
// USB ; le serveur lui est donné par AT$TNFS="hôte",port. Le dialogue AT
// passe par le port 0, celui de la ligne Minitel : seulement ligne au repos
// (ni appel, ni sonnerie, ni prise RS232), puis hayes_line est réinitialisé.
// Pas de repli sur l'UDP du port AT (choix de l'Oric pour un modem sans
// second port) : il prendrait la ligne Minitel. Une tentative par branchement
// du modem ; l'émulation attend pendant ce temps (jusqu'à ~35 s sans Wi-Fi).
static neo_esp_t net_esp;
static neo_dgram_t net_dgram;
static neo_tnfs_t net_tnfs;      // volume NEO_VOL_NET, prêt quand monté
static uint32_t net_tried;       // neo_cdc_mount_count() de la dernière tentative
static bool net_usb_enabled;     // AT$TNFSUSB=1 envoyé une fois
static bool net_first_mount = true;

// Source Réseau choisie dans le menu : ses fichiers relus à la suite de ceux
// de la clé, puis le sélecteur rouvert
static void net_scan(int item) {
    menu_drop_net_files();
    const int n = menu.nfiles;
    if (!neo_storage_list(NEO_VOL_NET, "/", usb_scan_entry, (void *)(intptr_t)OSD_VOL_NET))
        osd_menu_message(&menu, true, "Réseau : liste illisible");
    files_sort(n);
    osd_menu_browse(&menu, item, OSD_VOL_NET);
}

// Lecteurs du réseau de TELESTRA.CFG (a=net:/… ), dans les lecteurs vides
static bool net_cfg_drives(void) {
    bool any = false;
    for (int d = 0; d < 4 && net_tnfs.mounted; d++) {
        if (!osd_is_net(cfg_drive[d]) || (drive_name[d][0] && strcmp(drive_name[d], FLASH_NAME))) continue;
        if (drive_insert(d, cfg_drive[d])) any = true;
        else printf("TELESTRA.CFG : %s illisible\n", cfg_drive[d]);
    }
    return any;
}

static void net_poll(void) {
    if (net_tnfs.mounted && !neo_cdc_port_ready(1)) {
        net_tnfs.mounted = false;  // modem retiré : volume plus prêt
        files_dropped(OSD_VOL_NET);
        menu_drop_net_files();  // plus de fichier du réseau à choisir (F-16)
        osd_menu_message(&menu, true, "Réseau : modem retiré");
        menu_files_changed();
        printf("Réseau : modem retiré\n");
        return;
    }
    if (!cfg_net[0] || net_tnfs.mounted || !modem_present || neo_cdc_mount_count() == net_tried) return;
    if (osd_open || mux.rs232 || modem.state != HAYES_COMMAND || modem.ringing) return;  // ligne occupée
    net_tried = neo_cdc_mount_count();
    char host[48];
    uint16_t port;
    if (!osd_config_server(cfg_net, host, sizeof(host), &port, NEO_TNFS_PORT)) {
        printf("TELESTRA.CFG : reseau=%s invalide\n", cfg_net);
        return;
    }
    const bool relay = neo_cdc_port_ready(1);
    neo_esp_init(&net_esp, neo_cdc_write, neo_cdc_read_byte, (void *)0);
    bool ok = neo_esp_prepare(&net_esp, NULL, NULL);
    if (ok && !relay) {
        const int usb = neo_esp_tnfs_usb(&net_esp);
        if (usb == 0 && !net_usb_enabled) {
            net_usb_enabled = true;
            if (neo_esp_enable_tnfs_usb(&net_esp)) {
                printf("Réseau : port TNFS du modem activé, redémarrage du modem\n");
                return;  // nouvelle tentative à sa nouvelle énumération
            }
        }
        printf("Réseau : %s\n", usb < 0 ? "modem sans port TNFS" : "port TNFS du modem non monté");
        ok = false;
    }
    if (ok) {
        neo_dgram_init(&net_dgram, neo_cdc_write, neo_cdc_read_byte, (void *)1);
        neo_tnfs_init(&net_tnfs, neo_dgram_xfer, &net_dgram);
        ok = neo_esp_tnfs_server(&net_esp, host, port) && neo_tnfs_mount(&net_tnfs, "/", NULL, NULL);
    }
    // Ligne Minitel réinitialisée (ATE0V1, ATS0=0… après le dialogue AT)
    modem_mux_attach(&mux, modem_write, NULL, cfg_dial, cfg_listen);
    printf("Réseau : %s:%u %s\n", host, (unsigned)port, ok ? "monté" : "injoignable");
    if (!ok) return;
    snprintf(net_label, sizeof(net_label), "%s:%u", host, (unsigned)port);
    // Fichiers du réseau de TELESTRA.CFG : lecteurs ; au premier montage,
    // cartouches aussi (puis démarrage à froid, comme celles de la clé)
    bool cold = net_cfg_drives() && net_first_mount;
    for (int b = 1; b < 8 && net_first_mount; b++) {
        const char *err = "";
        if (!osd_is_net(cfg_bank[b])) continue;
        if (bank_load(b, cfg_bank[b], &err)) cold = true;
        else printf("TELESTRA.CFG : %s : %s\n", cfg_bank[b], err);
    }
    net_first_mount = false;
    tape_options_apply();
    if (cold) telestrat_cold_reset(&state.telestrat);
}
#endif

// Lecteurs de TELESTRA.CFG (a= … d=), sinon la première image dans A ; true
// si une disquette est insérée
static bool usb_mount_drives(void) {
    drive_set_t ds;
    drive_set_init(&ds);
    for (int i = 0; i < menu.nfiles; i++)
        if (menu.files[i].kind == OSD_FILE_DSK) drive_set_add(&ds, menu.files[i].name);
    const char *wanted[4] = {cfg_drive[0], cfg_drive[1], cfg_drive[2], cfg_drive[3]};
    drive_set_assign(&ds, wanted);
    // Signalé sur la console au lieu d'un échec silencieux (P-22)
    if (ds.rejected) printf("TELESTRA.CFG : %d image(s) disquette refusée(s) (nom trop long ou liste pleine)\n", ds.rejected);
    for (int d = 0; d < 4; d++)
        if ((ds.unmatched >> d & 1) && !osd_is_net(cfg_drive[d]))
            printf("TELESTRA.CFG : lecteur %c : « %s » introuvable sur la clé\n", 'a' + d, cfg_drive[d]);
    bool disks = false;
    for (int d = 0; d < 4; d++)
        if (ds.slot[d] >= 0 && !osd_is_net(cfg_drive[d])) disks |= drive_insert(d, ds.names[ds.slot[d]]);
#ifdef TELESTRAT_NET
    disks |= net_cfg_drives();  // réseau déjà monté (clé rebranchée)
#endif
    return disks;
}

// Cartouches de TELESTRA.CFG (bank1= … bank7=) ; true si l'une est en place
static bool usb_mount_banks(void) {
    bool any = false;
    for (int b = 1; b < 8; b++) {
        const char *err = "";
        if (!cfg_bank[b][0]) continue;
        if (bank_load_any(b, cfg_bank[b], &err)) any = true;
        else printf("TELESTRA.CFG : %s : %s\n", cfg_bank[b], err);
    }
    return any;
}

// Profil de démarrage (demarrage=ID ou libellé d'un profil de la clé) ; true
// si des banques ont changé (même si le profil a échoué en route : F-04)
static bool usb_mount_profile(void) {
    if (!cfg_boot[0] || !strcmp(cfg_boot, "choix")) return false;
    const char *err = "profil inconnu";
    bool touched = false, ok = false;
    const int profile = rom_profile_find(cfg_boot);
    if (profile >= 0) {
        ok = profile_run(profile, -1, &touched, &err);
    } else {
        for (int k = 0; k < user_n; k++)
            if (!strcmp(cfg_boot, user_label[k])) ok = profile_run(-1, k, &touched, &err);
    }
    if (!ok) printf("TELESTRA.CFG : demarrage=%s : %s\n", cfg_boot, err[0] ? err : "échec");
    return touched;
}

static void usb_poll(void) {
    msc_poll();
    // msc_app.c ne redescend pas msc_inquiry_complete au retrait :
    // présence suivie ici, le drapeau est remis à zéro pour le rebranchement
    if (usb_scanned && !usb_key_present()) {
        usb_scanned = false;
        msc_inquiry_complete = false;
        usb_unplugged();
        return;
    }
    if (usb_scanned || !msc_inquiry_complete) return;
    usb_scanned = true;
    read_config();
    usb_scan();
    const bool disks = usb_mount_drives();
    tape_options_apply();
    menu_files_changed();  // menu ouvert pendant ce temps : liste relue, redessin (F-16)
    // Rebranchement : lecteurs seulement (cartouches et machine inchangées)
    if (!usb_first_mount) return;
    usb_first_mount = false;
    // Premier montage (2-3 s après la mise sous tension, TELEMON a déjà
    // demandé sa disquette) : disquette insérée -> démarrage à froid dessus
    bool banks = disks;
    banks |= usb_mount_banks();
    banks |= usb_mount_profile();
    tape_options_apply();
    // Cartouches présentes dès le démarrage : TELEMON doit les inventorier
    if (banks) telestrat_cold_reset(&state.telestrat);
    if (!strcmp(cfg_boot, "choix")) {
#ifdef TELESTRAT_OSD
        menu_open();
        osd_menu_open_boot(&menu);
        menu_draw();
#else
        printf("TELESTRA.CFG : demarrage=choix ignoré (pas de menu en 800x480)\n");  // F-05
#endif
    }
}

// TELESTRA.CFG : « dial=hôte:port », « listen=port », « rs232=usb|uext »,
// « a= » … « d= », « bank1= » … « bank7= » (clé, ou réseau : « net:/NOM »),
// « reseau=hôte[:port] » (variante standard) ; une clé par ligne

// Réglages remis à leur valeur par défaut avant chaque relecture : ceux de
// la clé précédente ne restent pas (F-22)
static void config_defaults(void) {
    memset(cfg_drive, 0, sizeof(cfg_drive));
    memset(cfg_bank, 0, sizeof(cfg_bank));
    cfg_boot[0] = 0;
    user_n = 0;
    snprintf(cfg_printer, sizeof(cfg_printer), "IMPRIM.TXT");
    cfg_dial[0] = 0;
    cfg_listen = 0;
    printer_enabled = true;
    modem_enabled = true;
    tape_turbo = false;
    tape_motor_always = false;
    printer_select(OSD_PRINTER_TEXT);
#ifdef TELESTRAT_NET
    cfg_net[0] = 0;
#endif
    cfg_rs232_uext = false;
}

// Une ligne de TELESTRA.CFG (lecteurs, cartouches et profils compris)
static void config_line(const char *line) {
    const char *v;
    if ((v = osd_config_value(line, "dial"))) {
        snprintf(cfg_dial, sizeof(cfg_dial), "%.63s", v);
    } else if ((v = osd_config_value(line, "listen"))) {
        int port;
        if (osd_config_port(v, &port)) cfg_listen = port;
        else printf("TELESTRA.CFG : listen=%s invalide (1 à 65535)\n", v);
    } else if ((v = osd_config_value(line, "impression"))) {
        printer_enabled = osd_config_yes(v, true);
    } else if ((v = osd_config_value(line, "demarrage"))) {
        snprintf(cfg_boot, sizeof(cfg_boot), "%.*s", (int)sizeof(cfg_boot) - 1, v);
    } else if ((v = osd_config_value(line, "profil"))) {
        if (user_n < ROM_USER_PROFILES) rom_user_profile_label(v, user_label[user_n++], ROM_USER_LABEL);
    } else if ((v = osd_config_value(line, "cassette_rapide"))) {
        tape_turbo = osd_config_yes(v, tape_turbo);
    } else if ((v = osd_config_value(line, "cassette_moteur"))) {
        tape_motor_always = !strcmp(v, "toujours");
    } else if ((v = osd_config_value(line, "volume"))) {
        const int level = osd_config_volume(v, HID_VOLUME_MAX);
        if (level >= 0) audio_set_volume((uint8_t)level, false);
    } else if ((v = osd_config_value(line, "modem"))) {
        modem_enabled = osd_config_yes(v, true);
    } else if ((v = osd_config_value(line, "imprimante_type"))) {
        printer_select(osd_printer_type(v, printer_type));
    } else if ((v = osd_config_value(line, "imprimante"))) {
        snprintf(cfg_printer, sizeof(cfg_printer), "%.47s", v);
#ifdef TELESTRAT_NET
    } else if ((v = osd_config_value(line, "reseau"))) {
        snprintf(cfg_net, sizeof(cfg_net), "%.63s", v);
#endif
    } else if ((v = osd_config_value(line, "rs232"))) {
#ifdef TELESTRAT_RS232_UART
        cfg_rs232_uext = !strcmp(v, "uext");
#endif
    }
    for (int d = 0; d < 4; d++) {
        const char key[2] = {(char)('a' + d), 0};
        if ((v = osd_config_value(line, key))) snprintf(cfg_drive[d], sizeof(cfg_drive[d]), "%s", v);
    }
    for (int b = 1; b < 8; b++) {
        char key[8];
        snprintf(key, sizeof(key), "bank%d", b);
        if ((v = osd_config_value(line, key))) snprintf(cfg_bank[b], sizeof(cfg_bank[b]), "%s", v);
    }
}

static void read_config(void) {
    neo_file_t f;
    uint32_t pos = 0;
    const bool was_uext = cfg_rs232_uext;
    config_defaults();
    const bool found = neo_file_open(&f, NEO_VOL_USB, "TELESTRA.CFG", NEO_READ);
    if (found) {
        char line[CFG_LINE];
        bool cut;
        while (cfg_line(&f, &pos, line, &cut)) config_line(line);
        neo_file_close(&f);
    }
#ifdef TELESTRAT_RS232_UART
    if (cfg_rs232_uext) rs232_uart_init();
    else if (was_uext) rs232_uart_exit();  // la prise RS232 rend l'UART aux messages
#else
    (void)was_uext;
#endif
    printf("TELESTRA.CFG : dial=%s listen=%d rs232=%s\n", cfg_dial, cfg_listen, cfg_rs232_uext ? "uext" : "usb");
    if (modem_present) modem_mux_attach(&mux, modem_write, NULL, cfg_dial, cfg_listen);
}

/*-- Ligne de recette par sonde SWD (tools/carte.py ligne ...) ----------------*/
// Quand la sonde met diag_line_on à 1, la prise Minitel utilise cette ligne à
// la place du modem : la sonde simule l'appel (diag_line_ring), lit ce que le
// Telestrat émet (diag_tx) et écrit ce que le correspondant tape (diag_rx).
volatile uint8_t diag_line_on, diag_line_ring, diag_line_carrier;
volatile uint8_t diag_rx[256];
volatile uint32_t diag_rx_head, diag_rx_tail;
#ifndef DIAG_TX_SIZE
#define DIAG_TX_SIZE 1024  // puissance de 2 (tools/carte.py lit la taille du symbole)
#endif
volatile uint8_t diag_tx[DIAG_TX_SIZE];
volatile uint32_t diag_tx_n;  // octets émis depuis le début (diag_tx circulaire)

static minitel_line_t hayes;  // ligne du modem

static bool line_dial(void *ctx) {
    if (!diag_line_on) return modem_enabled && hayes.dial(hayes.ctx);
    diag_line_carrier = 1;
    return true;
}
static void line_answer(void *ctx) {
    if (!diag_line_on) {
        if (modem_enabled) hayes.answer(hayes.ctx);
        return;
    }
    diag_line_ring = 0;
    diag_line_carrier = 1;
}
static void line_hangup(void *ctx) {
    if (!diag_line_on) {
        hayes.hangup(hayes.ctx);
        return;
    }
    diag_line_carrier = 0;
}
static bool line_incoming(void *ctx) {
    return diag_line_on ? diag_line_ring != 0 : modem_enabled && hayes.incoming(hayes.ctx);
}
static bool line_carrier(void *ctx) {
    return diag_line_on ? diag_line_carrier != 0 : modem_enabled && hayes.carrier(hayes.ctx);
}
static int line_recv(void *ctx) {
    if (!diag_line_on) return modem_enabled ? hayes.recv(hayes.ctx) : -1;
    if (diag_rx_head == diag_rx_tail) return -1;
    return diag_rx[diag_rx_head++ & 255];
}
static void line_send(void *ctx, uint8_t data) {
    if (!diag_line_on) {
        if (modem_enabled) hayes.send(hayes.ctx, data);
        return;
    }
    diag_tx[diag_tx_n & (DIAG_TX_SIZE - 1)] = data;
    diag_tx_n++;
}

void app_init(void) {
    // Volume 0 : la clé, lecteur FatFs courant (« 0: », monté par msc_poll) ;
    // servi une fois la clé montée (usb_scanned, msc_inquiry_complete)
    neo_storage_set(NEO_VOL_USB, "Clé USB", &neo_storage_fatfs_ops, "");
#ifdef TELESTRAT_NET
    // Volume 1 : le réseau, prêt une fois le serveur TNFS monté (net_poll)
    neo_storage_set(NEO_VOL_NET, "Réseau", &neo_storage_tnfs_ops, &net_tnfs);
#endif
    modem_mux_init(&mux, &modem);
    hayes = hayes_line_line(&modem);
    minitel_line_t line = {line_dial, line_answer, line_hangup, line_incoming, line_carrier, line_recv, line_send, NULL};
    minitel_port_init(&minitel, &line);
    telestrat_desc_t desc = telestrat_desc();
    telestrat_init(&state.telestrat, &desc);
#ifdef WDC65C02_BUS_PIO
    bus_pio_start();  // broches du bus et PHI2 à la machine d'état (après wdc65C02cpu_init)
#endif
    static const oric_tape_rec_out_t rec_out = {rec_open, rec_write, rec_close, NULL};
    telestrat_tape_recorder(&state.telestrat, &rec_out);
    insert_flash_disk();
    telestrat_reset(&state.telestrat);
}

/*-- Menu à l'écran ------------------------------------------------------------*/
// Dessin dans la surface que le cœur 1 n'affiche pas, publiée une fois finie
// (voir osd_shown). Avant de dessiner dans une surface déjà publiée, le cœur 1
// doit avoir fini l'image qu'il en avait prise.
#ifdef TELESTRAT_OSD
static void menu_draw(void) {
    menu.version = TELESTRAT_VERSION;
    osd_surface_t *back = menu_back();
    if (osd_shown && !osd_blank) {  // la surface à écraser a été affichée avant la dernière publication
        const uint32_t t0 = time_us_32();
        while (core1_frames == osd_flip_mark && time_us_32() - t0 < 100000) tight_loop_contents();
    }
    osd_menu_draw(&menu, back);
    menu_publish(back);
}
#else
static void menu_draw(void) {}
#endif

/*-- Recette par sonde SWD (tools/carte.py) ----------------------------------*/
// Toujours présent (coût négligeable) : la sonde lit ces variables et remplit
// la file de touches pendant que la carte tourne.
volatile uint8_t diag_keyq[256];          // codes de touche Telestrat (ASCII, 0x146 exclu)
volatile uint32_t diag_keyq_head, diag_keyq_tail;
volatile uint32_t diag_frames;            // trames émulées depuis le démarrage
volatile uint32_t diag_frame_us_sum, diag_frame_us_max, diag_frame_n;  // travail du cœur 0 par trame
volatile uint32_t diag_line_us_sum, diag_line_us_max, diag_line_n;     // rendu d'une ligne, cœur 1
volatile uint32_t diag_late;              // lignes DVI en retard (PicoDVI) : valeur courante, redescend au rattrapage
volatile uint32_t diag_late_total;        // cumul des hausses de ce compteur (au moins les lignes rouges)
// Disposition de state pour la sonde : décalages de ram, fb, system_ticks, bank
// (used : lue par la sonde seulement)
const volatile uint32_t diag_layout[4] __attribute__((used)) = {
    offsetof(state_t, telestrat.ram), offsetof(state_t, telestrat.fb), offsetof(state_t, telestrat.system_ticks),
    offsetof(state_t, telestrat.bank)};

// Une touche de la file : appui 3 trames, relâche 3 trames. Menu : la sonde
// l'ouvre ou le ferme par diag_menu = 1 ; menu ouvert, la file lui va, avec
// 0x80-0x83 = haut, bas, gauche, droite, 0x0D Entrée, 0x1B Échap, 0x7F Suppr.
volatile uint8_t diag_menu;
/*-- Dépôt de fichiers sur la clé par la sonde (tools/carte.py deposer) --------*/
// La sonde écrit un morceau dans l'image du Telestrat (diag_up_len octets),
// puis la commande ; le firmware l'exécute à la trame suivante et remet
// diag_up_cmd à 0 (diag_up_status : DIAG_UP_OK, sinon la cause). Émulation en
// pause du début à la fin du dépôt (l'image sert de tampon : l'écran montre
// les données pendant le transfert, puis image ou menu sont redessinés).
#define DIAG_UP_CHUNK 16384
volatile uint32_t diag_up_cmd;      // 1 créer diag_up_name, 2 écrire, 3 fermer, 4 relire (16 Ko au plus)
volatile uint32_t diag_up_len;
volatile uint32_t diag_up_off;      // relecture : décalage dans le fichier
volatile int32_t diag_up_status;
volatile char diag_up_name[40];
static bool diag_up_active;   // émulation en pause (dépôt ou relecture en cours)
static bool diag_up_writing;  // fichier ouvert en écriture par la commande 1 (F-19)
// Causes d'échec (diag_up_status, lu par tools/carte.py)
#define DIAG_UP_OK        0
#define DIAG_UP_NOT_READY 1  // pas de clé
#define DIAG_UP_OPEN      2  // création ou ouverture impossible
#define DIAG_UP_IO        3  // écriture (clé pleine…) ou lecture impossible
#define DIAG_UP_CMD       4  // commande inconnue, ou sans fichier ouvert
// Fichier pris dans l'image aussi, après le tampon (pas de RAM de plus). Cette
// zone recouvre les deux surfaces du menu et leur zone de travail : menu ouvert,
// le dépôt les écrase, le menu est redessiné à la fin (commandes 3 et 5).
#define diag_up_file (*(neo_file_t *)((uintptr_t)(state.telestrat.fb + DIAG_UP_CHUNK + 7) & ~(uintptr_t)7))

_Static_assert(DIAG_UP_CHUNK + 8 + sizeof(neo_file_t) <= TELESTRAT_FRAMEBUFFER_SIZE, "tampon de dépôt hors de l'image");

// Nom de fichier déposé par la sonde, copié (octet par octet : volatile)
static void diag_up_name_get(char name[40]) {
    for (int i = 0; i < 39; i++) name[i] = diag_up_name[i];
    name[39] = 0;
}

// Menu redessiné après un dépôt ou une relecture : l'image les a écrasé
static void diag_up_redraw(void) {
    state.telestrat.screen_dirty = true;
    if (osd_open) {
        menu_refresh();
        menu_draw();
    }
}

static void diag_upload_poll(void) {
    const uint32_t cmd = diag_up_cmd;
    if (!cmd) return;
    int32_t r = DIAG_UP_OK;
    if (cmd == 1) {
        char name[40];
        diag_up_name_get(name);
        if (diag_up_writing) neo_file_close(&diag_up_file);
        diag_up_writing = usb_scanned && neo_file_open(&diag_up_file, NEO_VOL_USB, name, NEO_WRITE | NEO_CREATE);
        diag_up_active = diag_up_writing;
        r = diag_up_writing ? DIAG_UP_OK : usb_scanned ? DIAG_UP_OPEN : DIAG_UP_NOT_READY;
    } else if (cmd == 2 && diag_up_writing) {
        const uint32_t n = diag_up_len < DIAG_UP_CHUNK ? diag_up_len : DIAG_UP_CHUNK;
        if (!neo_file_append(&diag_up_file, state.telestrat.fb, n)) r = DIAG_UP_IO;  // clé pleine
    } else if (cmd == 4 && !diag_up_writing) {
        // Relecture : le début du fichier dans l'image, diag_up_len = octets lus.
        // Refusée pendant un dépôt : diag_up_file est alors le fichier ouvert (F-19)
        char name[40];
        diag_up_name_get(name);
        uint32_t n = 0;
        r = DIAG_UP_OPEN;
        if (neo_file_open(&diag_up_file, NEO_VOL_USB, name, NEO_READ)) {
            diag_up_active = true;  // émulation en pause pendant la relecture
            const uint32_t left = diag_up_off < diag_up_file.size ? diag_up_file.size - diag_up_off : 0;
            n = left < DIAG_UP_CHUNK ? left : DIAG_UP_CHUNK;
            r = !n || neo_file_read(&diag_up_file, diag_up_off, state.telestrat.fb, n) ? DIAG_UP_OK : DIAG_UP_IO;
            if (r != DIAG_UP_OK) n = 0;
            neo_file_close(&diag_up_file);
        }
        diag_up_len = n;
    } else if (cmd == 5 && !diag_up_writing) {
        diag_up_active = false;  // fin de relecture
        diag_up_redraw();        // l'image servait de tampon
    } else if (cmd == 3 && diag_up_writing) {
        r = neo_file_close(&diag_up_file) ? DIAG_UP_OK : DIAG_UP_IO;
        diag_up_writing = diag_up_active = false;
        usb_scan();  // le menu voit le nouveau fichier
        diag_up_redraw();
    } else {
        r = DIAG_UP_CMD;
    }
    diag_up_status = r;
    diag_up_cmd = 0;
}

static void diag_keys_poll(void) {
    static int held = 0, phase = 0;
#ifdef TELESTRAT_OSD
    if (diag_menu) {
        diag_menu = 0;
        if (osd_open) menu_close();
        else menu_open();
    }
    if (osd_open && phase == 0) {
        if (diag_keyq_head == diag_keyq_tail) return;
        static const int arrows[4] = {0x152, 0x151, 0x150, 0x14F};
        int code = diag_keyq[diag_keyq_head & 255];
        diag_keyq_head++;
        if (code >= 0x80 && code <= 0x83) code = arrows[code - 0x80];
        const int key = menu_key(code);
        if (key) menu_action(osd_menu_key(&menu, key));
        return;
    }
#endif
    if (phase == 0 && diag_keyq_head != diag_keyq_tail) {
        held = diag_keyq[diag_keyq_head & 255];
        telestrat_key_down(&state.telestrat, held);
        phase = 1;
    } else if (phase > 0) {
        phase++;
        if (phase == 4) telestrat_key_up(&state.telestrat, held);
        if (phase == 7) {
            diag_keyq_head++;
            phase = 0;
        }
    }
}

// Plans rouge, vert, bleu d'une ligne de sortie (1 bit par pixel) ; en .bss
// (mis à zéro dans main) : __not_in_flash() les stockait en flash (F-06)
static uint32_t planes[3][FRAME_WIDTH / 32];

struct dvi_inst dvi0;

#define HID_CODE_GUI_LEFT (HID_KEY_GUI_LEFT | 0x100)
#define TELESTRAT_KEY_FUNCT 0x146

static int host_to_telestrat(int code) {
    if (code == HID_CODE_GUI_LEFT) return TELESTRAT_KEY_FUNCT;
    if (isascii(code)) {
        // Majuscules et minuscules inversées : l'Oric démarre en majuscules
        if (isupper(code)) return tolower(code);
        if (islower(code)) return toupper(code);
    }
    return code;
}

volatile int diag_last_key[4];  // derniers codes reçus du clavier USB (diagnostic SWD)

// Touches de commande (menu, F1-F3, F11, F12) : notées dans le rappel du
// clavier, exécutées par keys_service dans la boucle principale. Le rappel
// peut venir d'un tuh_task lancé pendant l'émulation (lecture d'une piste ou
// d'une cassette en flux sur la clé ou le réseau) : un RESET, un instantané
// ou une action du menu tomberaient au milieu d'un cycle du 65C02, ou au
// milieu d'une autre action (signalé par reload). Les touches du Telestrat
// restent immédiates, comme sur la vraie machine.
#define KEY_CMD_Q 8
static int key_cmd_q[KEY_CMD_Q];
static volatile uint32_t key_cmd_head, key_cmd_tail;

static void key_command(int code);

// Pause : retour au firmware Neo6502 (multi-boot) ; exécuté par keys_service
// comme les autres commandes, après avoir refermé les fichiers (F-01)
#define KEY_RETURN_CODE (NEO_MULTIBOOT_RETURN_KEY | 0x100)

static bool key_is_command(int code) {
    return code == 0x13A || code == 0x13B || code == 0x13C || code == 0x144 || code == 0x145 || code == KEY_RETURN_CODE;
}

void kbd_raw_key_down(int code) {
    diag_last_key[3] = diag_last_key[2];
    diag_last_key[2] = diag_last_key[1];
    diag_last_key[1] = diag_last_key[0];
    diag_last_key[0] = code;
    // (derrière une commande en attente, toute touche attend aussi : l'ordre est gardé)
    if (osd_open || key_cmd_head != key_cmd_tail || key_is_command(code)) {
        if (key_cmd_tail - key_cmd_head < KEY_CMD_Q) key_cmd_q[key_cmd_tail++ % KEY_CMD_Q] = code;
        return;
    }
    telestrat_key_down(&state.telestrat, host_to_telestrat(code));
}

// Touches de commande en attente (boucle principale, hors émulation) ; une
// touche reçue pendant une action (lecture de fichier) attend la suivante
static void keys_service(void) {
    while (key_cmd_head != key_cmd_tail) key_command(key_cmd_q[key_cmd_head++ % KEY_CMD_Q]);
}

static void key_command(int code) {
    telestrat_t *sys = &state.telestrat;
    if (code < 0) {  // relâché, mis en file derrière un appui
        telestrat_key_up(sys, host_to_telestrat(-code));  // même menu ouvert : pas de touche bloquée (F-07)
        return;
    }
    if (code == KEY_RETURN_CODE) {
        files_close_all();
        neo_multiboot_return();
        return;
    }
#ifdef TELESTRAT_OSD
    if (osd_open && code != 0x13A) {
        const int key = menu_key(code);
        if (key) menu_action(osd_menu_key(&menu, key));
        return;
    }
#endif
    switch (code) {
        case 0x13A:  // F1 : menu (disquettes, cartouches)
#ifdef TELESTRAT_OSD
            if (osd_open) menu_close();
            else menu_open();
#endif
            break;
#ifdef TELESTRAT_OSD
        // Instantanés : par le menu (sa zone de travail est l'image, menu
        // ouvert). F2 : enregistrer, résultat affiché ; F3 : reprendre le
        // dernier, menu refermé si tout va bien
        case 0x13B:  // F2
            menu_open();
            menu_action((osd_action_t){.type = OSD_ACT_STATE_SAVE});
            break;
        case 0x13C: {  // F3
            menu_open();
            const int i = osd_state_latest(&menu);
            if (i < 0) {
                osd_menu_message(&menu, true, "Aucun instantané (ETATnnnn.STA) sur la clé");
                break;
            }
            menu_action((osd_action_t){.type = OSD_ACT_STATE_LOAD, .file = i});
            if (!menu.message_error) menu_close();
            break;
        }
#endif
        case 0x144:  // F11
            telestrat_nmi(sys);
            break;
        case 0x145:  // F12
            telestrat_reset(sys);
            break;
        default:
            telestrat_key_down(sys, host_to_telestrat(code));
            break;
    }
}

// À VÉRIFIER (F-08) : hid_app.c du socle (l.112, hors de ce dépôt) calcule le
// code du relâchement avec les modificateurs du rapport précédent ; Maj+A puis
// Maj relâché avant A pourrait relâcher « A » sous un autre code que celui de
// l'appui. Essai : clavier USB, tenir Maj+A, relâcher Maj, relâcher A, regarder
// diag_last_key[] par la sonde SWD ou l'écran de l'Oric (caractère répété ?).
//
// Les relâchements sont toujours transmis au clavier de l'Oric, menu ouvert
// compris : une touche tenue pendant F1 puis lâchée menu ouvert ne reste pas
// enfoncée (F-07). Relâcher une touche qui n'est pas enfoncée est sans effet.
void kbd_raw_key_up(int code) {
    if (key_cmd_head != key_cmd_tail) {  // derrière l'appui en attente (code négatif : relâché)
        if (key_cmd_tail - key_cmd_head < KEY_CMD_Q) key_cmd_q[key_cmd_tail++ % KEY_CMD_Q] = -code;
        return;
    }
    telestrat_key_up(&state.telestrat, host_to_telestrat(code));
}

// À VÉRIFIER (F-21) : ce rappel peut venir d'un tuh_task lancé pendant
// telestrat_tick (lecture de piste ou de cassette en flux sur la clé ou le
// réseau, comme pour les touches ci-dessus) ; telestrat_set_joystick modifie
// alors le VIA 2 au milieu d'un tick. Non observé. Essai : manette tenue
// pendant une lecture de piste, rejeu du joystick comparé au banc PC.
void gamepad_state_update(uint8_t index, uint8_t hat_state, uint32_t button_state) {
    if (index > 1) return;
    uint8_t j = 0;
    switch (hat_state) {
        case GAMEPAD_HAT_UP: j = TELESTRAT_JOY_UP; break;
        case GAMEPAD_HAT_UP_RIGHT: j = TELESTRAT_JOY_UP | TELESTRAT_JOY_RIGHT; break;
        case GAMEPAD_HAT_RIGHT: j = TELESTRAT_JOY_RIGHT; break;
        case GAMEPAD_HAT_DOWN_RIGHT: j = TELESTRAT_JOY_DOWN | TELESTRAT_JOY_RIGHT; break;
        case GAMEPAD_HAT_DOWN: j = TELESTRAT_JOY_DOWN; break;
        case GAMEPAD_HAT_DOWN_LEFT: j = TELESTRAT_JOY_DOWN | TELESTRAT_JOY_LEFT; break;
        case GAMEPAD_HAT_LEFT: j = TELESTRAT_JOY_LEFT; break;
        case GAMEPAD_HAT_UP_LEFT: j = TELESTRAT_JOY_UP | TELESTRAT_JOY_LEFT; break;
        default: break;
    }
    if (button_state & GAMEPAD_BUTTON_A) j |= TELESTRAT_JOY_FIRE;
    telestrat_set_joystick(&state.telestrat, index, j);
}

// Lignes affichées : PicoDVI montre chaque tampon sur deux lignes de sortie
// (DVI_VERTICAL_REPEAT = 2) ; une ligne Telestrat par tampon, centrée
#define DISPLAY_LINES (FRAME_HEIGHT / 2)

// Cœur 1 : image -> plans 1 bpp -> trois encodages TMDS 1 bpp par ligne ;
// menu ouvert : ses lignes à la place (même coût, plein écran)
// Lignes rouges : late_scanline_ctr de PicoDVI monte à chaque ligne sans tampon
// prêt et redescend quand il rattrape ; seul le cumul de ses hausses, relevé
// deux fois par ligne rendue, le montre (une hausse rattrapée entre deux
// relevés échappe : c'est un minimum)
static uint32_t late_prev;
static inline void __not_in_flash_func(late_sample)(void) {
    const uint32_t late = dvi0.late_scanline_ctr;
    if (late > late_prev) diag_late_total += late - late_prev;
    late_prev = late;
}

static inline void __not_in_flash_func(render_frame)() {
    // Menu, bandeau et noir pris une fois par image : une publication du cœur 0
    // en cours d'image ne coupe pas l'affichage en deux (F-17)
#ifdef TELESTRAT_OSD
    const osd_surface_t *menu_surf = osd_shown;
    const osd_row_t *banner = banner_shown;
    const bool blank = osd_blank;
#else
    const osd_surface_t *menu_surf = NULL;
    const osd_row_t *banner = NULL;
    const bool blank = false;
#endif
    for (int y = 0; y < DISPLAY_LINES; y++) {
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        late_sample();
        const uint32_t t0 = time_us_32();
        if (blank) {
            // Image ou surfaces en cours de réécriture par le cœur 0 : ligne noire
            volatile uint32_t *r = planes[0], *g = planes[1], *b = planes[2];
            for (int i = 0; i < FRAME_WIDTH / 32; i++) r[i] = g[i] = b[i] = 0;
        } else {
            telestrat_frame_line(y, FRAME_WIDTH, DISPLAY_LINES, state.telestrat.fb, menu_surf, banner, planes[0],
                                 planes[1], planes[2]);
        }
        // Voies TMDS : 0 bleu, 1 vert, 2 rouge
        tmds_encode_1bpp(planes[2], tmdsbuf, FRAME_WIDTH);
        tmds_encode_1bpp(planes[1], tmdsbuf + FRAME_WIDTH / DVI_SYMBOLS_PER_WORD, FRAME_WIDTH);
        tmds_encode_1bpp(planes[0], tmdsbuf + 2 * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD, FRAME_WIDTH);
        const uint32_t dt = time_us_32() - t0;
        diag_line_us_sum += dt;
        diag_line_n++;
        if (dt > diag_line_us_max) diag_line_us_max = dt;
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
        late_sample();
        diag_late = late_prev;
    }
    core1_frames++;  // image finie : le cœur 0 sait que les surfaces prises plus haut sont libres
}

void __not_in_flash_func(core1_main()) {
    audio_init(_AUDIO_PIN, 22050);

    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);

    while (1) {
        render_frame();
    }

    __builtin_unreachable();
}

// Sondes de mesure (tools/rp2040_load.py) : un cycle de lecture et un cycle
// d'écriture complets du pilote de bus, pour en compter les cycles
// used : appelées par l'outil de mesure seulement ;
// ne doit pas les élaguer (plus de faux appel dans main)
__attribute__((noinline, used, section(".time_critical.telestrat"))) void telestrat_bus_probe_read(void) {
    static wdc6502cpu_t c;
    bus_tick(&c);
    bus_set_data((uint8_t)c.addr);
}

__attribute__((noinline, used, section(".time_critical.telestrat"))) void telestrat_bus_probe_write(void) {
    static wdc6502cpu_t c;
    static volatile uint8_t sink __attribute__((unused));
    bus_tick(&c);
    sink = bus_get_data();
}

int main() {
    (void)diag_layout[0];  // lecture volatile : diag_layout, lue par la sonde, reste référencée à l'édition de liens
    neo_clock_start(&DVI_TIMING, VREG_VSEL);

    stdio_init_all();

    tusb_init();

    neo_dvi_init(&dvi0, &DVI_TIMING, &DVI_DEFAULT_SERIAL_CONFIG);

    telestrat_video_init();
    memset(planes, 0, sizeof(planes));

    // Priorité au cœur 1 et au DMA du DVI : le trafic du cœur 0 (bus du 65C02,
    // XIP) ne doit pas affamer le flux TMDS (réglage du BBC de reload-emulator)
    neo_core1_start(core1_main, NEO_BUS_PRIO_CORE1_DMA);

    app_init();

    while (1) {
        uint32_t start_time_in_micros = time_us_32();

        // Une trame PAL de l'ULA : 312 lignes x 64 cycles, par tranches de
        // 1 ms (sonnerie à 50 Hz sur CB1)
        const uint32_t num_ticks = 19968;
        // Menu ouvert : émulation en pause (le 65C02 attend, horloge arrêtée)
        for (uint32_t slice = 0; slice < 20 && !osd_open && !diag_up_active; slice++) {
            const uint32_t n = slice < 19 ? 998 : num_ticks - 19 * 998;
            for (uint32_t ticks = 0; ticks < n; ticks++) {
                telestrat_tick(&state.telestrat);
            }
            const bool to_rs232 = !cfg_rs232_uext && telestrat_serial_is_rs232(&state.telestrat);
            // Bascule vers la RS232 : SEP et séquences Minitel périmés effacés (P-03)
            if (to_rs232 && !mux.rs232) minitel_port_reset(&minitel);
            modem_mux_select(&mux, to_rs232);
            modem_poll();
            modem_mux_tick(&mux, 1000);
            telestrat_set_ring(&state.telestrat, minitel_port_tick(&minitel, 1000));
            modem_flush();
        }

        // Pistes modifiées : le socle écrit la piste en cache à la fin de chaque
        // commande d'écriture du WD1793 (WD1793_FLUSH_AT_END), et disks_flush à
        // l'ouverture du menu et avant Pause (F-02).
        if (!osd_open && !diag_up_active) {  // dépôt : l'image sert de tampon
            telestrat_screen_update(&state.telestrat);
            telestrat_kbd_update(&state.telestrat, num_ticks);
#ifdef TELESTRAT_OSD
            banner_update();
#endif
        }
        tuh_task();
        modem_watch();
        modem_flush();
        usb_poll();
#ifdef TELESTRAT_NET
        net_poll();
#endif
        printer_service(start_time_in_micros);
        diag_keys_poll();
        keys_service();
        diag_upload_poll();

        uint32_t execution_time = time_us_32() - start_time_in_micros;
        diag_frames++;
        diag_frame_us_sum += execution_time;
        diag_frame_n++;
        if (execution_time > diag_frame_us_max) diag_frame_us_max = execution_time;
        int sleep_time = (int)num_ticks - (int)execution_time;
        if (sleep_time > 0) {
            sleep_us(sleep_time);
        }
    }

    __builtin_unreachable();
}
