#pragma once

// printer_out.h — sortie des imprimantes émulées (pages PNG, tracés SVG)
//
// Interface commune aux rendus printer_fx80.h et plotter_mcp40.h : la
// plate-forme ouvre un fichier nouveau (nom choisi par elle, extension
// donnée : « PNG », « SVG »), y écrit, peut revenir en arrière (seek) pour
// compléter un en-tête, et ferme. Fonctions CRC-32 (PNG) et Adler-32 (zlib)
// sans grande table (16 entrées, en flash sur le Neo6502).
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

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool (*open)(void* ctx, const char* ext);  // nouveau fichier ; false : pas de sortie
    void (*write)(void* ctx, const void* data, uint32_t len);
    void (*seek)(void* ctx, uint32_t pos);     // position absolue (NULL : pas de retour)
    void (*close)(void* ctx);
    void* ctx;
} printer_out_t;

static inline bool printer_out_ok(const printer_out_t* o) { return o->open && o->write && o->close; }

// CRC-32 (polynôme 0xEDB88320), quatre bits à la fois
static const uint32_t _printer_crc_nibble[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

static inline uint32_t printer_crc32(uint32_t crc, const uint8_t* p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = _printer_crc_nibble[crc & 15] ^ (crc >> 4);
        crc = _printer_crc_nibble[crc & 15] ^ (crc >> 4);
    }
    return ~crc;
}

// Adler-32 : état (b << 16) | a, départ 1
static inline uint32_t printer_adler32(uint32_t adler, const uint8_t* p, uint32_t n) {
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (n) {
        uint32_t k = n < 3800 ? n : 3800;  // pas de débordement avant le modulo
        n -= k;
        while (k--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return b << 16 | a;
}

static inline void printer_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
