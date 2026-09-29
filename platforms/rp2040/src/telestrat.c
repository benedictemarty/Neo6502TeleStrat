// telestrat.c — Oric Telestrat pour Olimex Neo6502 (RP2040 + vrai W65C02S)
//
// Le 65C02 du Neo6502 exécute TELEMON ; le RP2040 sert la mémoire (banques),
// émule VIA 1 et 2, AY-3-8912, Microdisc intégré, ACIA et vidéo ULA (DVI).
// Dérivé de platforms/rp2040/systems/oric/src/oric.c de reload-emulator.
//
// Disquettes : fichiers .dsk (MFM_DISK) à la racine d'une clé USB (FAT),
// lus et écrits piste par piste (wd1793_insert_streamed) ; la première image
// est insérée dans le lecteur A dès que la clé est montée.
//
// Télématique : un PicoWiFiModemUSB (modem Hayes en USB CDC) sert de ligne au
// Minitel émulé sur la prise de l'ACIA (devices/minitel_port.h) : appels
// entrants (RING -> sonnerie sur CB1 du VIA 2, TELEMATIC en serveur) et
// sortants (ATD, émulation Minitel). Réglages facultatifs dans TELESTRA.CFG à
// la racine de la clé : « dial=hôte:port » et « listen=port ».
//
// Touches : F1 = image suivante dans le lecteur A, F11 = NMI, F12 = RESET,
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

#include <pico/platform.h>
#include "pico/stdlib.h"

#include "roms/telestrat_roms.h"

// Code exécuté à chaque cycle en RAM : depuis la flash (XIP), il déborderait
// le cache de 16 Ko (même choix que le BBC de reload-emulator)
#define TELESTRAT_HOT __attribute__((section(".time_critical.telestrat")))
#define CHIPS_HOT     __attribute__((section(".time_critical.telestrat")))
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
#include "systems/telestrat.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/vreg.h"
#include "hardware/interp.h"
#include "pico/multicore.h"

#include "tmds_encode.h"

#include "common_dvi_pin_configs.h"
#include "dvi.h"
#include "dvi_serialiser.h"

#include "audio.h"

#include "tusb.h"
#include "neo_multiboot.h"
#include "ff.h"

typedef struct {
    telestrat_t telestrat;
} state_t;

state_t __not_in_flash() state;

/*-- Télématique : Minitel sur l'ACIA, ligne sur le modem USB ---------------*/

static minitel_port_t minitel;
static hayes_line_t modem;
static int modem_idx = -1;  // interface CDC du modem (-1 : absent)
static char cfg_dial[64] = "";
static int cfg_listen = 0;

static void modem_write(void *ctx, const uint8_t *data, uint32_t len) {
    (void)ctx;
    if (modem_idx < 0) return;
    tuh_cdc_write((uint8_t)modem_idx, data, len);
    tuh_cdc_write_flush((uint8_t)modem_idx);
}

void tuh_cdc_mount_cb(uint8_t idx) {
    modem_idx = idx;
    hayes_line_init(&modem, modem_write, NULL, cfg_dial, cfg_listen);
    printf("Modem USB CDC %u branché\n", idx);
}

void tuh_cdc_umount_cb(uint8_t idx) {
    if ((int)idx == modem_idx) modem_idx = -1;
}

static void modem_poll(void) {
    if (modem_idx < 0) return;
    uint8_t buf[64];
    uint32_t n;
    while ((n = tuh_cdc_read((uint8_t)modem_idx, buf, sizeof(buf))) > 0) {
        for (uint32_t i = 0; i < n; i++) hayes_line_feed(&modem, buf[i]);
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

static void audio_callback(const uint8_t sample, void *user_data) {
    (void)user_data;
    audio_push_sample(sample);
}

// Banques : notice « Extension RAM 64 Ko » (F. Broche, 1987), chapitre IV
static telestrat_desc_t telestrat_desc(void) {
    telestrat_desc_t d = {
        .audio = {.callback = {.func = audio_callback}, .sample_rate = 22050},
        .minitel = {.tx = minitel_tx, .rx = minitel_rx},
    };
    d.banks[0].type = TELESTRAT_BANK_RAM;
#ifdef TELESTRAT_RAM64K
    // Cartouche RAM 64 Ko dans le port droit (banques 1-4)
    for (int i = 1; i <= 4; i++) d.banks[i].type = TELESTRAT_BANK_RAM;
#else
    d.banks[2] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_teleass};
    d.banks[3] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_telematic};
#endif
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_hyperbas};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, telestrat_telemon24};
    return d;
}

/*-- Disquettes sur clé USB -------------------------------------------------*/

#define USB_MAX_FILES 32
static char usb_files[USB_MAX_FILES][13];  // noms 8.3 des .dsk de la racine
static int usb_num_files = 0;
static bool usb_scanned = false;
static FIL usb_fil;
static bool usb_fil_open = false;
static int current_image = -1;

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
    (void)ctx;
    UINT n = 0;
    if (!usb_fil_open || f_lseek(&usb_fil, offset) != FR_OK) return false;
    if (f_read(&usb_fil, buf, len, &n) != FR_OK) return false;
    return n == len;
}

