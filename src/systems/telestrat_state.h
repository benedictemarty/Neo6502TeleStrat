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
// Intégrité : tout ce qui suit le texte de la plate-forme est couvert par un
// CRC-32 écrit en fin de fichier, et les valeurs lues sont bornées avant
// d'être appliquées. Les pointeurs de l'hôte (rappels des VIA, de l'AY et de
// l'ACIA) sont écrits à zéro : le fichier est déterministe et ne fuit aucune
// adresse ; au chargement, les rappels restent ceux de ce programme, quelle
// que soit l'issue (fichier tronqué compris).
//
// À inclure après systems/telestrat.h, compilé avec CHIPS_IMPL (utilise ses
// fonctions internes) ; telestrat_regs_t et telestrat_cpu_capture / _restore
// sont déclarés dans la partie publique de telestrat.h.
//
// Pas enregistré : les supports (disquettes, cassette : restent ceux
// insérés, comme sur une vraie machine), l'enregistreur, le joystick, la
// ligne. Refusé pendant un accès disque (commande du WD1793 en cours).
// Les structures des puces sont copiées telles quelles : un instantané ne se
// relit que sur la même plate-forme et la même variante (signature : tailles,
// positions de champs clés, endianness).
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
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// 2 : horloge audio fractionnaire (v0.16.8) ; 3 : WD1793 du socle ; 4 : via6522 (v0.16.29) ;
// 5 : CRC-32, pointeurs à zéro, endianness, printer_wait, signature renforcée
#define TELESTRAT_STATE_VERSION 5
#define TELESTRAT_STATE_INFO_MAX 1024
#define TELESTRAT_STATE_ENDIAN 0x01020304u

// Lecture ou écriture de len octets ; false : erreur
typedef bool (*telestrat_state_io_t)(void* ctx, void* data, uint32_t len);

typedef struct {
    char magic[16];      // "TELESTRAT ETAT"
    uint32_t version;
    uint32_t signature;  // tailles des structures, positions de champs, pointeurs, banques de RAM
    uint32_t endian;     // TELESTRAT_STATE_ENDIAN tel qu'écrit par l'hôte
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
    bool printer_wait;
    int32_t printer_ack;
    int blink_counter;
    uint8_t pattr;
    uint32_t system_ticks, psg_next, psg_next_sample, sample_acc;
} _telestrat_state_misc_t;

static inline uint32_t telestrat_state_signature(void) {
    uint32_t s = 2166136261u;
    const uint32_t v[] = {(uint32_t)sizeof(TELESTRAT_VIA_T), (uint32_t)sizeof(ay38910psg_t), (uint32_t)sizeof(kbd_t),
                          (uint32_t)sizeof(mos6551acia_t), (uint32_t)sizeof(void*), TELESTRAT_MAX_RAM_BANKS,
                          (uint32_t)sizeof(_telestrat_state_fdc_t), (uint32_t)sizeof(_telestrat_state_misc_t),
                          (uint32_t)sizeof(telestrat_regs_t), (uint32_t)sizeof(int), (uint32_t)sizeof(bool),
                          // Positions de champs clés (un champ ajouté ou déplacé change la disposition)
                          (uint32_t)offsetof(TELESTRAT_VIA_T, ifr), (uint32_t)offsetof(TELESTRAT_VIA_T, irq_callback),
                          (uint32_t)offsetof(ay38910psg_t, tone), (uint32_t)offsetof(ay38910psg_t, sample),
                          (uint32_t)offsetof(kbd_t, key_buffer), (uint32_t)offsetof(mos6551acia_t, tx_cb),
                          (uint32_t)offsetof(_telestrat_state_misc_t, system_ticks)};
    for (size_t i = 0; i < sizeof(v) / sizeof(v[0]); i++) s = (s ^ v[i]) * 16777619u;
    return s;
}

// CRC-32 (polynôme réfléchi 0xEDB88320, par quartets : table de 16 entrées)
static inline uint32_t _telestrat_state_crc(uint32_t crc, const void* p, uint32_t n) {
    static const uint32_t t[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
                                   0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                   0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t* b = (const uint8_t*)p;
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= b[i];
        crc = (crc >> 4) ^ t[crc & 15];
        crc = (crc >> 4) ^ t[crc & 15];
    }
    return ~crc;
}

