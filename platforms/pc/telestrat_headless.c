// telestrat_headless.c
//
// Telestrat sans fenêtre ni son, pour les tests automatiques (cœur W65C02S
// cycle à cycle de reload-emulator à la place du vrai 65C02 du Neo6502).
//
//   telestrat_headless [-c config] [-f N] [-w N] [-t TEXTE] [-s] [-b] [-p f.ppm] [-r f.bin]
//
//   -c CONFIG  standard (défaut) : 0 RAM, 2 TELE-ASS, 3 TELEMATIC, 6 HYPER-BASIC, 7 TELEMON
//              ram64k : cartouche RAM 64 Ko à droite (1-4 RAM), 6 HYPER-BASIC, 7 TELEMON
//              oricutron : configuration par défaut d'Oricutron (0-4 RAM, 5 TELE-ASS,
//              6 HYPER-BASIC, 7 TELEMON)
//              telemon : TELEMON seul (0 RAM, 7 TELEMON)
//   -f N       nombre de trames de 20 ms (défaut 150)
//   -w N       trames avant la frappe de -t (défaut 100)
//   -t TEXTE   texte tapé (une touche toutes les 4 trames ; \n = RETURN)
//   -s         affiche l'écran texte (28 x 40 en $BB80)
//   -b         affiche la banque courante et l'état des banques ($0200-$0207)
//   -p FICHIER écrit l'image 240 x 224 (PPM binaire)
//   -r FICHIER écrit les 48 Ko de RAM de base
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
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "systems/telestrat.h"

static telestrat_t sys;

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
    const telestrat_bank_desc_t* src;
    if (!strcmp(name, "standard")) src = standard;
    else if (!strcmp(name, "ram64k")) src = ram64k;
    else if (!strcmp(name, "oricutron")) src = oricutron;
    else if (!strcmp(name, "telemon")) src = telemon;
    else return 0;
    memcpy(d->banks, src, sizeof(d->banks));
    return 1;
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
    int show_screen = 0, show_banks = 0;
    int opt;
    while ((opt = getopt(argc, argv, "c:f:w:t:sbp:r:")) != -1) {
        switch (opt) {
            case 'c': config = optarg; break;
            case 'f': frames = atoi(optarg); break;
            case 'w': wait = atoi(optarg); break;
            case 't': text = optarg; break;
            case 's': show_screen = 1; break;
            case 'b': show_banks = 1; break;
            case 'p': ppm = optarg; break;
            case 'r': ramfile = optarg; break;
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
    telestrat_init(&sys, &desc);
    telestrat_reset(&sys);

    size_t pos = 0, len = text ? strlen(text) : 0;
    int key_down = 0;
    for (int frame = 0; frame < frames; frame++) {
        if (text && frame >= wait && pos < len && ((frame - wait) % 4) == 0) {
            int c = (unsigned char)text[pos];
            if (c == '\\' && pos + 1 < len && text[pos + 1] == 'n') {
                c = 0x0D;
                pos++;
            } else if (c == '\n') {
                c = 0x0D;
            }
            kbd_key_down(&sys.kbd, c);
            key_down = c;
            pos++;
        } else if (key_down && ((frame - wait) % 4) == 2) {
            kbd_key_up(&sys.kbd, key_down);
            key_down = 0;
        }
        telestrat_exec(&sys, 20000);
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