static bool usb_write(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len) {
    (void)ctx;
    UINT n = 0;
    if (!usb_fil_open || f_lseek(&usb_fil, offset) != FR_OK) return false;
    if (f_write(&usb_fil, buf, len, &n) != FR_OK || n != len) return false;
    return f_sync(&usb_fil) == FR_OK;
}

static void usb_scan(void) {
    DIR dir;
    FILINFO fno;
    usb_num_files = 0;
    if (f_opendir(&dir, "/") != FR_OK) return;
    while (usb_num_files < USB_MAX_FILES && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
        if (fno.fattrib & AM_DIR) continue;
        if (has_ext(fno.fname, ".dsk")) {
            strncpy(usb_files[usb_num_files], fno.fname, 12);
            usb_files[usb_num_files][12] = 0;
            usb_num_files++;
        }
    }
    f_closedir(&dir);
    printf("USB : %d image(s) .dsk\n", usb_num_files);
}

// Insère l'image `index` de la clé dans le lecteur A
static void insert_image(int index) {
    if (index < 0 || index >= usb_num_files) return;
    wd1793_eject(&state.telestrat.fdc.wd, 0);  // réécrit la piste en attente
    if (usb_fil_open) {
        f_close(&usb_fil);
        usb_fil_open = false;
    }
    const char *name = usb_files[index];
    bool rw = f_open(&usb_fil, name, FA_READ | FA_WRITE) == FR_OK;
    if (!rw && f_open(&usb_fil, name, FA_READ) != FR_OK) {
        printf("USB : %s illisible\n", name);
        return;
    }
    usb_fil_open = true;
    if (!wd1793_insert_streamed(&state.telestrat.fdc.wd, 0, f_size(&usb_fil), usb_read, rw ? usb_write : NULL, NULL)) {
        printf("USB : %s n'est pas une image MFM_DISK\n", name);
        f_close(&usb_fil);
        usb_fil_open = false;
        return;
    }
    current_image = index;
    printf("Lecteur A : %s%s\n", name, rw ? "" : " (protégée)");
}

// À chaque trame : à la première apparition de la clé, liste et insère la première image
static void usb_poll(void) {
    if (usb_scanned || !msc_inquiry_complete) return;
    usb_scanned = true;
    read_config();
    usb_scan();
    if (usb_num_files > 0) insert_image(0);
}

