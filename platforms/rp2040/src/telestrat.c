// telestrat.c — Oric Telestrat pour Olimex Neo6502 (RP2040 + vrai W65C02S)
//
// Le 65C02 du Neo6502 exécute TELEMON ; le RP2040 sert la mémoire (banques),
// émule VIA 1 et 2, AY-3-8912, Microdisc intégré, ACIA et vidéo ULA (DVI).
// Dérivé de platforms/rp2040/systems/oric/src/oric.c de reload-emulator.
//
// Clé USB (FAT/exFAT) : images .dsk (MFM_DISK) dans les lecteurs A à D, lues
// et écrites piste par piste (wd1793_insert_streamed), images .rom en banque
// (cartouches, en RAM) ; menu à l'écran (F1, src/osd) et TELESTRA.CFG.
//
// Télématique : un PicoWiFiModemUSB (modem Hayes en USB CDC) sert de ligne au
// Minitel émulé sur la prise de l'ACIA (devices/minitel_port.h) : appels
// entrants (RING -> sonnerie sur CB1 du VIA 2, TELEMATIC en serveur) et
// sortants (ATD, émulation Minitel). Réglages facultatifs dans TELESTRA.CFG à
// la racine de la clé : « dial=hôte:port » et « listen=port ».
//
// Touches : F1 = menu (disquettes, cartouches), F11 = NMI, F12 = RESET,
// Windows gauche = FUNCT, Pause = retour au firmware Neo6502 (multi-boot).
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

#define RGBA8(r, g, b) (0xFF000000 | (r << 16) | (g << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

#include <pico/platform.h>
#include "pico/stdlib.h"

#include "roms/telestrat_roms.h"

// Code exécuté à chaque cycle en RAM : depuis la flash (XIP), il déborderait
// le cache de 16 Ko (même choix que le BBC de reload-emulator)
#define TELESTRAT_HOT __attribute__((section(".time_critical.telestrat")))
#define CHIPS_HOT     __attribute__((section(".time_critical.telestrat")))
// Journal des premiers accès en $03xx (tools/carte.py journal)
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
#include "chips/chips_common.h"
#include "neo6502_bus.h"  // bus du vrai 65C02 intégré au tick (Olimex Neo6502)
#include "chips/mos6522via.h"
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
// Police du menu en RAM : lue par le cœur 1 à chaque ligne affichée
#define OSD_FONT_SECTION __attribute__((section(".time_critical.osd_font")))
#include "osd/osd_menu.h"
#include "osd/osd_config.h"
#include "systems/telestrat.h"
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
#include "dvi_serialiser.h"

#include "audio.h"

#include "tusb.h"
#ifdef TELESTRAT_FLASH_DISK_H
#include "telestrat_flash_disk.h"
#endif
#include "neo_multiboot.h"
#include "ff.h"

typedef struct {
    telestrat_t telestrat;
} state_t;

state_t __not_in_flash() state;

/*-- Télématique : Minitel sur l'ACIA, ligne sur le modem USB ---------------*/

static minitel_port_t minitel;
static hayes_line_t modem;
static modem_mux_t mux;  // le modem suit PA4 : prise Minitel (Hayes géré ici) ou RS232 (octets bruts)
static int modem_idx = -1;  // interface CDC du modem (-1 : absent)
static char cfg_dial[64] = "";
static int cfg_listen = 0;
static bool cfg_rs232_uext = false;  // TELESTRA.CFG rs232=uext : prise RS232 sur l'UART de l'UEXT

static void modem_write(void *ctx, const uint8_t *data, uint32_t len) {
    (void)ctx;
    if (modem_idx < 0) return;
    tuh_cdc_write((uint8_t)modem_idx, data, len);
    tuh_cdc_write_flush((uint8_t)modem_idx);
}

void tuh_cdc_mount_cb(uint8_t idx) {
    modem_idx = idx;
    modem_mux_attach(&mux, modem_write, NULL, cfg_dial, cfg_listen);
    printf("Modem USB CDC %u branché\n", idx);
}

void tuh_cdc_umount_cb(uint8_t idx) {
    if ((int)idx == modem_idx) {
        modem_idx = -1;
        modem_mux_detach(&mux);
    }
}

