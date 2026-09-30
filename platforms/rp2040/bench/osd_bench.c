// osd_bench.c — coût d'une ligne affichée par le cœur 1 (tools/osd_cost.py)
//
// Jamais flashé : exécuté dans un émulateur Cortex-M0+. bench_osd_line rend
// une ligne du menu (osd_render_line), bench_video_line une ligne de l'image
// du Telestrat (telestrat_video_line), dans les mêmes conditions que le
// firmware (code, police et tables en RAM).
//
// Copyright (c) 2026 bmarty — licence zlib/libpng (voir src/osd/osd.h)

#include <stdint.h>
#include <string.h>

#include <pico/platform.h>

#define OSD_FONT_SECTION __attribute__((section(".time_critical.osd_font")))
#include "osd/osd_menu.h"
#include "telestrat_video.h"

static osd_surface_t bench_surf;
static uint8_t bench_fb[120 * 224];
static uint32_t bench_planes[3][30];

// Surface représentative du menu, sans la bibliothèque C (snprintf) :
// bandeau et grandes lettres, panneaux tramés, texte sur toutes les lignes
__attribute__((noinline, used)) void bench_osd_setup(void) {
    osd_clear(&bench_surf, OSD_BG);
    osd_fill(&bench_surf, 0, 0, 3, OSD_COLS, OSD_ATTR(OSD_WHITE, OSD_BLUE));
    osd_puts_big(&bench_surf, 1, 3, "TELESTRAT", OSD_ATTR(OSD_WHITE, OSD_BLUE));
    for (int r = 4; r < OSD_ROWS; r++) {
        osd_fill(&bench_surf, r, 2, 1, 56, OSD_PANEL);
        osd_fill(&bench_surf, r, 61, 1, 57, OSD_PANEL);
        osd_puts(&bench_surf, r, 4, "Banque 7  TELEMON 2.4   STRATSED.DSK  écriture", OSD_PANEL_ACC, -1);
        osd_puts(&bench_surf, r, 63, "Disquette pour le lecteur B — 1001 Ko", OSD_SEL, -1);
    }
    telestrat_video_init();
    for (unsigned i = 0; i < sizeof(bench_fb); i++) bench_fb[i] = (uint8_t)(i * 37);
}

__attribute__((noinline, used, section(".time_critical.bench_osd"))) void bench_osd_line(int line) {
    osd_render_line(&bench_surf, line, bench_planes[0], bench_planes[1], bench_planes[2]);
}

__attribute__((noinline, used, section(".time_critical.bench_osd"))) void bench_video_line(int line) {
    telestrat_video_line(&bench_fb[line * TELESTRAT_VIDEO_BYTES_PER_LINE], bench_planes[0], bench_planes[1],
                         bench_planes[2], 120);
}

int main(void) {
    bench_osd_setup();
    bench_osd_line(0);
    bench_video_line(0);
    return 0;
}
