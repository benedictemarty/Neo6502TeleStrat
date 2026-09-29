#pragma once

// printer_files.h — sortie des imprimantes émulées dans un répertoire (banc PC)
//
// Fichiers IMPR0001.PNG, IMPR0002.SVG… : le premier numéro libre du
// répertoire, une seule suite pour les deux extensions (comme sur la clé USB du Neo6502).
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
#include <string.h>

#include "devices/printer_out.h"

typedef struct {
    const char* dir;
    int next;           // prochain numéro essayé
    FILE* f;
    char name[512];     // dernier fichier ouvert
} printer_files_t;

static bool _printer_files_open(void* ctx, const char* ext) {
    printer_files_t* pf = (printer_files_t*)ctx;
    for (; pf->next <= 9999; pf->next++) {
        // Numéro libre pour les deux extensions (une seule suite)
        bool taken = false;
        for (int k = 0; k < 2 && !taken; k++) {
            snprintf(pf->name, sizeof(pf->name), "%s/IMPR%04d.%s", pf->dir, pf->next, k ? "SVG" : "PNG");
            FILE* t = fopen(pf->name, "rb");
            if (t) fclose(t), taken = true;
        }
        if (taken) continue;
        snprintf(pf->name, sizeof(pf->name), "%s/IMPR%04d.%s", pf->dir, pf->next, ext);
        pf->f = fopen(pf->name, "wb");
        pf->next++;
        return pf->f != NULL;
    }
    return false;
}

static void _printer_files_write(void* ctx, const void* d, uint32_t n) {
    printer_files_t* pf = (printer_files_t*)ctx;
    if (pf->f) fwrite(d, 1, n, pf->f);
}

static void _printer_files_seek(void* ctx, uint32_t pos) {
    printer_files_t* pf = (printer_files_t*)ctx;
    if (pf->f) fseek(pf->f, (long)pos, SEEK_SET);
}

static void _printer_files_close(void* ctx) {
    printer_files_t* pf = (printer_files_t*)ctx;
    if (pf->f) fclose(pf->f);
    pf->f = NULL;
}

static inline printer_out_t printer_files_out(printer_files_t* pf, const char* dir) {
    memset(pf, 0, sizeof(*pf));
    pf->dir = dir;
    pf->next = 1;
    return (printer_out_t){_printer_files_open, _printer_files_write, _printer_files_seek, _printer_files_close, pf};
}
