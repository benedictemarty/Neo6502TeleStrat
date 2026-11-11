#pragma once

// banc_config.h — configurations de banques du banc PC (-c CONFIG), partagées par
// platforms/pc/telestrat_headless.c et tests/replay.c (une seule table, O-23)
//
//   standard   0 RAM, 2 TELE-ASS, 3 TELEMATIC, 6 HYPER-BASIC, 7 TELEMON
//   ram64k     cartouche RAM 64 Ko à droite (1-4 RAM), 6 HYPER-BASIC, 7 TELEMON
//   oricutron  configuration par défaut d'Oricutron (0-4 RAM, 5 TELE-ASS, 6 HYPER-BASIC, 7 TELEMON)
//   telemon    TELEMON seul (0 RAM, 7 TELEMON)
//   atmos      cartouche Atmos (BASIC 1.1) en banque 7 : mode Atmos, lecteur de cassette
//
// À inclure après roms/telestrat_roms.h et systems/telestrat.h (ou telestrat_ref.h).
// Retourne 0 pour une configuration inconnue.
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

#include <string.h>

#define BANC_RAM(n) {.type = TELESTRAT_BANK_RAM}
#define BANC_ROM(p) {.type = TELESTRAT_BANK_ROM, .rom = (p)}
#define BANC_EMPTY  {.type = TELESTRAT_BANK_EMPTY}

static int banc_config_banks(const char* name, telestrat_desc_t* d) {
    const telestrat_bank_desc_t standard[8] = {BANC_RAM(0), BANC_EMPTY, BANC_ROM(telestrat_teleass), BANC_ROM(telestrat_telematic),
                                               BANC_EMPTY, BANC_EMPTY, BANC_ROM(telestrat_hyperbas), BANC_ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t ram64k[8] = {BANC_RAM(0), BANC_RAM(1), BANC_RAM(2), BANC_RAM(3),
                                             BANC_RAM(4), BANC_EMPTY, BANC_ROM(telestrat_hyperbas), BANC_ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t oricutron[8] = {BANC_RAM(0), BANC_RAM(1), BANC_RAM(2), BANC_RAM(3),
                                                BANC_RAM(4), BANC_ROM(telestrat_teleass), BANC_ROM(telestrat_hyperbas),
                                                BANC_ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t telemon[8] = {BANC_RAM(0), BANC_EMPTY, BANC_EMPTY, BANC_EMPTY, BANC_EMPTY, BANC_EMPTY, BANC_EMPTY,
                                              BANC_ROM(telestrat_telemon24)};
    const telestrat_bank_desc_t atmos[8] = {BANC_RAM(0), BANC_EMPTY, BANC_EMPTY, BANC_EMPTY, BANC_EMPTY, BANC_EMPTY, BANC_EMPTY,
                                            BANC_ROM(telestrat_atmos)};
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