// Rappels et pointeurs de l'hôte dans les structures des puces : jamais ceux
// du fichier (adresses d'un autre lancement ou d'un autre firmware)
typedef struct {
    struct {
        uint8_t (*porta_read)(void*);
        void (*porta_write)(uint8_t, void*);
        uint8_t (*portb_read)(void*);
        void (*portb_write)(uint8_t, void*);
        void* userdata;
        void (*irq_callback)(bool, void*);
        void* irq_userdata;
    } via[2];
    ay38910psg_in_t psg_in;
    ay38910psg_out_t psg_out;
    void* psg_user;
    mos6551acia_tx_t acia_tx;
    mos6551acia_rx_t acia_rx;
    void* acia_user;
    uint32_t acia_cpu_freq;
} _telestrat_state_hooks_t;

static inline void _telestrat_state_hooks_take(const telestrat_t* sys, _telestrat_state_hooks_t* h) {
    const via6522_t* v[2] = {&sys->via, &sys->via2};
    for (int i = 0; i < 2; i++) {
        h->via[i].porta_read = v[i]->porta_read;
        h->via[i].porta_write = v[i]->porta_write;
        h->via[i].portb_read = v[i]->portb_read;
        h->via[i].portb_write = v[i]->portb_write;
        h->via[i].userdata = v[i]->userdata;
        h->via[i].irq_callback = v[i]->irq_callback;
        h->via[i].irq_userdata = v[i]->irq_userdata;
    }
    h->psg_in = sys->psg.in_cb;
    h->psg_out = sys->psg.out_cb;
    h->psg_user = sys->psg.user_data;
    h->acia_tx = sys->acia.tx_cb;
    h->acia_rx = sys->acia.rx_cb;
    h->acia_user = sys->acia.user_data;
    h->acia_cpu_freq = sys->acia.cpu_freq;
}

static inline void _telestrat_state_hooks_put(telestrat_t* sys, const _telestrat_state_hooks_t* h) {
    via6522_t* v[2] = {&sys->via, &sys->via2};
    for (int i = 0; i < 2; i++) {
        v[i]->porta_read = h->via[i].porta_read;
        v[i]->porta_write = h->via[i].porta_write;
        v[i]->portb_read = h->via[i].portb_read;
        v[i]->portb_write = h->via[i].portb_write;
        v[i]->userdata = h->via[i].userdata;
        v[i]->irq_callback = h->via[i].irq_callback;
        v[i]->irq_userdata = h->via[i].irq_userdata;
    }
    sys->psg.in_cb = h->psg_in;
    sys->psg.out_cb = h->psg_out;
    sys->psg.user_data = h->psg_user;
    sys->acia.tx_cb = h->acia_tx;
    sys->acia.rx_cb = h->acia_rx;
    sys->acia.user_data = h->acia_user;
    sys->acia.cpu_freq = h->acia_cpu_freq;
}

static inline void _telestrat_state_hooks_clear(telestrat_t* sys) {
    _telestrat_state_hooks_t z;
    memset(&z, 0, sizeof(z));
    z.acia_cpu_freq = sys->acia.cpu_freq;  // une valeur, pas un pointeur : écrite telle quelle
    _telestrat_state_hooks_put(sys, &z);
}

#define _TELESTRAT_IO(p, n)                  \
    do {                                     \
        if (!io(ctx, (void*)(p), (uint32_t)(n))) { \
            *err = "écriture impossible";    \
            return false;                    \
        }                                    \
    } while (0)

// Écriture d'un morceau couvert par le CRC
#define _TELESTRAT_IOC(p, n)                 \
    do {                                     \
        _TELESTRAT_IO(p, n);                 \
        crc = _telestrat_state_crc(crc, (p), (uint32_t)(n)); \
    } while (0)