static void modem_poll(void) {
    if (modem_idx < 0) return;
    uint8_t buf[64];
    uint32_t n;
    while ((n = tuh_cdc_read((uint8_t)modem_idx, buf, sizeof(buf))) > 0) {
        for (uint32_t i = 0; i < n; i++) modem_mux_feed(&mux, buf[i]);
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
    modem_mux_rs232_send(&mux, data);
}

static int rs232_rx(void *user_data) {
    (void)user_data;
#ifdef TELESTRAT_RS232_UART
    if (cfg_rs232_uext) {
        rs232_uart_config();
        return uart_is_readable(RS232_UART) ? (uint8_t)uart_getc(RS232_UART) : -1;
    }
#endif
    return modem_mux_rs232_recv(&mux);
}

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
        .audio = {.callback = {.func = audio_callback}, .sample_rate = 22050},
        .minitel = {.tx = minitel_tx, .rx = minitel_rx},
        .rs232 = {.tx = rs232_tx, .rx = rs232_rx},
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
// Cassette (.tap de la clé) et son bandeau, incrusté sous l'image pendant que
// le moteur tourne (dessiné par le cœur 0, affiché par le cœur 1)
static FIL tape_fil;
static bool tape_open = false;
static char tape_name[OSD_NAME_LEN];
static osd_row_t banner_row;
static volatile bool banner_on = false;
static bool usb_scanned = false;
static FIL drive_fil[4];
static bool drive_open[4];
static char drive_name[4][OSD_NAME_LEN];  // "" : vide ; image en flash : nom réservé
static const char FLASH_NAME[] = "(image en flash)";
static char cfg_drive[4][OSD_NAME_LEN];   // TELESTRA.CFG : a= … d=
static char cfg_bank[8][OSD_NAME_LEN];    // TELESTRA.CFG : bank1= … bank7=

extern bool msc_inquiry_complete;

static bool has_ext(const char *name, const char *ext) {
    size_t n = strlen(name), e = strlen(ext);
    if (n < e) return false;
    for (size_t i = 0; i < e; i++) {
        char c = name[n - e + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != ext[i]) return false;
    }
    return true;
}

static bool usb_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len) {
    FIL *f = ctx;
    UINT n = 0;
    if (f_lseek(f, offset) != FR_OK || f_read(f, buf, len, &n) != FR_OK) return false;
    return n == len;
}

static bool usb_write(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len) {
    FIL *f = ctx;
    UINT n = 0;
    if (f_lseek(f, offset) != FR_OK || f_write(f, buf, len, &n) != FR_OK || n != len) return false;
    return f_sync(f) == FR_OK;
}

// Fichiers .dsk et .rom de la racine (noms longs jusqu'à OSD_NAME_LEN - 1)
static void usb_scan(void) {
    DIR dir;
    FILINFO fno;
    menu.nfiles = 0;
    menu.usb_present = msc_inquiry_complete && f_opendir(&dir, "/") == FR_OK;
    if (!menu.usb_present) return;
    while (menu.nfiles < OSD_MENU_FILES && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & AM_DIR) continue;
        const bool dsk = has_ext(fno.fname, ".dsk"), rom = has_ext(fno.fname, ".rom"), tap = has_ext(fno.fname, ".tap");
        if ((!dsk && !rom && !tap) || strlen(fno.fname) >= OSD_NAME_LEN) continue;
        osd_file_t *f = &menu.files[menu.nfiles++];
        snprintf(f->name, sizeof(f->name), "%s", fno.fname);
        f->size = (uint32_t)fno.fsize;
        f->kind = dsk ? OSD_FILE_DSK : tap ? OSD_FILE_TAP : OSD_FILE_ROM;
    }
    f_closedir(&dir);
    // Tri par nom (insertion : 64 fichiers au plus)
    for (int i = 1; i < menu.nfiles; i++) {
        osd_file_t t = menu.files[i];
        int j = i - 1;
        while (j >= 0 && strcmp(menu.files[j].name, t.name) > 0) {
            menu.files[j + 1] = menu.files[j];
            j--;
        }
        menu.files[j + 1] = t;
    }
    snprintf(menu.usb_label, sizeof(menu.usb_label), "Clé montée");
}

static void drive_eject(int d) {
    wd1793_eject(&state.telestrat.fdc.wd, d);  // réécrit la piste en attente
    if (drive_open[d]) f_close(&drive_fil[d]);
    drive_open[d] = false;
    drive_name[d][0] = 0;
}

