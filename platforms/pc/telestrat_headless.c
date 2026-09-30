// telestrat_headless.c
//
// Telestrat sans fenêtre ni son, pour les tests automatiques (cœur W65C02S
// cycle à cycle de reload-emulator à la place du vrai 65C02 du Neo6502).
//
//   telestrat_headless [-c config] [-f N] [-w N] [-t TEXTE] [-s] [-b] [-p f.ppm] [-r f.bin]
//                      [-0 disque.dsk] [-1 ...] [-2 ...] [-3 ...] [-W f.dsk] [-P f.txt]
//
//   -c CONFIG  standard (défaut) : 0 RAM, 2 TELE-ASS, 3 TELEMATIC, 6 HYPER-BASIC, 7 TELEMON
//              ram64k : cartouche RAM 64 Ko à droite (1-4 RAM), 6 HYPER-BASIC, 7 TELEMON
//              oricutron : configuration par défaut d'Oricutron (0-4 RAM, 5 TELE-ASS,
//              6 HYPER-BASIC, 7 TELEMON)
//              telemon : TELEMON seul (0 RAM, 7 TELEMON)
//   -f N       nombre de trames de 20 ms (défaut 150)
//   -w N       trames avant la frappe de -t (défaut 100)
//   -t TEXTE   texte tapé (une touche toutes les 4 trames ; \n = RETURN ; ~, absent du
//              clavier, ne fait qu'occuper un créneau : une pause d'une touche)
//   -k N       trames par touche pour -t (défaut 4) ; dans -t, \\f devant un caractère :
//              touche tapée avec FUNCT maintenue (ex. \\fD = FUNCT+D)
//   -s         affiche l'écran texte (28 x 40 en $BB80)
//   -b         affiche la banque courante et l'état des banques ($0200-$0207)
//   -p FICHIER écrit l'image 240 x 224 (PPM binaire)
//   -r FICHIER écrit les 48 Ko de RAM de base
//   -0..-3 F   insère l'image MFM_DISK F dans le lecteur A..D (copie en mémoire)
//   -W FICHIER écrit l'image du lecteur A (éventuellement modifiée) en fin d'exécution
//   -P FICHIER branche une imprimante dont la sortie va dans FICHIER (octets bruts)
//   -G TYPE:RÉP branche une imprimante rendue : fx80 (Epson FX-80, pages PNG) ou
//              mcp40 (table traçante MCP-40, SVG), fichiers RÉP/IMPR0001.PNG… ;
//              avec -U, le menu propose texte, fx80 et mcp40 (imprimante_type=)
//   -T FICHIER trace les octets émis par l'ACIA (hexadécimal, avec le numéro de trame)
//   -L LIGNE   branche un Minitel sur l'ACIA et sa ligne sur TCP :
//              listen:PORT (appel entrant = client TCP) ou connect:HOTE:PORT
//   -S LIAISON branche la prise RS232 (PA4 = 1) sur TCP, liaison directe sans
//              modem : listen:PORT (le premier client) ou connect:HOTE:PORT ;
//              -T la trace en RTX/RRX (-B ne trace que la prise Minitel) ;
//              -T note aussi les bascules de prise (PA4)
//   -U RÉP     répertoire tenant lieu de clé USB pour le menu (.dsk, .rom,
//              TELESTRA.CFG : a= … d=, bank1= … bank7= appliqués au démarrage)
//   -M T:TOUCHES ouvre le menu à la trame T et y tape TOUCHES : u d l r
//              (flèches), e (Entrée), x (Échap), s (Suppr), h / z (début / fin),
//              majuscule = saut à l'initiale ; le menu se ferme sur Reprendre
//              ou RESET, sinon à la fin des touches
//   -O FICHIER image du menu (960 x 544, PPM) après les touches de -M ; FICHIER.txt :
//              texte du menu
//   -D FICHIER sortie DVI de la carte en fin d'exécution (960 x 544, PPM) : image
//              du Telestrat centrée, bandeau de la cassette si le moteur tourne
//   -C RÉP     CSAVE : cassettes enregistrées en RÉP/NOM.TAP (défaut : le
//              répertoire de -U s'il est donné)
//   -K FICHIER cassette .tap insérée (mode Atmos : -c atmos, ou cartouche Atmos)
//   -Z         cassette rapide : CLOAD du BASIC 1.1 sans attendre (ROM patchée,
//              oric_tape_turbo.h) ; avec -U, TELESTRA.CFG cassette_rapide= prime
//   -Y         moteur de la cassette toujours en marche (câble sans relais)
//   -X T:FICHIER instantané de la machine à la trame T (telestrat_state.h)
//   -Q T       démarrage à froid (RAM effacée, RESET) à la trame T, comme le RESET du
//              menu ; aussi dans le banc de référence
//   -J FICHIER reprend un instantané au démarrage (même configuration -c ; avec
//              -U, les cartouches de l'instantané sont remises)
//   -R         temps réel (trames de 20 ms cadencées), pour dialoguer avec la ligne
//   -B PRÉFIXE enregistre la trace (tests/replay.c, tools/rp2040_load.py) : PRÉFIXE.trace
//              (un mot par cycle : adresse | R/W << 16 | IRQ << 17 | donnée << 24),
//              .ev (clavier), .aud (échantillons audio), .ser (octets série et leur
//              cycle), .ring (sonnerie par tranche de 1 ms)
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

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "roms/telestrat_roms.h"