// Corps de l'enregistrement ; les rappels de l'hôte sont déjà mis à zéro
static inline bool _telestrat_state_save_body(telestrat_t* sys, const char* info, telestrat_state_io_t io, void* ctx,
                                              const char** err) {
    const wd1793_t* w = &sys->fdc.wd;
    // Accès disque refusé. Contrôle fait avant la capture des registres : un
    // STA $0310 exécuté pendant les quelques dizaines de cycles de la capture
    // ne serait pas vu (cas rare, non vérifié sur matériel ni en pratique).
    if (telestrat_fdc_busy(&sys->fdc)) {
        *err = "accès disque en cours : réessayer";
        return false;
    }
    telestrat_regs_t r;
    if (!telestrat_cpu_capture(sys, &r)) {
        *err = "le processeur n'a pas répondu (NMI)";
        return false;
    }
    // Pas de périphériques sautés pendant la capture ni avant : leur durée
    // est appliquée maintenant, pour que l'instantané (qui ne l'enregistre
    // pas) soit complet
    _telestrat_catch_up(sys);
    telestrat_state_header_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "TELESTRAT ETAT", 14);
    h.version = TELESTRAT_STATE_VERSION;
    h.signature = telestrat_state_signature();
    h.endian = TELESTRAT_STATE_ENDIAN;
    h.info_len = info ? (uint32_t)strlen(info) : 0;
    if (h.info_len > TELESTRAT_STATE_INFO_MAX) {
        h.info_len = TELESTRAT_STATE_INFO_MAX;
        // Coupure sur une frontière de caractère UTF-8 : le premier octet
        // laissé de côté ne doit pas être un octet de suite (10xxxxxx)
        while (h.info_len > 0 && ((uint8_t)info[h.info_len] & 0xC0) == 0x80) h.info_len--;
    }
    _TELESTRAT_IO(&h, sizeof(h));
    if (h.info_len) _TELESTRAT_IO(info, h.info_len);
    uint32_t crc = 0;
    _TELESTRAT_IOC(&r, sizeof(r));
    _TELESTRAT_IOC(sys->ram, sizeof(sys->ram));
    _TELESTRAT_IOC(sys->bank_ram, sizeof(sys->bank_ram));
    _TELESTRAT_IOC(&sys->via, sizeof(sys->via));
    _TELESTRAT_IOC(&sys->via2, sizeof(sys->via2));
    _TELESTRAT_IOC(&sys->psg, sizeof(sys->psg));
    _TELESTRAT_IOC(&sys->kbd, sizeof(sys->kbd));
    _TELESTRAT_IOC(&sys->acia, sizeof(sys->acia));
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
    _TELESTRAT_IOC(&f, sizeof(f));
    _telestrat_state_misc_t m;
    memset(&m, 0, sizeof(m));
    m.bank = sys->bank;
    m.strobe = sys->strobe;
    m.printer_wait = sys->printer_wait;
    m.printer_ack = sys->printer_ack;
    m.blink_counter = sys->blink_counter;
    m.pattr = sys->pattr;
    m.system_ticks = sys->system_ticks;
    m.psg_next = sys->psg_next;
    m.psg_next_sample = sys->psg_next_sample;
    m.sample_acc = sys->sample_acc;
    _TELESTRAT_IOC(&m, sizeof(m));
    _TELESTRAT_IO(&crc, sizeof(crc));
    return true;
}