// Image de la clé dans un lecteur ; false si illisible, invalide ou déjà ailleurs
static bool drive_insert(int d, const char *name) {
    for (int o = 0; o < 4; o++)
        if (o != d && !strcmp(drive_name[o], name)) return false;
    drive_eject(d);
    FIL *f = &drive_fil[d];
    const bool rw = f_open(f, name, FA_READ | FA_WRITE) == FR_OK;
    if (!rw && f_open(f, name, FA_READ) != FR_OK) return false;
    drive_open[d] = true;
    if (!wd1793_insert_streamed(&state.telestrat.fdc.wd, d, f_size(f), usb_read, rw ? usb_write : NULL, f)) {
        f_close(f);
        drive_open[d] = false;
        return false;
    }
    snprintf(drive_name[d], sizeof(drive_name[d]), "%s", name);
    printf("Lecteur %c : %s%s\n", 'A' + d, name, rw ? "" : " (protégée)");
    return true;
}

// Image intégrée à la flash (lecture seule), dans le lecteur A
static void insert_flash_disk(void) {
#ifdef TELESTRAT_FLASH_DISK_H
    if (wd1793_insert(&state.telestrat.fdc.wd, 0, (uint8_t *)telestrat_flash_disk, sizeof(telestrat_flash_disk), true)) {
        snprintf(drive_name[0], sizeof(drive_name[0]), "%s", FLASH_NAME);
        printf("Lecteur A : image en flash (%u octets, protégée)\n", (unsigned)sizeof(telestrat_flash_disk));
    }
#endif
}

static void bank_restore(int bank) { rom_pool_restore(&pool, &state.telestrat, bank); }

// Cartouche de la clé en banque ; *err : raison d'un refus
static bool bank_load(int bank, const char *name, const char **err) {
    FIL f;
    if (f_open(&f, name, FA_READ) != FR_OK) {
        *err = "fichier illisible";
        return false;
    }
    const size_t size = f_size(&f);
    uint8_t *dst = rom_pool_claim(&pool, &state.telestrat, bank, size, err);
    UINT n = 0;
    bool ok = dst != NULL;
    if (ok && (f_read(&f, dst, (UINT)size, &n) != FR_OK || n != size)) {
        *err = "lecture impossible";
        rom_pool_abort(&pool, &state.telestrat, bank);
        ok = false;
    }
    if (ok) {
        rom_pool_commit(&pool, &state.telestrat, bank, size, name);
        printf("Banque %d : %s\n", bank, name);
    }
    f_close(&f);
    return ok;
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

static bool tape_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len) {
    (void)ctx;
    UINT n = 0;
    if (!tape_open || f_lseek(&tape_fil, offset) != FR_OK || f_read(&tape_fil, buf, len, &n) != FR_OK) return false;
    return n == len;
}

static void tape_eject(void) {
    telestrat_tape_insert(&state.telestrat, 0, NULL, NULL);
    if (tape_open) f_close(&tape_fil);
    tape_open = false;
    tape_name[0] = 0;
}

// Cassette de la clé (la même : rembobinée)
static bool tape_insert(const char *name) {
    tape_eject();
    if (f_open(&tape_fil, name, FA_READ) != FR_OK) return false;
    tape_open = true;
    snprintf(tape_name, sizeof(tape_name), "%s", name);
    telestrat_tape_insert(&state.telestrat, (uint32_t)f_size(&tape_fil), tape_read, NULL);
    printf("Cassette : %s\n", name);
    return true;
}

// Bandeau : à chaque trame, tant que le moteur tourne
static void banner_update(void) {
    const oric_tape_t *t = &state.telestrat.tape;
    if (!oric_tape_running(t)) {
        banner_on = false;
        return;
    }
    osd_tape_banner(&banner_row, tape_name, oric_tape_percent(t));
    banner_on = true;
}

