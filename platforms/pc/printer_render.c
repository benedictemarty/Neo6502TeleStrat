// printer_render.c — rend une impression brute (octets du port Centronics)
//
//   printer_render fx80|mcp40 ENTRÉE RÉPERTOIRE
//
// fx80 : pages PNG d'une Epson FX-80 (src/devices/printer_fx80.h) ;
// mcp40 : tracé SVG de la table traçante MCP-40 (src/devices/plotter_mcp40.h).
// Fichiers IMPR0001.PNG… dans RÉPERTOIRE. Sert aux tests (tests/test_printer.sh)
// et à rendre après coup un fichier IMPRIM.TXT de la clé.
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

#define _POSIX_C_SOURCE 200809L  // fileno (pilote neo_storage POSIX)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osd/osd_font.h"
#include "devices/printer_fx80.h"
#include "devices/plotter_mcp40.h"
#define NEO_STORAGE_IMPL
#include "devices/neo_storage.h"
#include "neo_storage_posix.h"
#include "printer_files.h"

static fx80_t fx;
static mcp40_t mcp;

int main(int argc, char** argv) {
    if (argc != 4 || (strcmp(argv[1], "fx80") && strcmp(argv[1], "mcp40"))) {
        fprintf(stderr, "usage : %s fx80|mcp40 ENTRÉE RÉPERTOIRE\n", argv[0]);
        return 2;
    }
    FILE* in = fopen(argv[2], "rb");
    if (!in) {
        perror(argv[2]);
        return 1;
    }
    static printer_files_t pf;
    neo_storage_set(0, "Fichiers", &neo_storage_posix_ops, "");  // chemins tels quels
    const printer_out_t out = printer_files_out(&pf, 0, argv[3]);
    const bool plotter = !strcmp(argv[1], "mcp40");
    if (plotter) mcp40_init(&mcp, &out);
    else fx80_init(&fx, &out, osd_font);
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (plotter) {
            mcp40_feed(&mcp, (uint8_t)c);
        } else {
            while (fx80_busy(&fx)) fx80_service(&fx, 1 << 30);
            fx80_feed(&fx, (uint8_t)c);
        }
    }
    fclose(in);
    if (plotter) {
        mcp40_finish(&mcp);
        printf("%u fichier(s) SVG\n", (unsigned)mcp.files);
    } else {
        while (fx80_busy(&fx)) fx80_service(&fx, 1 << 30);
        fx80_finish(&fx);
        while (fx80_busy(&fx)) fx80_service(&fx, 1 << 30);
        printf("%u page(s) PNG\n", (unsigned)fx.pages);
    }
    return 0;
}
