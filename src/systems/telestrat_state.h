#pragma once

// telestrat_state.h — instantanés (savestates) du Telestrat
//
// Fichier : en-tête (« TELESTRAT ETAT », version, signature de la variante),
// texte de la plate-forme (cartouches, supports : lignes clé=valeur comme
// TELESTRA.CFG), registres du processeur, puis l'état de la machine : RAM,
// banques de RAM, banque visible, VIA 1 et 2, AY, clavier, ACIA, registres
// du contrôleur de disquettes (piste, secteur, données, lecteur, face, têtes
// des lecteurs, $0314), cadence (compteur de cycles, échéances de l'AY),
// imprimante (STROBE, ACK), attributs vidéo.
//
// Registres : telestrat_cpu_capture / telestrat_cpu_restore (NMI détourné,
// telestrat.h), qui marchent aussi avec le vrai 65C02 du Neo6502.
//
// À inclure après systems/telestrat.h.
//
// Pas enregistré : les supports (disquettes, cassette : restent ceux
// insérés, comme sur une vraie machine), l'enregistreur, le joystick, la
// ligne. Refusé pendant un accès disque (commande du WD1793 en cours).
// Les structures des puces sont copiées telles quelles : un instantané ne se
// relit que sur la même plate-forme et la même variante (signature).
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
#include <string.h>

#define TELESTRAT_STATE_VERSION 4  // 2 : horloge audio fractionnaire (v0.16.8) ; 3 : WD1793 du socle ; 4 : via6522 (v0.16.29)
#define TELESTRAT_STATE_INFO_MAX 1024

// Lecture ou écriture de len octets ; false : erreur
typedef bool (*telestrat_state_io_t)(void* ctx, void* data, uint32_t len);

typedef struct {
    char magic[16];      // "TELESTRAT ETAT"
    uint32_t version;
    uint32_t signature;  // tailles des structures, pointeurs, banques de RAM
    uint32_t info_len;
} telestrat_state_header_t;

// Registres du contrôleur de disquettes (pas l'opération en cours)
typedef struct {
    uint8_t status, track, sector, data, drive, side, ctrl;
    bool step_in, intrq;
    uint8_t head[WD1793_MAX_DRIVES];
} _telestrat_state_fdc_t;

// Divers de telestrat_t
typedef struct {
    uint8_t bank;
    bool strobe;
    int32_t printer_ack;
    int blink_counter;
    uint8_t pattr;
    uint32_t system_ticks, psg_next, psg_next_sample, sample_acc;
} _telestrat_state_misc_t;

static inline uint32_t telestrat_state_signature(void) {
    uint32_t s = 2166136261u;
    const uint32_t v[] = {(uint32_t)sizeof(TELESTRAT_VIA_T), (uint32_t)sizeof(ay38910psg_t), (uint32_t)sizeof(kbd_t),
                          (uint32_t)sizeof(mos6551acia_t), (uint32_t)sizeof(void*), TELESTRAT_MAX_RAM_BANKS,
                          (uint32_t)sizeof(_telestrat_state_fdc_t), (uint32_t)sizeof(_telestrat_state_misc_t)};
    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) s = (s ^ v[i]) * 16777619u;
    return s;
}

#define _TELESTRAT_IO(p, n)                  \
    do {                                     \
        if (!io(ctx, (void*)(p), (uint32_t)(n))) { \
            *err = "écriture impossible";    \
            return false;                    \
        }                                    \
    } while (0)