static void menu_refresh(void) {
    const telestrat_t *sys = &state.telestrat;
    snprintf(menu.tape, sizeof(menu.tape), "%.47s", sys->tape.inserted ? tape_name : "");
    menu.tape_percent = oric_tape_percent(&sys->tape);
    menu.tape_motor = sys->tape.inserted && sys->tape.motor;
    for (int k = 0; k < ROM_BUILTINS && k < OSD_BUILTINS; k++) menu.builtin[k] = rom_builtins[k].label;
    for (int d = 0; d < 4; d++) {
        snprintf(menu.drive[d], sizeof(menu.drive[d]), "%.47s", drive_name[d]);
        menu.drive_ro[d] = drive_name[d][0] && sys->fdc.wd.disk[d].write_protect;
    }
    for (int b = 0; b < 8; b++) {
        if (pool.name[b][0]) {
            snprintf(menu.bank[b], sizeof(menu.bank[b]), "%.47s", rom_builtin_label(pool.name[b]));
            menu.bank_kind[b] = pool.name[b][0] == '@' ? OSD_BANK_ROM : OSD_BANK_ROM_USB;
        } else {
            snprintf(menu.bank[b], sizeof(menu.bank[b]), "%s", bank_label(b));
            menu.bank_kind[b] = sys->bank_type[b] == TELESTRAT_BANK_RAM   ? OSD_BANK_RAM
                                : sys->bank_type[b] == TELESTRAT_BANK_ROM ? OSD_BANK_ROM
                                                                          : OSD_BANK_EMPTY;
        }
    }
}

