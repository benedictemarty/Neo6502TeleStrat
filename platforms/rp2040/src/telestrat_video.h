#pragma once

// telestrat_video.h — une ligne de l'image Telestrat vers 3 plans de 1 bit
//
// L'image (telestrat_t.fb) a 240 pixels de 4 bits par ligne (deux par octet,
// pixel de gauche dans les 4 bits de poids fort ; couleur : bit 0 rouge,
// bit 1 vert, bit 2 bleu). Chaque pixel est triplé en largeur (720 pixels)
// et réparti sur trois plans de 1 bit (rouge, vert, bleu ; bit 0 d'un mot =
// pixel de gauche, comme l'attend tmds_encode_1bpp de PicoDVI). Les 8
// couleurs de l'Oric valent 0x00 ou 0xFF par canal : trois encodages 1 bpp
// remplacent l'encodage à palette, trop lent sur le cœur 1 (72 us par ligne
// mesurés sur carte par le BBC de reload-emulator, pour un budget de 59 à
// 63 us ; 34 us avec les plans).
//
// Code en C pur (testé sur PC par tests/test_telestrat.c).
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

#include <stdint.h>

#ifndef TELESTRAT_VIDEO_RAM
#define TELESTRAT_VIDEO_RAM
#endif

#define TELESTRAT_VIDEO_BYTES_PER_LINE 120  // 240 pixels de 4 bits
#define TELESTRAT_VIDEO_PIXELS         720  // après triplement

// Octet de l'image (deux pixels) -> 6 bits par plan (pixel de gauche en bits
// 0-2), plan p dans l'octet p du mot
static uint32_t TELESTRAT_VIDEO_RAM telestrat_video_lut[256];

static inline void telestrat_video_init(void) {
    for (int b = 0; b < 256; b++) {
        uint32_t w = 0;
        for (int p = 0; p < 3; p++) {
            uint32_t left = ((b >> 4) >> p) & 1, right = ((b & 15) >> p) & 1;
            w |= ((left * 7u) | ((right * 7u) << 3)) << (8 * p);
        }
        telestrat_video_lut[b] = w;
    }
}

// Écrit une ligne dans les plans (tableaux de mots, un bit par pixel de
// sortie) à partir du pixel x0 ; les bits hors de [x0, x0 + 720) ne sont pas
// modifiés, sauf ceux des mots partiels des deux bords, remis à 0 (marges).
static inline void TELESTRAT_VIDEO_RAM telestrat_video_line(const uint8_t* src, uint32_t* red, uint32_t* green,
                                                              uint32_t* blue, unsigned x0) {
    uint32_t* w0 = red + (x0 >> 5);
    uint32_t* w1 = green + (x0 >> 5);
    uint32_t* w2 = blue + (x0 >> 5);
    unsigned pos = x0 & 31;
    uint32_t a0 = 0, a1 = 0, a2 = 0;
    for (int i = 0; i < TELESTRAT_VIDEO_BYTES_PER_LINE; i++) {
        const uint32_t v = telestrat_video_lut[src[i]];
        const uint32_t v0 = v & 63, v1 = (v >> 8) & 63, v2 = (v >> 16) & 63;
        a0 |= v0 << pos;
        a1 |= v1 << pos;
        a2 |= v2 << pos;
        pos += 6;
        if (pos >= 32) {
            *w0++ = a0;
            *w1++ = a1;
            *w2++ = a2;
            pos -= 32;
            // Bits du groupe de 6 qui débordent sur le mot suivant
            const unsigned used = 6 - pos;
            a0 = pos ? v0 >> used : 0;
            a1 = pos ? v1 >> used : 0;
            a2 = pos ? v2 >> used : 0;
        }
    }
    if (pos) {
        *w0 = a0;
        *w1 = a1;
        *w2 = a2;
    }
}