// Enregistre la machine (info : texte de la plate-forme, "" si aucun).
// Le programme en cours continue (quelques cycles pour lire les registres).
static inline bool telestrat_state_save(telestrat_t* sys, const char* info, telestrat_state_io_t io, void* ctx,
                                        const char** err) {
    const wd1793_t* w = &sys->fdc.wd;
    if (telestrat_fdc_busy(&sys->fdc)) {
        *err = "accès disque en cours : réessayer";
        return false;
    }
    telestrat_regs_t r;
    if (!telestrat_cpu_capture(sys, &r)) {
        *err = "le processeur n'a pas répondu (NMI)";
        return false;
    }
    telestrat_state_header_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "TELESTRAT ETAT", 14);
    h.version = TELESTRAT_STATE_VERSION;
    h.signature = telestrat_state_signature();
    h.info_len = info ? (uint32_t)strlen(info) : 0;
    if (h.info_len > TELESTRAT_STATE_INFO_MAX) h.info_len = TELESTRAT_STATE_INFO_MAX;
    _TELESTRAT_IO(&h, sizeof(h));
    if (h.info_len) _TELESTRAT_IO(info, h.info_len);
    _TELESTRAT_IO(&r, sizeof(r));
    _TELESTRAT_IO(sys->ram, sizeof(sys->ram));
    _TELESTRAT_IO(sys->bank_ram, sizeof(sys->bank_ram));
    _TELESTRAT_IO(&sys->via, sizeof(sys->via));
    _TELESTRAT_IO(&sys->via2, sizeof(sys->via2));
    _TELESTRAT_IO(&sys->psg, sizeof(sys->psg));
    _TELESTRAT_IO(&sys->kbd, sizeof(sys->kbd));
    _TELESTRAT_IO(&sys->acia, sizeof(sys->acia));
    _telestrat_state_fdc_t f;
    memset(&f, 0, sizeof(f));
    f.status = w->status;
    f.track = w->track;
    f.sector = w->sector;
    f.data = w->data;
    f.drive = (uint8_t)w->drive;
    f.side = (uint8_t)w->side;
    f.ctrl = sys->fdc.ctrl;
    f.step_in = w->step_in;
    f.intrq = w->intrq;
    for (int d = 0; d < WD1793_MAX_DRIVES; d++) f.head[d] = w->head[d];
    _TELESTRAT_IO(&f, sizeof(f));
    _telestrat_state_misc_t m;
    memset(&m, 0, sizeof(m));
    m.bank = sys->bank;
    m.strobe = sys->strobe;
    m.printer_ack = sys->printer_ack;
    m.blink_counter = sys->blink_counter;
    m.pattr = sys->pattr;
    m.system_ticks = sys->system_ticks;
    m.psg_next = sys->psg_next;
    m.psg_next_sample = sys->psg_next_sample;
    m.sample_acc = sys->sample_acc;
    _TELESTRAT_IO(&m, sizeof(m));
    return true;
}

#undef _TELESTRAT_IO
#define _TELESTRAT_IO(p, n)                  \
    do {                                     \
        if (!io(ctx, (void*)(p), (uint32_t)(n))) { \
            *err = "fichier tronqué";        \
            return false;                    \
        }                                    \
    } while (0)

// Première étape du chargement : en-tête et texte de la plate-forme (info,
// terminé par 0) ; la plate-forme remet alors ses cartouches, puis appelle
// telestrat_state_load_machine. Rien n'est encore changé dans la machine.
static inline bool telestrat_state_load_info(telestrat_state_io_t io, void* ctx, char* info, size_t cap,
                                             const char** err) {
    telestrat_state_header_t h;
    _TELESTRAT_IO(&h, sizeof(h));
    if (memcmp(h.magic, "TELESTRAT ETAT", 14) || h.version != TELESTRAT_STATE_VERSION) {
        *err = "pas un instantané du Telestrat (ou d'une autre version)";
        return false;
    }
    if (h.signature != telestrat_state_signature()) {
        *err = "instantané d'une autre plate-forme ou variante";
        return false;
    }
    if (h.info_len > TELESTRAT_STATE_INFO_MAX || h.info_len >= cap) {
        *err = "en-tête invalide";
        return false;
    }
    if (h.info_len) _TELESTRAT_IO(info, h.info_len);
    info[h.info_len] = 0;
    return true;
}