#include "chips/chips_common.h"
#include "chips/w65c02cpu.h"
#include "chips/mos6522via.h"
#include "chips/ay38910psg.h"
#include "chips/kbd.h"
#include "chips/mem.h"
#include "chips/clk.h"
#include "devices/wd1793.h"
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "devices/minitel_port.h"
#ifdef TELESTRAT_REF
#include "systems/telestrat_ref.h"
#define telestrat_key_down(s, c)   kbd_key_down(&(s)->kbd, c)
#define telestrat_key_up(s, c)     kbd_key_up(&(s)->kbd, c)
#define telestrat_kbd_update(s, u) kbd_update(&(s)->kbd, u)
#else
#include "systems/telestrat.h"
#endif
#include "line_tcp.h"
#ifndef TELESTRAT_REF  // la référence figée n'a pas de menu
#include "menu_pc.h"
#include "telestrat_frame.h"
#include "devices/printer_fx80.h"
#include "devices/plotter_mcp40.h"
#include "printer_files.h"
#endif
#include <time.h>

#define RAM(n)  {.type = TELESTRAT_BANK_RAM}
#define ROM(p)  {.type = TELESTRAT_BANK_ROM, .rom = (p)}
#define EMPTY   {.type = TELESTRAT_BANK_EMPTY}

