#pragma once

// telestrat_frame.h — une ligne de tampon de la sortie DVI (960 x 272, chaque
// ligne affichée deux fois) : menu plein écran, sinon image du Telestrat
// centrée (720 x 224, pixels triplés) et, dans la marge du bas, le bandeau de
// la cassette. Utilisé par le cœur 1 du firmware et par le banc PC (-D), pour
// que la capture du banc soit celle de la carte.
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <stdint.h>
#include <string.h>

#include "osd/osd.h"
#include "telestrat_video.h"

#define TELESTRAT_FRAME_LINES 272  // 960x544

// y : ligne de tampon ; width, lines : sortie (960 x 272 ; 800 x 240 en
// 800x480) ; menu / banner : NULL s'ils ne sont pas affichés (tous deux
// demandent 960 pixels : l'appelant ne les passe pas en 800x480) ; plans de
// width / 32 mots. Bandeau : 8 lignes, à 16 lignes du bas (marge sous l'image).
static inline void TELESTRAT_VIDEO_RAM telestrat_frame_line(int y, int width, int lines, const uint8_t* fb,
                                                             const osd_surface_t* menu, const osd_row_t* banner,
                                                             uint32_t* red, uint32_t* green, uint32_t* blue) {
    if (menu) {
        osd_render_line(menu, y, red, green, blue);
        return;
    }
    const int banner_line = lines - 16;
    if (banner && y >= banner_line && y < banner_line + 8) {
        osd_render_cells(banner->ch, banner->attr, banner->big, y - banner_line, y & 1, red, green, blue);
        return;
    }
    const int src = y - (lines - 224) / 2;
    if (src >= 0 && src < 224) {
        telestrat_video_line(&fb[src * TELESTRAT_VIDEO_BYTES_PER_LINE], red, green, blue,
                             (unsigned)(width - TELESTRAT_VIDEO_PIXELS) / 2);
    } else {
        // Bordure noire : mots volatils, pas memset (en flash, appelé depuis le
        // cœur 1 : tools/core1_flash.py du socle) ; volatile empêche GCC de
        // reconnaître la boucle et d'y remettre memset
        volatile uint32_t* r = red;
        volatile uint32_t* g = green;
        volatile uint32_t* b = blue;
        for (int i = 0; i < width / 32; i++) r[i] = g[i] = b[i] = 0;
    }
}