// Seconde étape : la machine. Une erreur ici laisse une machine incohérente
// (la plate-forme la redémarre à froid).
static inline bool telestrat_state_load_machine(telestrat_t* sys, telestrat_state_io_t io, void* ctx,
                                                const char** err) {
    telestrat_regs_t r;
    _TELESTRAT_IO(&r, sizeof(r));
    _TELESTRAT_IO(sys->ram, sizeof(sys->ram));
    _TELESTRAT_IO(sys->bank_ram, sizeof(sys->bank_ram));
    // Rappels et pointeurs de la VIA : ceux de ce programme, pas ceux de
    // l'instantané (adresses d'un autre lancement ou d'un autre firmware)
    const via6522_t keep[2] = {sys->via, sys->via2};
    _TELESTRAT_IO(&sys->via, sizeof(sys->via));
    _TELESTRAT_IO(&sys->via2, sizeof(sys->via2));
    via6522_t* v[2] = {&sys->via, &sys->via2};
    for (int i = 0; i < 2; i++) {
        v[i]->porta_read = keep[i].porta_read;
        v[i]->porta_write = keep[i].porta_write;
        v[i]->portb_read = keep[i].portb_read;
        v[i]->portb_write = keep[i].portb_write;
        v[i]->userdata = keep[i].userdata;
        v[i]->irq_callback = keep[i].irq_callback;
        v[i]->irq_userdata = keep[i].irq_userdata;
    }
    // AY, ACIA : rappels de la plate-forme gardés
    const ay38910psg_t psg = sys->psg;
    _TELESTRAT_IO(&sys->psg, sizeof(sys->psg));
    sys->psg.in_cb = psg.in_cb;
    sys->psg.out_cb = psg.out_cb;
    sys->psg.user_data = psg.user_data;
    _TELESTRAT_IO(&sys->kbd, sizeof(sys->kbd));
    const mos6551acia_t acia = sys->acia;
    _TELESTRAT_IO(&sys->acia, sizeof(sys->acia));
    sys->acia.tx_cb = acia.tx_cb;
    sys->acia.rx_cb = acia.rx_cb;
    sys->acia.user_data = acia.user_data;
    _telestrat_state_fdc_t f;
    _TELESTRAT_IO(&f, sizeof(f));
    wd1793_t* w = &sys->fdc.wd;
    wd1793_flush(w);  // piste modifiée des disquettes insérées : écrite
    wd1793_reset(w);  // commande abandonnée ; disques insérés gardés
    w->status = f.status;
    w->track = f.track;
    w->sector = f.sector;
    w->data = f.data;
    wd1793_select(w, f.drive, f.side);
    w->step_in = f.step_in;
    w->intrq = f.intrq;
    for (int d = 0; d < WD1793_MAX_DRIVES; d++) w->head[d] = f.head[d];
    sys->fdc.ctrl = f.ctrl;
    _telestrat_state_misc_t m;
    _TELESTRAT_IO(&m, sizeof(m));
    sys->strobe = m.strobe;
    sys->printer_ack = m.printer_ack;
    sys->printer_wait = false;
    sys->blink_counter = m.blink_counter;
    sys->pattr = m.pattr;
    sys->system_ticks = m.system_ticks;
    sys->psg_next = m.psg_next;
    sys->psg_next_sample = m.psg_next_sample;
    sys->sample_acc = m.sample_acc;
    sys->tape_due = m.system_ticks;
    sys->deferred = 0;
    sys->bank = 0xFF;  // forcer la sélection
    telestrat_select_bank(sys, m.bank & 7);
    sys->screen_dirty = true;
    sys->inputs_dirty = true;
    sys->quiet_until = sys->system_ticks;
    if (!telestrat_cpu_restore(sys, &r)) {
        *err = "le processeur n'a pas répondu (NMI)";
        return false;
    }
    return true;
}

#undef _TELESTRAT_IO