static int config_banks(const char* name, telestrat_desc_t* d) {
    const telestrat_bank_desc_t standard[8] = {RAM(0), EMPTY, ROM(telestrat_teleass), ROM(telestrat_telematic),
                                               EMPTY, EMPTY, ROM(telestrat_hyperbas), ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t ram64k[8] = {RAM(0), RAM(1), RAM(2), RAM(3),
                                             RAM(4), EMPTY, ROM(telestrat_hyperbas), ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t oricutron[8] = {RAM(0), RAM(1), RAM(2), RAM(3),
                                                RAM(4), ROM(telestrat_teleass), ROM(telestrat_hyperbas),
                                                ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t telemon[8] = {RAM(0), EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
                                              ROM(telestrat_telemon24)};
    // Cartouche Atmos (BASIC 1.1) en banque 7 : mode Atmos, lecteur de cassette
    const telestrat_bank_desc_t atmos[8] = {RAM(0), EMPTY, EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
                                            ROM(telestrat_atmos)};
    const telestrat_bank_desc_t* src;
    if (!strcmp(name, "standard")) src = standard;
    else if (!strcmp(name, "ram64k")) src = ram64k;
    else if (!strcmp(name, "oricutron")) src = oricutron;
    else if (!strcmp(name, "telemon")) src = telemon;
    else if (!strcmp(name, "atmos")) src = atmos;
    else return 0;
    memcpy(d->banks, src, sizeof(d->banks));
    return 1;
}

// Imprimante et ligne coupées par le menu (-U : impression=non, modem=non)
static bool printer_enabled = true, modem_enabled = true;

#ifndef TELESTRAT_REF
// Rendu de l'imprimante (-G) : Epson FX-80 (pages PNG) ou table traçante
// MCP-40 (SVG) ; fin de travail après 10 s sans octet, comme le firmware
#define PRINTER_IDLE_FRAMES 500
static int printer_type = OSD_PRINTER_TEXT;
static bool printer_render_on = false;
static printer_files_t printer_files;
static fx80_t printer_fx;
static mcp40_t printer_mcp;
static int printer_idle = -1;  // trames depuis le dernier octet (-1 : pas de travail)

// Instantanés (-X, -J)
static bool state_file_write(void* ctx, void* d, uint32_t n) { return fwrite(d, 1, n, (FILE*)ctx) == n; }
static bool state_file_read(void* ctx, void* d, uint32_t n) { return fread(d, 1, n, (FILE*)ctx) == n; }

static void printer_job_end(void) {
    if (printer_idle < 0) return;
    printer_idle = -1;
    if (printer_type == OSD_PRINTER_FX80) {
        while (fx80_busy(&printer_fx)) fx80_service(&printer_fx, 1 << 30);
        fx80_finish(&printer_fx);
        while (fx80_busy(&printer_fx)) fx80_service(&printer_fx, 1 << 30);
    } else if (printer_type == OSD_PRINTER_MCP40) {
        mcp40_finish(&printer_mcp);
    }
}

// Modèle choisi (menu) : le travail en cours de l'autre modèle est terminé
static void printer_select(int type) {
    if (type == printer_type) return;
    printer_job_end();
    printer_type = type;
}
#endif

static void printer_out(uint8_t data, void* user_data) {
    if (!printer_enabled) return;
    if (user_data) fputc(data, (FILE*)user_data);
#ifndef TELESTRAT_REF
    if (!printer_render_on || printer_type == OSD_PRINTER_TEXT) return;
    printer_idle = 0;
    if (printer_type == OSD_PRINTER_FX80) {
        while (fx80_busy(&printer_fx)) fx80_service(&printer_fx, 1 << 30);
        fx80_feed(&printer_fx, data);
    } else {
        mcp40_feed(&printer_mcp, data);
    }
#endif
}

// Trace de bus pour la mesure de charge du RP2040
static FILE* bench_trace = NULL;
static uint32_t bench_events[4096];
static uint32_t bench_event_count = 0;

static FILE *bench_aud = NULL, *bench_ser = NULL, *bench_ring = NULL, *bench_fb = NULL;

// Empreinte FNV-1a de l'image (comparée par tests/replay.c)
static uint32_t fb_hash(const uint8_t* fb, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ fb[i]) * 16777619u;
    return h;
}

static void bench_audio(const uint8_t sample, void* user_data) {
    (void)user_data;
    if (bench_aud) fputc(sample, bench_aud);
}

static void bench_serial(uint32_t cycle, int dir, uint8_t data) {
    if (bench_ser) {
        uint32_t w[2] = {cycle, (uint32_t)dir << 8 | data};
        fwrite(w, 4, 2, bench_ser);
    }
}

static void bench_key(int frame, int down, int code) {
    if (bench_trace && bench_event_count < 4096) {
        bench_events[bench_event_count++] = ((uint32_t)frame << 16) | (down ? 0x8000u : 0) | (uint32_t)(code & 0x7FFF);
    }
}

static FILE* serial_trace = NULL;
static int current_frame = 0;
static minitel_port_t minitel;
static bool minitel_on = false;

static telestrat_t sys;

static void serial_tx(uint8_t data, void* user_data) {
    if (!modem_enabled) return;
    (void)user_data;
    bench_serial(sys.system_ticks, 0, data);
    if (serial_trace) fprintf(serial_trace, "%d TX %02X\n", current_frame, data);
    if (minitel_on) minitel_port_from_telestrat(&minitel, data);
}

static int serial_rx(void* user_data) {
    if (!modem_enabled) return -1;
    (void)user_data;
    if (!minitel_on) return -1;
    int c = minitel_port_to_telestrat(&minitel);
    if (c >= 0) bench_serial(sys.system_ticks, 1, (uint8_t)c);
    if (c >= 0 && serial_trace) fprintf(serial_trace, "%d RX %02X\n", current_frame, c);
    return c;
}

// Prise RS232 : liaison TCP directe (ouverte d'emblée ou au premier client)
static line_tcp_t rs232_link;

static void rs232_tx(uint8_t data, void* user_data) {
    (void)user_data;
    if (serial_trace) fprintf(serial_trace, "%d RTX %02X\n", current_frame, data);
    if (line_tcp_incoming(&rs232_link)) line_tcp_answer(&rs232_link);
    line_tcp_send(&rs232_link, data);
}

static int rs232_rx(void* user_data) {
    (void)user_data;
    if (line_tcp_incoming(&rs232_link)) line_tcp_answer(&rs232_link);
    int c = line_tcp_recv(&rs232_link);
    if (c >= 0 && serial_trace) fprintf(serial_trace, "%d RRX %02X\n", current_frame, c);
    return c;
}

#ifndef TELESTRAT_REF
// Sortie DVI de la carte (telestrat_frame.h) : 960 x 272 lignes doublées
static void write_dvi(const char* path, const char* tape_name) {
    static osd_row_t banner;
    const oric_tape_rec_t* rec = &sys.tape_rec;
    const bool on = oric_tape_running(&sys.tape) || oric_tape_rec_active(rec);
    if (oric_tape_rec_active(rec)) {
        const uint32_t total = rec->written + rec->remaining;
        osd_tape_banner(&banner, "Écriture", rec->file, total ? (int)(rec->written * 100 / total) : 0);
    } else if (on) {
        const char* base = strrchr(tape_name, '/');
        osd_tape_banner(&banner, "Lecture", base ? base + 1 : tape_name, oric_tape_percent(&sys.tape));
    }
    telestrat_video_init();
    FILE* f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n960 544\n255\n");
    static uint32_t r[30], g[30], b[30];
    static uint8_t rgb[960 * 3];
    for (int y = 0; y < TELESTRAT_FRAME_LINES; y++) {
        telestrat_frame_line(y, 960, TELESTRAT_FRAME_LINES, sys.fb, NULL, on ? &banner : NULL, r, g, b);
        for (int x = 0; x < 960; x++) {
            rgb[3 * x] = (r[x >> 5] >> (x & 31) & 1) ? 255 : 0;
            rgb[3 * x + 1] = (g[x >> 5] >> (x & 31) & 1) ? 255 : 0;
            rgb[3 * x + 2] = (b[x >> 5] >> (x & 31) & 1) ? 255 : 0;
        }
        fwrite(rgb, 1, sizeof(rgb), f);
        fwrite(rgb, 1, sizeof(rgb), f);
    }
    fclose(f);
}
#endif

// CSAVE : fichiers .tap du répertoire d'enregistrement
static const char* rec_path_dir;
static FILE* rec_file;
static bool rec_open(void* ctx, const char* name) {
    (void)ctx;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", rec_path_dir, name);
    rec_file = fopen(path, "wb");
    fprintf(stderr, "cassette : enregistrement de %s\n", path);
    return rec_file != NULL;
}
static void rec_write(void* ctx, const uint8_t* data, uint32_t len) {
    (void)ctx;
    if (rec_file) fwrite(data, 1, len, rec_file);
}
static void rec_close(void* ctx) {
    (void)ctx;
    if (rec_file) fclose(rec_file);
    rec_file = NULL;
}

// Cassette en mémoire
static uint8_t* tape_image;
static size_t tape_size;
static bool tape_read(void* ctx, uint32_t offset, uint8_t* buf, uint32_t len) {
    (void)ctx;
    if ((size_t)offset + len > tape_size) return false;
    memcpy(buf, tape_image + offset, len);
    return true;
}

static uint8_t* load_file(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "%s : lecture impossible\n", path);
        exit(1);
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static void print_screen(void) {
    for (int y = 0; y < 28; y++) {
        char line[41];
        for (int x = 0; x < 40; x++) {
            uint8_t c = sys.ram[0xBB80 + y * 40 + x] & 0x7F;
            line[x] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
        }
        int n = 40;
        while (n > 0 && line[n - 1] == ' ') n--;
        line[n] = 0;
        printf("%s\n", line);
    }
}

static void write_ppm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", TELESTRAT_SCREEN_WIDTH, TELESTRAT_SCREEN_HEIGHT);
    for (int i = 0; i < TELESTRAT_FRAMEBUFFER_SIZE; i++) {
        uint8_t two = sys.fb[i];
        for (int k = 0; k < 2; k++) {
            uint32_t c = telestrat_palette[k ? (two & 15) : (two >> 4)];
            uint8_t rgb[3] = {(c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char** argv) {
    const char* config = "standard";
    int frames = 150, wait = 100;
    const char* text = NULL;
    const char* ppm = NULL;
    const char* ramfile = NULL;
    const char* disks[4] = {NULL, NULL, NULL, NULL};
    const char* write_disk = NULL;
    const char* printer_file = NULL;
    const char* render_spec = NULL;
    const char* line_spec = NULL;
    const char* rs232_spec = NULL;
    const char* usb_dir = NULL;
    const char* tape_file = NULL;
    bool tape_turbo = false, tape_motor_always = false;
    int state_frame = -1;
    const char *state_save = NULL, *state_load = NULL;
    const char* dvi_file = NULL;
    const char* rec_dir = NULL;
    const char* menu_script = NULL;
    const char* menu_ppm = NULL;
    int menu_frame = -1;
    int cold_frame = -1;  // -Q : démarrage à froid à cette trame (référence comprise)
#ifndef TELESTRAT_REF
    static menu_pc_t menu_pc;
#endif
    int realtime = 0;
    int key_period = 4;
    const char* bench_prefix = NULL;
    static line_tcp_t line;
    int show_screen = 0, show_banks = 0;
    int opt;
    while ((opt = getopt(argc, argv, "c:f:w:t:sbp:r:0:1:2:3:W:P:G:T:L:S:U:M:O:K:D:C:RB:k:ZYX:J:Q:")) != -1) {
        switch (opt) {
            case 'c': config = optarg; break;
            case 'f': frames = atoi(optarg); break;
            case 'w': wait = atoi(optarg); break;
            case 't': text = optarg; break;
            case 's': show_screen = 1; break;
            case 'b': show_banks = 1; break;
            case 'p': ppm = optarg; break;
            case 'r': ramfile = optarg; break;
            case '0': case '1': case '2': case '3': disks[opt - '0'] = optarg; break;
            case 'W': write_disk = optarg; break;
            case 'P': printer_file = optarg; break;
            case 'G': render_spec = optarg; break;
            case 'L': line_spec = optarg; break;
            case 'S': rs232_spec = optarg; break;
            case 'U': usb_dir = optarg; break;
            case 'M':
                menu_frame = atoi(optarg);
                menu_script = strchr(optarg, ':') ? strchr(optarg, ':') + 1 : "";
                break;
            case 'O': menu_ppm = optarg; break;
            case 'Q': cold_frame = atoi(optarg); break;
            case 'K': tape_file = optarg; break;
            case 'Z': tape_turbo = true; break;
            case 'X':
                state_frame = atoi(optarg);
                state_save = strchr(optarg, ':') ? strchr(optarg, ':') + 1 : NULL;
                break;
            case 'J': state_load = optarg; break;
            case 'Y': tape_motor_always = true; break;
            case 'D': dvi_file = optarg; break;
            case 'C': rec_dir = optarg; break;
            case 'R': realtime = 1; break;
            case 'k': key_period = atoi(optarg) < 2 ? 2 : atoi(optarg); break;
            case 'B': bench_prefix = optarg; break;
            case 'T':
                serial_trace = fopen(optarg, "w");
                if (!serial_trace) {
                    perror(optarg);
                    return 1;
                }
                break;
            default:
                fprintf(stderr, "usage : %s [-c config] [-f N] [-w N] [-t texte] [-s] [-b] [-p f.ppm] [-r f.bin]\n",
                        argv[0]);
                return 2;
        }
    }

    telestrat_desc_t desc = {0};
    if (!config_banks(config, &desc)) {
        fprintf(stderr, "configuration inconnue : %s\n", config);
        return 2;
    }
    FILE* printer = NULL;
    if (printer_file) {
        printer = fopen(printer_file, "wb");
        if (!printer) {
            perror(printer_file);
            return 1;
        }
        desc.printer.func = printer_out;
        desc.printer.user_data = printer;
    }
#ifndef TELESTRAT_REF
    if (render_spec) {
        const char* dir = strchr(render_spec, ':');
        char type[8] = "";
        if (dir && dir - render_spec < (int)sizeof(type)) memcpy(type, render_spec, (size_t)(dir - render_spec));
        const int t = osd_printer_type(type, -1);
        if (!dir || t < 0) {
            fprintf(stderr, "-G fx80:RÉP ou mcp40:RÉP\n");
            return 2;
        }
        const printer_out_t out = printer_files_out(&printer_files, dir + 1);
        fx80_init(&printer_fx, &out, osd_font);
        mcp40_init(&printer_mcp, &out);
        printer_type = t;
        printer_render_on = true;
        desc.printer.func = printer_out;
        desc.printer.user_data = printer;
    }
#endif
    if (bench_prefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s.trace", bench_prefix);
        bench_trace = fopen(path, "wb");
        if (!bench_trace) {
            perror(path);
            return 1;
        }
        snprintf(path, sizeof(path), "%s.aud", bench_prefix);
        bench_aud = fopen(path, "wb");
        snprintf(path, sizeof(path), "%s.ser", bench_prefix);
        bench_ser = fopen(path, "wb");
        snprintf(path, sizeof(path), "%s.ring", bench_prefix);
        bench_ring = fopen(path, "wb");
        snprintf(path, sizeof(path), "%s.fb", bench_prefix);
        bench_fb = fopen(path, "wb");
        desc.audio.callback.func = bench_audio;
    }
    if (line_spec) {
        if (!line_tcp_open(&line, line_spec)) {
            fprintf(stderr, "ligne invalide : %s\n", line_spec);
            return 2;
        }
        minitel_line_t l = line_tcp_line(&line);
        minitel_port_init(&minitel, &l);
        minitel_on = true;
    }
    if (rs232_spec) {
        if (!line_tcp_open(&rs232_link, rs232_spec)) {
            fprintf(stderr, "liaison RS232 invalide : %s\n", rs232_spec);
            return 2;
        }
        if (rs232_link.listen_fd < 0 && !line_tcp_dial(&rs232_link)) {
            fprintf(stderr, "liaison RS232 : connexion impossible (%s)\n", rs232_spec);
            return 1;
        }
        desc.rs232.tx = rs232_tx;
        desc.rs232.rx = rs232_rx;
    }
    if (serial_trace || minitel_on || bench_prefix) {
        desc.minitel.tx = serial_tx;
        desc.minitel.rx = serial_rx;
    }
#ifndef TELESTRAT_REF
    if (menu_script && !usb_dir) usb_dir = ".";
    // ROM en mémoire modifiable (lecture accélérée : BASIC 1.1 patché) ; avec
    // -U, les emplacements du menu en tiennent lieu (et gardent l'adresse
    // d'origine qui nomme les ROM intégrées)
    static uint8_t rom_copy[8][0x4000];
    for (int b = 0; b < 8 && !usb_dir; b++) {
        if (desc.banks[b].type != TELESTRAT_BANK_ROM || !desc.banks[b].rom) continue;
        memcpy(rom_copy[b], desc.banks[b].rom, 0x4000);
        desc.banks[b].rom = rom_copy[b];
    }
    if (usb_dir) menu_pc_prepare(&menu_pc, &desc);
#endif
    telestrat_init(&sys, &desc);
    uint8_t* images[4] = {NULL, NULL, NULL, NULL};
    size_t image_sizes[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) {
        if (!disks[i]) continue;
        images[i] = load_file(disks[i], &image_sizes[i]);
        if (!telestrat_insert_disk(&sys, i, images[i], image_sizes[i], false)) {
            fprintf(stderr, "%s : image MFM_DISK invalide\n", disks[i]);
            return 1;
        }
    }
#ifndef TELESTRAT_REF
    if (usb_dir) {
        menu_pc.printer_file = printer_file;
        menu_pc.printer_type = printer_type;
        menu_pc.tape_turbo = tape_turbo;
        menu_pc.tape_motor_always = tape_motor_always;
        menu_pc.printer_types = printer_render_on ? OSD_PRINTER_TYPES : 1;
        menu_pc.printer_last = printer_files.name;
        menu_pc.line_present = minitel_on;
        menu_pc_init(&menu_pc, &sys, usb_dir, "banc PC");
        printer_enabled = menu_pc.printer_on;
        printer_select(menu_pc.printer_type);
        modem_enabled = menu_pc.modem_on;
    } else {
        telestrat_tape_options(&sys, tape_turbo, tape_motor_always);
        for (int b = 0; b < 8; b++) oric_turbo_apply(rom_copy[b], tape_turbo);
    }
#else
    (void)usb_dir;
    (void)menu_ppm;
    (void)render_spec;
    (void)tape_turbo;
    (void)tape_motor_always;
    (void)state_frame;
    (void)state_save;
    (void)state_load;
    (void)menu_frame;
    if (menu_script) {
        fprintf(stderr, "menu absent de la référence\n");
        return 2;
    }
#endif
    if (!rec_dir) rec_dir = usb_dir;
    if (rec_dir) {
#ifndef TELESTRAT_REF
        rec_path_dir = rec_dir;
        const oric_tape_rec_out_t out = {rec_open, rec_write, rec_close, NULL};
        telestrat_tape_recorder(&sys, &out);
#endif
    }
    if (tape_file) {
        tape_image = load_file(tape_file, &tape_size);
#ifndef TELESTRAT_REF
        telestrat_tape_insert(&sys, (uint32_t)tape_size, tape_read, NULL);
#else
        fprintf(stderr, "cassette absente de la référence\n");
        return 2;
#endif
    }
    telestrat_reset(&sys);
#ifndef TELESTRAT_REF
    // Reprise d'un instantané : après le RESET (le processeur y est ramené)
    if (state_load) {
        const char* err = "";
        bool ok;
        if (usb_dir) {
            // Par le menu : cartouches de l'instantané remises
            const char* base = strrchr(state_load, '/');
            ok = menu_pc_state_load(&menu_pc, &sys, base ? base + 1 : state_load, &err);
        } else {
            FILE* sf = fopen(state_load, "rb");
            static char info[TELESTRAT_STATE_INFO_MAX + 1];
            ok = sf && telestrat_state_load_info(state_file_read, sf, info, sizeof(info), &err) &&
                 telestrat_state_load_machine(&sys, state_file_read, sf, &err);
            if (sf) fclose(sf);
            else err = "illisible";
        }
        if (!ok) {
            fprintf(stderr, "%s : %s\n", state_load, err);
            return 1;
        }
    }
#endif

    size_t pos = 0, len = text ? strlen(text) : 0;
    int key_down = 0;
    int funct = 0;
    for (int frame = 0; frame < frames; frame++) {
        if (text && frame >= wait && pos < len && ((frame - wait) % key_period) == 0) {
            int c = (unsigned char)text[pos];
            funct = 0;
            if (c == '\\' && pos + 1 < len && text[pos + 1] == 'f' && pos + 2 < len) {
                // \f : touche suivante avec FUNCT maintenue (code 0x146)
                funct = 1;
                pos += 2;
                c = (unsigned char)text[pos];
            }
            if (c == '\\' && pos + 1 < len && text[pos + 1] == 'n') {
                c = 0x0D;
                pos++;
            } else if (c == '\n') {
                c = 0x0D;
            }
            if (funct) {
                telestrat_key_down(&sys, 0x146);
                bench_key(frame, 1, 0x146);
            }
            telestrat_key_down(&sys, c);
            bench_key(frame, 1, c);
            key_down = c;
            pos++;
        } else if (key_down && ((frame - wait) % key_period) == key_period / 2) {
            telestrat_key_up(&sys, key_down);
            bench_key(frame, 0, key_down);
            if (funct) {
                telestrat_key_up(&sys, 0x146);
                bench_key(frame, 0, 0x146);
                funct = 0;
            }
            key_down = 0;
        }
        current_frame = frame;
        if (frame == cold_frame) telestrat_cold_reset(&sys);
#ifndef TELESTRAT_REF
        if (printer_idle >= 0 && ++printer_idle >= PRINTER_IDLE_FRAMES) printer_job_end();
        if (frame == state_frame && state_save) {
            static char info[TELESTRAT_STATE_INFO_MAX];
            info[0] = 0;
            if (usb_dir) menu_pc_state_info(&menu_pc, &sys, info, sizeof(info));
            FILE* sf = fopen(state_save, "wb");
            const char* err = "écriture impossible";
            if (!sf || !telestrat_state_save(&sys, info, state_file_write, sf, &err))
                fprintf(stderr, "%s : %s\n", state_save, err);
            if (sf) fclose(sf);
        }
        if (frame == menu_frame) {
            menu_pc_script(&menu_pc, &sys, menu_script);
            printer_enabled = menu_pc.printer_on;
            printer_select(menu_pc.printer_type);
            if (!printer_enabled) printer_job_end();
            modem_enabled = menu_pc.modem_on;
            if (menu_ppm) menu_pc_ppm(&menu_pc, menu_ppm);
        }
#endif
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        // Trame de 20 ms par tranches de 1 ms (sonnerie à 50 Hz)
        for (int ms = 0; ms < 20; ms++) {
            for (int i = 0; i < 1000; i++) {
                telestrat_tick(&sys);
                if (bench_trace) {
                    uint32_t e = sys.cpu.addr | ((uint32_t)sys.cpu.rw << 16) | ((uint32_t)(sys.cpu.irq ? 1 : 0) << 17) |
                                 ((uint32_t)sys.cpu.data << 24);
                    fwrite(&e, 4, 1, bench_trace);
                }
            }
            if (serial_trace) {  // bascule de prise (PA4 du VIA 2), vue à la milliseconde
                static int prise = -1;
                int p = telestrat_serial_is_rs232(&sys);
                if (p != prise) fprintf(serial_trace, "%d PA4 %s\n", frame, p ? "RS232" : "MINITEL");
                prise = p;
            }
            if (minitel_on) {
                bool ring = modem_enabled && minitel_port_tick(&minitel, 1000);
                if (bench_ring && ring != sys.ring) {
                    uint32_t w[2] = {(uint32_t)(frame * 20 + ms), ring};
                    fwrite(w, 4, 2, bench_ring);
                }
                telestrat_set_ring(&sys, ring);
            }
        }
        telestrat_kbd_update(&sys, 20000);
        telestrat_screen_update(&sys);
        if (bench_fb) {
            uint32_t h = fb_hash(sys.fb, sizeof(sys.fb));
            fwrite(&h, 4, 1, bench_fb);
        }
        if (serial_trace) fflush(serial_trace);
        if (realtime) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            long used = (t1.tv_sec - t0.tv_sec) * 1000000L + (t1.tv_nsec - t0.tv_nsec) / 1000;
            if (used < 20000) usleep((useconds_t)(20000 - used));
        }
    }
    sys.screen_dirty = true;
    telestrat_screen_update(&sys);

    if (show_screen) print_screen();
    if (show_banks) {
        printf("banque courante : %d\n", sys.bank);
        printf("etat des banques ($0200-$0207) :");
        for (int i = 0; i < 8; i++) printf(" %02X", sys.ram[0x200 + i]);
        printf("\n");
    }
    if (ppm) write_ppm(ppm);
#ifndef TELESTRAT_REF
    printer_job_end();
    if (usb_dir) menu_pc_finish(&menu_pc, &sys);
    if (dvi_file) write_dvi(dvi_file, tape_file ? tape_file : menu_pc.tape_name);
#else
    (void)dvi_file;
#endif
    if (write_disk && images[0]) {
        FILE* f = fopen(write_disk, "wb");
        if (!f || fwrite(images[0], 1, image_sizes[0], f) != image_sizes[0]) {
            perror(write_disk);
            return 1;
        }
        fclose(f);
    }
    if (printer) fclose(printer);
    if (bench_aud) fclose(bench_aud);
    if (bench_ser) fclose(bench_ser);
    if (bench_ring) fclose(bench_ring);
    if (bench_fb) fclose(bench_fb);
    if (bench_trace) {
        fclose(bench_trace);
        char path[512];
        snprintf(path, sizeof(path), "%s.ev", bench_prefix);
        FILE* f = fopen(path, "wb");
        if (f) {
            fwrite(&bench_event_count, 4, 1, f);
            fwrite(bench_events, 4, bench_event_count, f);
            fclose(f);
        }
    }
    if (ramfile) {
        FILE* f = fopen(ramfile, "wb");
        if (!f) {
            perror(ramfile);
            return 1;
        }
        fwrite(sys.ram, 1, sizeof(sys.ram), f);
        fclose(f);
    }
    return 0;
}