// TELESTRA.CFG : « dial=hôte:port », « listen=port » (une clé par ligne)
static void read_config(void) {
    FIL f;
    if (f_open(&f, "TELESTRA.CFG", FA_READ) != FR_OK) return;
    char line[96];
    while (f_gets(line, sizeof(line), &f)) {
        char *e = line + strlen(line);
        while (e > line && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
        if (!strncmp(line, "dial=", 5)) {
            snprintf(cfg_dial, sizeof(cfg_dial), "%.63s", line + 5);
        } else if (!strncmp(line, "listen=", 7)) {
            cfg_listen = atoi(line + 7);
        }
    }
    f_close(&f);
    printf("TELESTRA.CFG : dial=%s listen=%d\n", cfg_dial, cfg_listen);
    if (modem_idx >= 0) hayes_line_init(&modem, modem_write, NULL, cfg_dial, cfg_listen);
}

void app_init(void) {
    minitel_line_t line = hayes_line_line(&modem);
    minitel_port_init(&minitel, &line);
    telestrat_desc_t desc = telestrat_desc();
    telestrat_init(&state.telestrat, &desc);
    telestrat_reset(&state.telestrat);
}

#ifdef OLIMEX_NEO6502
// TMDS bit clock 295.2 MHz, DVDD 1.2V
#define FRAME_WIDTH  800
#define FRAME_HEIGHT 480
#define VREG_VSEL    VREG_VOLTAGE_1_20
#define DVI_TIMING   dvi_timing_800x480p_60hz
#else
// TMDS bit clock 372 MHz, DVDD 1.3V
#define FRAME_WIDTH  960
#define FRAME_HEIGHT 544
#define VREG_VSEL    VREG_VOLTAGE_1_30
#define DVI_TIMING   dvi_timing_960x544p_60hz
#endif

uint32_t __not_in_flash() tmds_palette[TELESTRAT_PALETTE_SIZE * 6];
uint32_t __not_in_flash() empty_tmdsbuf[3 * FRAME_WIDTH / DVI_SYMBOLS_PER_WORD];
uint8_t __not_in_flash() scanbuf[FRAME_WIDTH];

struct dvi_inst dvi0;

void tmds_palette_init() {
    tmds_setup_palette24_symbols(telestrat_palette, tmds_palette, TELESTRAT_PALETTE_SIZE);
}

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
    switch (code) {
        case 0x13A:  // F1 : image suivante dans le lecteur A
            if (usb_num_files > 0) insert_image((current_image + 1) % usb_num_files);
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

void kbd_raw_key_up(int code) { telestrat_key_up(&state.telestrat, host_to_telestrat(code)); }

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

// Rendu 3x des lignes (utils.S de l'Oric de reload-emulator)
extern void oric_render_scanline_3x(const uint32_t *pixbuf, uint32_t *scanbuf, size_t n_pix);
extern void copy_tmdsbuf(uint32_t *dest, const uint32_t *src);

static inline void __not_in_flash_func(render_scanline)(const uint32_t *pixbuf, uint32_t *scanbuf, size_t n_pix) {
    interp_config c;

    c = interp_default_config();
    interp_config_set_cross_result(&c, true);
    interp_config_set_shift(&c, 0);
    interp_config_set_mask(&c, 0, 3);
    interp_config_set_signed(&c, false);
    interp_set_config(interp0, 0, &c);

    c = interp_default_config();
    interp_config_set_cross_result(&c, false);
    interp_config_set_shift(&c, 4);
    interp_config_set_mask(&c, 0, 31);
    interp_config_set_signed(&c, false);
    interp_set_config(interp0, 1, &c);

    oric_render_scanline_3x(pixbuf, scanbuf, n_pix);
}

#define EMPTY_LINES   ((FRAME_HEIGHT - TELESTRAT_SCREEN_HEIGHT * 2) / 4)
#define EMPTY_COLUMNS ((FRAME_WIDTH - TELESTRAT_SCREEN_WIDTH * 3) / 2)

static inline void __not_in_flash_func(render_empty_scanlines)() {
    for (int y = 0; y < EMPTY_LINES; y += 2) {
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        copy_tmdsbuf(tmdsbuf, empty_tmdsbuf);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);

        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        copy_tmdsbuf(tmdsbuf, empty_tmdsbuf);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
}

static inline void __not_in_flash_func(render_frame)() {
    for (int y = 0; y < TELESTRAT_SCREEN_HEIGHT; y++) {
        uint32_t *tmdsbuf;
        queue_remove_blocking_u32(&dvi0.q_tmds_free, &tmdsbuf);
        render_scanline((const uint32_t *)(&state.telestrat.fb[y * 120]), (uint32_t *)(&scanbuf[EMPTY_COLUMNS]), 120);
        tmds_encode_palette_data((const uint32_t *)scanbuf, tmds_palette, tmdsbuf, FRAME_WIDTH,
                                 TELESTRAT_PALETTE_BITS);
        queue_add_blocking_u32(&dvi0.q_tmds_valid, &tmdsbuf);
    }
}

void __not_in_flash_func(core1_main()) {
    audio_init(_AUDIO_PIN, 22050);

    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);

    while (1) {
        render_empty_scanlines();
        render_frame();
        render_empty_scanlines();
    }

    __builtin_unreachable();
}

// Sondes de mesure (tools/rp2040_load.py) : un cycle de lecture et un cycle
// d'écriture complets du pilote de bus, pour en compter les cycles
__attribute__((noinline, section(".time_critical.telestrat"))) void telestrat_bus_probe_read(void) {
    static neo6502bus_t c;
    neo6502bus_tick(&c);
    neo6502bus_set_data((uint8_t)c.addr);
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
    }

    vreg_set_voltage(VREG_VSEL);
    sleep_ms(10);
    set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

    stdio_init_all();
    tusb_init();

    dvi0.timing = &DVI_TIMING;
    dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
    dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

    tmds_palette_init();
    tmds_encode_palette_data((const uint32_t *)scanbuf, tmds_palette, empty_tmdsbuf, FRAME_WIDTH,
                             TELESTRAT_PALETTE_BITS);

    hw_set_bits(&bus_ctrl_hw->priority, BUSCTRL_BUS_PRIORITY_PROC1_BITS);
    multicore_launch_core1(core1_main);

    app_init();

    while (1) {
        uint32_t start_time_in_micros = time_us_32();

        // Une trame PAL de l'ULA : 312 lignes x 64 cycles, par tranches de
        // 1 ms (sonnerie à 50 Hz sur CB1)
        const uint32_t num_ticks = 19968;
        for (uint32_t slice = 0; slice < 20; slice++) {
            const uint32_t n = slice < 19 ? 998 : num_ticks - 19 * 998;
            for (uint32_t ticks = 0; ticks < n; ticks++) {
                telestrat_tick(&state.telestrat);
            }
            modem_poll();
            hayes_line_tick(&modem, 1000);
            telestrat_set_ring(&state.telestrat, minitel_port_tick(&minitel, 1000));
        }

        telestrat_screen_update(&state.telestrat);
        telestrat_kbd_update(&state.telestrat, num_ticks);
        tuh_task();
        usb_poll();

        uint32_t execution_time = time_us_32() - start_time_in_micros;
        int sleep_time = (int)num_ticks - (int)execution_time;
        if (sleep_time > 0) {
            sleep_us(sleep_time);
        }
    }

    __builtin_unreachable();
}