// Appelée menu ouvert : ses tampons (2 x 2 Ko) sont pris dans l'image du
// Telestrat, après la surface du menu (l'image est redessinée à la fermeture)
_Static_assert(sizeof(osd_surface_t) + 4096 <= sizeof(state.telestrat.fb), "image trop petite pour le menu");
static void config_save(void) {
    char *old = (char *)state.telestrat.fb + sizeof(osd_surface_t);
    char *out = old + 2048;
    FIL f;
    UINT n = 0;
    old[0] = 0;
    if (f_open(&f, "TELESTRA.CFG", FA_READ) == FR_OK) {
        f_read(&f, old, 2048 - 1, &n);
        old[n] = 0;
        f_close(&f);
    }
    const char *drives[4], *banks[8];
    for (int d = 0; d < 4; d++) drives[d] = strcmp(drive_name[d], FLASH_NAME) ? drive_name[d] : NULL;
    for (int b = 0; b < 8; b++) banks[b] = pool.name[b];
    const size_t len = osd_config_merge(old, drives, banks, out, 2048);
    bool ok = len > 0 && f_open(&f, "TELESTRA.CFG", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK;
    if (ok) {
        ok = f_write(&f, out, (UINT)len, &n) == FR_OK && n == len;
        f_close(&f);
    }
    osd_menu_message(&menu, !ok, ok ? "Configuration enregistrée dans TELESTRA.CFG" : "TELESTRA.CFG : écriture impossible");
}

static void menu_draw(void);

static void menu_open(void) {
    usb_scan();
    menu_refresh();
    menu.page = OSD_PAGE_MAIN;
    menu.cursor = OSD_ITEM_RESUME;
    menu.message[0] = 0;
    osd_open = true;
    menu_draw();
}

static void menu_close(void) {
    osd_open = false;
    state.telestrat.screen_dirty = true;  // l'image a servi de surface au menu
}

static void menu_action(osd_action_t a) {
    char msg[96];
    const char *err = "";
    switch (a.type) {
        case OSD_ACT_INSERT:
            if (drive_insert(a.target, menu.files[a.file].name)) {
                snprintf(msg, sizeof(msg), "Lecteur %c : %s", 'A' + a.target, menu.files[a.file].name);
                osd_menu_message(&menu, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : image refusée (MFM_DISK, déjà en place ?)", menu.files[a.file].name);
                osd_menu_message(&menu, true, msg);
            }
            break;
        case OSD_ACT_EJECT:
            drive_eject(a.target);
            snprintf(msg, sizeof(msg), "Lecteur %c vide", 'A' + a.target);
            osd_menu_message(&menu, false, msg);
            break;
        case OSD_ACT_LOAD_ROM:
            if (bank_load(a.target, menu.files[a.file].name, &err)) {
                snprintf(msg, sizeof(msg), "Banque %d : %s — RESET conseillé", a.target, menu.files[a.file].name);
                osd_menu_message(&menu, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : %s", menu.files[a.file].name, err);
                osd_menu_message(&menu, true, msg);
            }
            break;
        case OSD_ACT_RESTORE:
            bank_restore(a.target);
            snprintf(msg, sizeof(msg), "Banque %d : contenu d'origine", a.target);
            osd_menu_message(&menu, false, msg);
            break;
        case OSD_ACT_RESET:
            telestrat_cold_reset(&state.telestrat);  // à froid : TELEMON revoit les cartouches
            menu_close();
            return;
        case OSD_ACT_SAVE: config_save(); break;
        case OSD_ACT_TAPE_INSERT:
            if (tape_insert(menu.files[a.file].name)) {
                snprintf(msg, sizeof(msg), "Cassette : %s (au début)", menu.files[a.file].name);
                osd_menu_message(&menu, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "%s : illisible", menu.files[a.file].name);
                osd_menu_message(&menu, true, msg);
            }
            break;
        case OSD_ACT_TAPE_EJECT:
            tape_eject();
            osd_menu_message(&menu, false, "Cassette éjectée");
            break;
        case OSD_ACT_LOAD_BUILTIN:
            if (a.file < ROM_BUILTINS &&
                rom_pool_load_builtin(&pool, &state.telestrat, a.target, &rom_builtins[a.file], &err)) {
                snprintf(msg, sizeof(msg), "Banque %d : %s — RESET conseillé", a.target, rom_builtins[a.file].label);
                osd_menu_message(&menu, false, msg);
            } else {
                snprintf(msg, sizeof(msg), "Banque %d : %s", a.target, err);
                osd_menu_message(&menu, true, msg);
            }
            break;
        case OSD_ACT_RESUME: menu_close(); return;
        default: break;
    }
    menu_refresh();
    menu_draw();
}

// Touche du clavier USB (codes de hid_app.c) vers le menu
static int menu_key(int code) {
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

// À chaque trame : à la première apparition de la clé, réglages, lecteurs
// (a= … d=, sinon la première image dans A) et cartouches (bank1= … bank7=) ;
// la clé l'emporte sur l'image en flash
static void usb_poll(void) {
    if (usb_scanned || !msc_inquiry_complete) return;
    usb_scanned = true;
    read_config();
    usb_scan();
    drive_set_t ds;
    drive_set_init(&ds);
    for (int i = 0; i < menu.nfiles; i++)
        if (menu.files[i].kind == OSD_FILE_DSK) drive_set_add(&ds, menu.files[i].name);
    const char *wanted[4] = {cfg_drive[0], cfg_drive[1], cfg_drive[2], cfg_drive[3]};
    drive_set_assign(&ds, wanted);
    for (int d = 0; d < 4; d++)
        if (ds.slot[d] >= 0) drive_insert(d, ds.names[ds.slot[d]]);
    bool banks = false;
    for (int b = 1; b < 8; b++) {
        const char *err = "";
        if (!cfg_bank[b][0]) continue;
        const rom_builtin_t *rb = cfg_bank[b][0] == '@' ? rom_builtin_find(cfg_bank[b]) : NULL;
        if (cfg_bank[b][0] == '@' && !rb) err = "ROM intégrée inconnue";
        if (rb ? rom_pool_load_builtin(&pool, &state.telestrat, b, rb, &err) : bank_load(b, cfg_bank[b], &err)) banks = true;
        else printf("TELESTRA.CFG : %s : %s\n", cfg_bank[b], err);
    }
    // Cartouches présentes dès le démarrage : TELEMON doit les inventorier
    if (banks) telestrat_cold_reset(&state.telestrat);
}

// TELESTRA.CFG : « dial=hôte:port », « listen=port », « rs232=usb|uext »,
// « a= » … « d= », « bank1= » … « bank7= » (une clé par ligne)
static void read_config(void) {
    FIL f;
    if (f_open(&f, "TELESTRA.CFG", FA_READ) != FR_OK) return;
    char line[96];
    while (f_gets(line, sizeof(line), &f)) {
        char *e = line + strlen(line);
        while (e > line && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
        const char *v;
        if (!strncmp(line, "dial=", 5)) {
            snprintf(cfg_dial, sizeof(cfg_dial), "%.63s", line + 5);
        } else if (!strncmp(line, "listen=", 7)) {
            cfg_listen = atoi(line + 7);
        } else if (!strncmp(line, "rs232=", 6)) {
#ifdef TELESTRAT_RS232_UART
            cfg_rs232_uext = !strcmp(line + 6, "uext");
            if (cfg_rs232_uext) rs232_uart_init();
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
    f_close(&f);
    printf("TELESTRA.CFG : dial=%s listen=%d rs232=%s\n", cfg_dial, cfg_listen, cfg_rs232_uext ? "uext" : "usb");
    if (modem_idx >= 0) modem_mux_attach(&mux, modem_write, NULL, cfg_dial, cfg_listen);
}

/*-- Ligne de recette par sonde SWD (tools/carte.py ligne ...) ----------------*/
// Quand la sonde met diag_line_on à 1, la prise Minitel utilise cette ligne à
// la place du modem : la sonde simule l'appel (diag_line_ring), lit ce que le
// Telestrat émet (diag_tx) et écrit ce que le correspondant tape (diag_rx).
volatile uint8_t diag_line_on, diag_line_ring, diag_line_carrier;
volatile uint8_t diag_rx[256];
volatile uint32_t diag_rx_head, diag_rx_tail;
volatile uint8_t diag_tx[4096];
volatile uint32_t diag_tx_n;  // octets émis depuis le début (diag_tx circulaire)

static minitel_line_t hayes;  // ligne du modem

static bool line_dial(void *ctx) {
    if (!diag_line_on) return hayes.dial(hayes.ctx);
    diag_line_carrier = 1;
    return true;
}
static void line_answer(void *ctx) {
    if (!diag_line_on) {
        hayes.answer(hayes.ctx);
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
static bool line_incoming(void *ctx) { return diag_line_on ? diag_line_ring != 0 : hayes.incoming(hayes.ctx); }
static bool line_carrier(void *ctx) { return diag_line_on ? diag_line_carrier != 0 : hayes.carrier(hayes.ctx); }
static int line_recv(void *ctx) {
    if (!diag_line_on) return hayes.recv(hayes.ctx);
    if (diag_rx_head == diag_rx_tail) return -1;
    return diag_rx[diag_rx_head++ & 255];
}
static void line_send(void *ctx, uint8_t data) {
    if (!diag_line_on) {
        hayes.send(hayes.ctx, data);
        return;
    }
    diag_tx[diag_tx_n & 4095] = data;
    diag_tx_n++;
}

void app_init(void) {
    modem_mux_init(&mux, &modem);
    hayes = hayes_line_line(&modem);
    minitel_line_t line = {line_dial, line_answer, line_hangup, line_incoming, line_carrier, line_recv, line_send, NULL};
    minitel_port_init(&minitel, &line);
    telestrat_desc_t desc = telestrat_desc();
    telestrat_init(&state.telestrat, &desc);
    insert_flash_disk();
    telestrat_reset(&state.telestrat);
}

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

/*-- Menu à l'écran ------------------------------------------------------------*/
// Pleine résolution de sortie (960 x 272 lignes de tampon) : pas en 800x480.
// Surface : l'image du Telestrat, inutile tant que l'émulation est en pause.
#if FRAME_WIDTH >= OSD_WIDTH && FRAME_HEIGHT / 2 >= OSD_LINES
#define TELESTRAT_OSD 1
#define OSD_SURFACE ((osd_surface_t *)state.telestrat.fb)
_Static_assert(sizeof(osd_surface_t) <= sizeof(state.telestrat.fb), "surface du menu plus grande que l'image");
static void menu_draw(void) {
    menu.version = TELESTRAT_VERSION;
    osd_menu_draw(&menu, OSD_SURFACE);
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
volatile uint32_t diag_late;              // lignes DVI en retard (PicoDVI)
// Disposition de state pour la sonde : décalages de ram, fb, system_ticks, bank
const volatile uint32_t diag_layout[4] = {offsetof(state_t, telestrat.ram), offsetof(state_t, telestrat.fb),
                                 offsetof(state_t, telestrat.system_ticks), offsetof(state_t, telestrat.bank)};

// Une touche de la file : appui 3 trames, relâche 3 trames. Menu : la sonde
// l'ouvre ou le ferme par diag_menu = 1 ; menu ouvert, la file lui va, avec
// 0x80-0x83 = haut, bas, gauche, droite, 0x0D Entrée, 0x1B Échap, 0x7F Suppr.
volatile uint8_t diag_menu;
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

// Plans rouge, vert, bleu d'une ligne de sortie (1 bit par pixel)
static uint32_t __not_in_flash() planes[3][FRAME_WIDTH / 32];

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

void kbd_raw_key_down(int code) {
    if (code == (NEO_MULTIBOOT_RETURN_KEY | 0x100)) neo_multiboot_return();
    telestrat_t *sys = &state.telestrat;
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

void kbd_raw_key_up(int code) {
    if (osd_open) return;
    telestrat_key_up(&state.telestrat, host_to_telestrat(code));
}

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
static inline void __not_in_flash_func(render_frame)() {
    for (int y = 0; y < DISPLAY_LINES; y++) {
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        const uint32_t t0 = time_us_32();
#ifdef TELESTRAT_OSD
        const osd_surface_t *menu_surf = osd_open ? OSD_SURFACE : NULL;
        const osd_row_t *banner = banner_on ? &banner_row : NULL;
#else
        const osd_surface_t *menu_surf = NULL;
        const osd_row_t *banner = NULL;
#endif
        telestrat_frame_line(y, FRAME_WIDTH, DISPLAY_LINES, state.telestrat.fb, menu_surf, banner, planes[0], planes[1],
                             planes[2]);
        // Voies TMDS : 0 bleu, 1 vert, 2 rouge
        tmds_encode_1bpp(planes[2], tmdsbuf, FRAME_WIDTH);
        tmds_encode_1bpp(planes[1], tmdsbuf + FRAME_WIDTH / DVI_SYMBOLS_PER_WORD, FRAME_WIDTH);
        tmds_encode_1bpp(planes[0], tmdsbuf + 2 * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD, FRAME_WIDTH);
        const uint32_t dt = time_us_32() - t0;
        diag_line_us_sum += dt;
        diag_line_n++;
        if (dt > diag_line_us_max) diag_line_us_max = dt;
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
        diag_late = dvi0.late_scanline_ctr;
    }
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
__attribute__((noinline, section(".time_critical.telestrat"))) void telestrat_bus_probe_read(void) {
    static neo6502bus_t c;
    neo6502bus_tick(&c);
    neo6502bus_set_data(&c, (uint8_t)c.addr);
}

__attribute__((noinline, section(".time_critical.telestrat"))) void telestrat_bus_probe_write(void) {
    static neo6502bus_t c;
    static volatile uint8_t sink __attribute__((unused));
    neo6502bus_tick(&c);
    sink = neo6502bus_get_data();
}

int main() {
    // Jamais vrai : garde les sondes à l'édition de liens
    if (time_us_32() == 0xFFFFFFFFu) {
        telestrat_bus_probe_read();
        telestrat_bus_probe_write();
        printf("%lu\n", (unsigned long)diag_layout[0]);  // garde diag_layout pour la sonde
    }

    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

    stdio_init_all();

    tusb_init();

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    telestrat_video_init();
    memset(planes, 0, sizeof(planes));

    // Priorité au cœur 1 et au DMA du DVI : le trafic du cœur 0 (bus du 65C02,
    // XIP) ne doit pas affamer le flux TMDS (réglage du BBC de reload-emulator)
    hw_set_bits(&bus_ctrl_hw->priority,
                BUSCTRL_BUS_PRIORITY_PROC1_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS);
    multicore_launch_core1(core1_main);

    app_init();

    while (1) {
        uint32_t start_time_in_micros = time_us_32();

        // Une trame PAL de l'ULA : 312 lignes x 64 cycles, par tranches de
        // 1 ms (sonnerie à 50 Hz sur CB1)
        const uint32_t num_ticks = 19968;
        // Menu ouvert : émulation en pause (le 65C02 attend, horloge arrêtée)
        for (uint32_t slice = 0; slice < 20 && !osd_open; slice++) {
            const uint32_t n = slice < 19 ? 998 : num_ticks - 19 * 998;
            for (uint32_t ticks = 0; ticks < n; ticks++) {
                telestrat_tick(&state.telestrat);
            }
            modem_mux_select(&mux, !cfg_rs232_uext && telestrat_serial_is_rs232(&state.telestrat));
            modem_poll();
            modem_mux_tick(&mux, 1000);
            telestrat_set_ring(&state.telestrat, minitel_port_tick(&minitel, 1000));
        }

        if (!osd_open) {
            telestrat_screen_update(&state.telestrat);
            telestrat_kbd_update(&state.telestrat, num_ticks);
#ifdef TELESTRAT_OSD
            banner_update();
#endif
        }
        tuh_task();
        usb_poll();
        diag_keys_poll();

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