// Enregistre la machine (info : texte de la plate-forme, "" si aucun).
// Le programme en cours continue (quelques cycles pour lire les registres).
static inline bool telestrat_state_save(telestrat_t* sys, const char* info, telestrat_state_io_t io, void* ctx,
                                        const char** err) {
    // Rappels de l'hôte mis à zéro le temps de l'écriture (jamais dans le
    // fichier), puis remis dans tous les cas
    _telestrat_state_hooks_t hooks;
    _telestrat_state_hooks_take(sys, &hooks);
    _telestrat_state_hooks_clear(sys);
    const bool ok = _telestrat_state_save_body(sys, info, io, ctx, err);
    _telestrat_state_hooks_put(sys, &hooks);
    return ok;
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
    if (h.signature != telestrat_state_signature() || h.endian != TELESTRAT_STATE_ENDIAN) {
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

// Compteur relatif à l'horloge de l'instantané : dans [0, TELESTRAT_FREQUENCY]
// cycles à venir (l'échéance n'est jamais passée ni plus d'une seconde plus loin)
static inline bool _telestrat_state_due_ok(uint32_t due, uint32_t now) {
    return (uint32_t)(due - now) <= (uint32_t)TELESTRAT_FREQUENCY;
}

#define _TELESTRAT_DELAY_OK(x) ((x) >= -(1 << 20) && (x) <= (1 << 20))

// Bornes des valeurs lues (le CRC écarte la corruption ; ceci écarte un
// fichier fabriqué ou d'un autre état interne)
static inline bool _telestrat_state_valid(const telestrat_t* sys, const _telestrat_state_fdc_t* f,
                                          const _telestrat_state_misc_t* m) {
    const via6522_t* v[2] = {&sys->via, &sys->via2};
    for (int i = 0; i < 2; i++) {
        if (v[i]->sr_count > 15 || v[i]->ca2_pulse < 0 || v[i]->ca2_pulse > 2 || v[i]->cb2_pulse < 0 ||
            v[i]->cb2_pulse > 2 || v[i]->sr_delay > 3 || v[i]->sr_t2_pending > 2)
            return false;
    }
    if (sys->psg.addr > 15) return false;
    if (!_TELESTRAT_DELAY_OK(sys->acia.tx_timer) || !_TELESTRAT_DELAY_OK(sys->acia.rx_timer)) return false;
    if (f->drive >= WD1793_MAX_DRIVES || f->side > 1) return false;
    for (int d = 0; d < WD1793_MAX_DRIVES; d++)
        if (f->head[d] > WD1793_MAX_HEAD_POS) return false;
    if (m->printer_ack < 0 || m->printer_ack > 40) return false;
    if (m->blink_counter < 0 || m->blink_counter > 0x3F || m->pattr > 7) return false;
    if (!_telestrat_state_due_ok(m->psg_next, m->system_ticks) ||
        !_telestrat_state_due_ok(m->psg_next_sample, m->system_ticks) || m->sample_acc >= TELESTRAT_FREQUENCY)
        return false;
    return true;
}

// Corps du chargement (rappels de l'hôte sauvés et remis par l'appelant)
static inline bool _telestrat_state_load_body(telestrat_t* sys, telestrat_state_io_t io, void* ctx,
                                              const char** err) {
    telestrat_regs_t r;
    uint32_t crc = 0;
#define _TELESTRAT_RDC(p, n)                                  \
    do {                                                      \
        _TELESTRAT_IO(p, n);                                  \
        crc = _telestrat_state_crc(crc, (p), (uint32_t)(n)); \
    } while (0)
    _TELESTRAT_RDC(&r, sizeof(r));
    _TELESTRAT_RDC(sys->ram, sizeof(sys->ram));
    _TELESTRAT_RDC(sys->bank_ram, sizeof(sys->bank_ram));
    _TELESTRAT_RDC(&sys->via, sizeof(sys->via));
    _TELESTRAT_RDC(&sys->via2, sizeof(sys->via2));
    _TELESTRAT_RDC(&sys->psg, sizeof(sys->psg));
    _TELESTRAT_RDC(&sys->kbd, sizeof(sys->kbd));
    _TELESTRAT_RDC(&sys->acia, sizeof(sys->acia));
    _telestrat_state_fdc_t f;
    _TELESTRAT_RDC(&f, sizeof(f));
    _telestrat_state_misc_t m;
    _TELESTRAT_RDC(&m, sizeof(m));
#undef _TELESTRAT_RDC
    uint32_t file_crc;
    _TELESTRAT_IO(&file_crc, sizeof(file_crc));
    if (file_crc != crc) {
        *err = "instantané corrompu (CRC)";
        return false;
    }
    if (!_telestrat_state_valid(sys, &f, &m)) {
        *err = "instantané invalide (valeurs hors bornes)";
        return false;
    }
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
    sys->strobe = m.strobe;
    sys->printer_ack = m.printer_ack;
    sys->printer_wait = m.printer_wait;
    sys->blink_counter = m.blink_counter;
    sys->pattr = m.pattr;
    sys->system_ticks = m.system_ticks;
    sys->psg_next = m.psg_next;
    sys->psg_next_sample = m.psg_next_sample;
    sys->sample_acc = m.sample_acc;
    sys->tape_due = m.system_ticks;
    sys->deferred = 0;  // l'instantané est pris à jour (_telestrat_catch_up à l'enregistrement)
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

// Seconde étape : la machine. Une erreur ici laisse une machine incohérente
// (la plate-forme la redémarre à froid) ; les rappels de l'hôte, eux, sont
// toujours ceux de ce programme, y compris après un fichier tronqué.
static inline bool telestrat_state_load_machine(telestrat_t* sys, telestrat_state_io_t io, void* ctx,
                                                const char** err) {
    _telestrat_state_hooks_t hooks;
    _telestrat_state_hooks_take(sys, &hooks);
    const bool ok = _telestrat_state_load_body(sys, io, ctx, err);
    _telestrat_state_hooks_put(sys, &hooks);
    return ok;
}

#undef _TELESTRAT_IO
#undef _TELESTRAT_IOC
#undef _TELESTRAT_DELAY_OK
