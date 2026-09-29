// test_telestrat.c — tests unitaires du système Telestrat (sans ROM réelle)
//
// Un petit programme 6502 en banque 7 exerce le décodage : commutation de
// banques par le VIA 2, protection des ROM, banques vides, registres du FDC
// et de l'ACIA.

#define _POSIX_C_SOURCE 200809L
#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chips/chips_common.h"
#include "chips/w65c02cpu.h"
#include "chips/mos6522via.h"
#include "chips/ay38910psg.h"
#include "chips/kbd.h"
#include "chips/clk.h"
#include "devices/wd1793.h"
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "devices/minitel_port.h"
#include "devices/hayes_line.h"
#include "systems/telestrat.h"
#include "telestrat_video.h"

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        checks++;                                \
        if (!(cond)) {                           \
            failures++;                          \
            printf("ÉCHEC %s:%d : ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
        }                                        \
    } while (0)

static telestrat_t sys;
static uint8_t rom7[0x4000], rom6[0x4000];

// Assemble un programme en $C000 de rom7 (vecteur RESET -> $C000)
static void load_program(const uint8_t* code, size_t n) {
    memset(rom7, 0xEA, sizeof(rom7));
    memcpy(rom7, code, n);
    rom7[0x3FFC] = 0x00;
    rom7[0x3FFD] = 0xC0;
}

static void boot(void) {
    telestrat_desc_t d = {0};
    d.banks[0].type = TELESTRAT_BANK_RAM;
    d.banks[1].type = TELESTRAT_BANK_RAM;
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom6};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom7};
    telestrat_init(&sys, &d);
    telestrat_reset(&sys);
}

static void run(int cycles) {
    for (int i = 0; i < cycles; i++) telestrat_tick(&sys);
}

static void test_reset_bank(void) {
    const uint8_t prog[] = {0x4C, 0x00, 0xC0};  // JMP $C000
    load_program(prog, sizeof(prog));
    boot();
    CHECK(sys.bank == 7, "banque au RESET = %d (attendu 7)", sys.bank);
    run(100);
    CHECK(sys.bank == 7, "banque après exécution = %d", sys.bank);
}

static void test_bank_switch_and_ram(void) {
    // Le programme se copie en $0400 puis bascule sur la banque 1 (RAM),
    // y écrit $5A en $C123, revient en banque 7 et range le résultat.
    const uint8_t stub[] = {
        0xA9, 0x07, 0x8D, 0x23, 0x03,  // LDA #$07 : STA $0323 (DDRA VIA 2 = %111)
        0xA9, 0x01, 0x8D, 0x21, 0x03,  // LDA #$01 : STA $0321 (banque 1)
        0xA9, 0x5A, 0x8D, 0x23, 0xC1,  // LDA #$5A : STA $C123
        0xAD, 0x23, 0xC1, 0x8D, 0x00, 0x10,  // LDA $C123 : STA $1000
        0xA9, 0x06, 0x8D, 0x21, 0x03,  // banque 6 (ROM)
        0xA9, 0xA5, 0x8D, 0x00, 0xC0,  // tentative d'écriture en ROM
        0xAD, 0x00, 0xC0, 0x8D, 0x01, 0x10,  // LDA $C000 : STA $1001
        0xA9, 0x03, 0x8D, 0x21, 0x03,  // banque 3 (vide)
        0xAD, 0x00, 0xC0, 0x8D, 0x02, 0x10,  // LDA $C000 : STA $1002
        0xA9, 0x07, 0x8D, 0x21, 0x03,  // retour banque 7
        0x4C, 0x00, 0x05,              // JMP $0500 (fin dans le code copié en RAM)
    };
    uint8_t prog[256];
    size_t n = 0;
    // Copie de stub en $0400 (X = 0..len-1) puis JMP $0400
    prog[n++] = 0xA2; prog[n++] = 0x00;                                   // LDX #0
    prog[n++] = 0xBD; prog[n++] = 0x20; prog[n++] = 0xC0;                 // LDA $C020,X
    prog[n++] = 0x9D; prog[n++] = 0x00; prog[n++] = 0x04;                 // STA $0400,X
    prog[n++] = 0xE8;                                                     // INX
    prog[n++] = 0xE0; prog[n++] = (uint8_t)sizeof(stub);                  // CPX #len
    prog[n++] = 0xD0; prog[n++] = 0xF5;                                   // BNE
    prog[n++] = 0x4C; prog[n++] = 0x00; prog[n++] = 0x04;                 // JMP $0400
    while (n < 0x20) prog[n++] = 0xEA;
    memcpy(prog + n, stub, sizeof(stub));
    n += sizeof(stub);
    load_program(prog, n);
    memset(rom6, 0x66, sizeof(rom6));
    boot();
    sys.ram[0x0500] = 0x4C;  // JMP $0500 : boucle d'arrêt
    sys.ram[0x0501] = 0x00;
    sys.ram[0x0502] = 0x05;
    run(5000);
    CHECK(sys.ram[0x1000] == 0x5A, "banque 1 RAM relue = %02X (attendu 5A)", sys.ram[0x1000]);
    CHECK(sys.ram[0x1001] == 0x66, "ROM banque 6 après écriture = %02X (attendu 66)", sys.ram[0x1001]);
    // Banque vide : bus flottant, instable d'une lecture à l'autre (TELEMON la
    // classe alors « invalide », $10)
    telestrat_select_bank(&sys, 3);
    int diff = 0;
    for (int i = 0; i < 16; i++) {
        uint8_t v1 = telestrat_peek(&sys, 0xFF00 + i);
        sys.system_ticks += 57;
        uint8_t v2 = telestrat_peek(&sys, 0xFF00 + i);
        diff += v1 != v2;
    }
    telestrat_select_bank(&sys, 7);
    CHECK(diff >= 12, "banque vide instable : %d lectures différentes sur 16", diff);
    CHECK(sys.bank == 7, "banque finale = %d (attendu 7)", sys.bank);
    CHECK(sys.bank_ram[1][0x0123] == 0x5A, "RAM de la banque 1 = %02X", sys.bank_ram[1][0x0123]);
    CHECK(rom6[0] == 0x66, "la ROM n'a pas été modifiée");
}

static void test_bank_ddr_keeps_inputs(void) {
    const uint8_t prog[] = {0x4C, 0x00, 0xC0};
    load_program(prog, sizeof(prog));
    boot();
    // DDRA = %001 : seul PA0 est piloté ; PA1-PA2 gardent la valeur (7 -> 6 ou 7)
    mos6522via_write(&sys.via2, 3, 0x01);
    mos6522via_write(&sys.via2, 1, 0x00);
    // appliquer comme le fait une écriture CPU
    sys.bank = 7;
    {
        uint8_t ddr = sys.via2.pa.ddr & 7;
        uint8_t bank = (sys.bank & ~ddr) | (sys.via2.pa.outr & ddr);
        telestrat_select_bank(&sys, bank);
    }
    CHECK(sys.bank == 6, "DDRA partiel : banque %d (attendu 6)", sys.bank);
}

// Fabrique une piste MFM de `nsec` secteurs de 256 octets (N = 1), numérotés
// 1..nsec, avec les CRC calculés comme le WD1793 ; data[i] = f(piste, face, secteur, i)
static uint8_t fdc_pattern(int trk, int side, int sec, int i) { return (uint8_t)(trk * 7 + side * 13 + sec * 31 + i); }

static uint8_t* make_disk(int sides, int tracks, int nsec, size_t* size) {
    *size = WD1793_HEADER_SIZE + (size_t)sides * tracks * WD1793_TRACK_SIZE;
    uint8_t* img = calloc(1, *size);
    memcpy(img, "MFM_DISK", 8);
    img[8] = (uint8_t)sides;
    img[12] = (uint8_t)tracks;
    img[16] = 1;
    for (int sd = 0; sd < sides; sd++) {
        for (int tr = 0; tr < tracks; tr++) {
            uint8_t* t = img + WD1793_HEADER_SIZE + ((size_t)sd * tracks + tr) * WD1793_TRACK_SIZE;
            memset(t, 0x4E, WD1793_TRACK_SIZE);
            int o = 40;
            for (int sec = 1; sec <= nsec; sec++) {
                memset(t + o, 0x00, 12); o += 12;
                uint16_t crc = 0xFFFF;
                for (int k = 0; k < 3; k++) { t[o++] = 0xA1; crc = wd1793_crc(crc, 0xA1); }
                uint8_t id[5] = {0xFE, (uint8_t)tr, (uint8_t)sd, (uint8_t)sec, 1};
                for (int k = 0; k < 5; k++) { t[o++] = id[k]; crc = wd1793_crc(crc, id[k]); }
                t[o++] = (uint8_t)(crc >> 8); t[o++] = (uint8_t)crc;
                o += 22;
                memset(t + o, 0x00, 12); o += 12;
                crc = 0xFFFF;
                for (int k = 0; k < 3; k++) { t[o++] = 0xA1; crc = wd1793_crc(crc, 0xA1); }
                t[o++] = 0xFB; crc = wd1793_crc(crc, 0xFB);
                for (int i = 0; i < 256; i++) { t[o] = fdc_pattern(tr, sd, sec, i); crc = wd1793_crc(crc, t[o++]); }
                t[o++] = (uint8_t)(crc >> 8); t[o++] = (uint8_t)crc;
                o += 24;
            }
        }
    }
    return img;
}

// Attend DRQ ou INTRQ (au plus `max` cycles)
static void fdc_wait(telestrat_fdc_t* f, int max) {
    for (int i = 0; i < max && !f->wd.drq && !f->wd.intrq; i++) telestrat_fdc_tick(f, 1);
}

static int fdc_read_bytes(telestrat_fdc_t* f, uint8_t* buf, int max) {
    int n = 0;
    for (;;) {
        fdc_wait(f, 2000);
        if (!f->wd.drq) break;
        uint8_t v;
        telestrat_fdc_read(f, 3, &v);
        if (n < max) buf[n] = v;
        n++;
    }
    return n;
}

static void test_fdc_no_disk(void) {
    telestrat_fdc_t f = {0};
    uint8_t v;
    telestrat_fdc_reset(&f);
    CHECK(telestrat_fdc_read(&f, 4, &v) && v == 0xFF, "$0314 au repos = %02X (attendu FF)", v);
    telestrat_fdc_write(&f, 4, TELESTRAT_FDC_CTRL_INTENA);
    telestrat_fdc_write(&f, 0, 0x08);  // RESTORE
    fdc_wait(&f, 100);
    CHECK(f.wd.intrq, "INTRQ après RESTORE");
    CHECK(telestrat_fdc_irq(&f), "IRQ si INTENA");
    CHECK(telestrat_fdc_read(&f, 4, &v) && v == 0x7F, "$0314 avec INTRQ = %02X (attendu 7F)", v);
    CHECK(telestrat_fdc_read(&f, 0, &v) && (v & WD1793_ST_NOT_READY) && (v & WD1793_ST_TRACK0),
          "statut type I sans disque = %02X", v);
    CHECK(!f.wd.intrq, "la lecture du statut efface INTRQ");
    telestrat_fdc_write(&f, 0, 0x80);  // READ SECTOR sans disquette
    telestrat_fdc_read(&f, 0, &v);
    CHECK(v & WD1793_ST_NOT_READY, "READ SECTOR sans disque : non prêt (%02X)", v);
    CHECK(!telestrat_fdc_read(&f, 5, &v), "$0315 n'appartient pas au FDC");
}

static void test_fdc_disk(void) {
    telestrat_fdc_t f = {0};
    size_t size;
    uint8_t* img = make_disk(2, 4, 16, &size);
    uint8_t v, buf[1024];
    telestrat_fdc_reset(&f);
    CHECK(!wd1793_insert(&f.wd, 0, img + 1, size - 1, false), "en-tête invalide refusé");
    CHECK(wd1793_insert(&f.wd, 0, img, size, false), "insertion d'une image MFM_DISK");

    // Type I : SEEK 3, STEP OUT (u), RESTORE
    telestrat_fdc_write(&f, 3, 3);
    telestrat_fdc_write(&f, 0, 0x1C);  // SEEK avec vérification
    fdc_wait(&f, 100);
    telestrat_fdc_read(&f, 0, &v);
    CHECK(f.wd.track == 3 && f.wd.disk[0].head == 3, "SEEK piste 3 (TR=%d tête=%d)", f.wd.track, f.wd.disk[0].head);
    CHECK(!(v & (WD1793_ST_SEEK_ERR | WD1793_ST_NOT_READY | WD1793_ST_TRACK0)), "statut SEEK = %02X", v);
    telestrat_fdc_write(&f, 0, 0x70);  // STEP OUT avec mise à jour de TR
    fdc_wait(&f, 100);
    CHECK(f.wd.track == 2 && f.wd.disk[0].head == 2, "STEP OUT : TR=%d", f.wd.track);

    // READ SECTOR piste 2, face 1, secteur 5
    telestrat_fdc_write(&f, 4, TELESTRAT_FDC_CTRL_SIDE);
    telestrat_fdc_write(&f, 2, 5);
    telestrat_fdc_write(&f, 0, 0x80);
    int n = fdc_read_bytes(&f, buf, sizeof(buf));
    int ok = n == 256;
    for (int i = 0; ok && i < 256; i++) ok = buf[i] == fdc_pattern(2, 1, 5, i);
    CHECK(ok, "READ SECTOR : %d octets, contenu %s", n, ok ? "exact" : "faux");
    CHECK(telestrat_fdc_read(&f, 8, &v) && v == 0xFF, "$0318 : plus de DRQ (%02X)", v);
    fdc_wait(&f, 200);
    telestrat_fdc_read(&f, 0, &v);
    CHECK(v == 0x00, "statut fin de lecture = %02X", v);

    // Multi-secteurs depuis le 15 : 15 et 16 puis fin de piste
    telestrat_fdc_write(&f, 2, 15);
    telestrat_fdc_write(&f, 0, 0x90);
    n = fdc_read_bytes(&f, buf, sizeof(buf));
    CHECK(n == 512 && buf[256] == fdc_pattern(2, 1, 16, 0), "lecture multi-secteurs : %d octets", n);

    // Secteur absent
    telestrat_fdc_write(&f, 2, 40);
    telestrat_fdc_write(&f, 0, 0x80);
    fdc_wait(&f, 100);
    telestrat_fdc_read(&f, 0, &v);
    CHECK(v & WD1793_ST_RNF, "secteur 40 introuvable : statut %02X", v);

    // READ ADDRESS : 6 octets (piste, face, secteur, taille, CRC)
    telestrat_fdc_write(&f, 0, 0xC0);
    n = fdc_read_bytes(&f, buf, sizeof(buf));
    CHECK(n == 6 && buf[0] == 2 && buf[1] == 1 && buf[3] == 1, "READ ADDRESS : %d octets, piste %d face %d", n,
          buf[0], buf[1]);
    CHECK(f.wd.sector == 2, "READ ADDRESS : piste dans le registre de secteur (%d)", f.wd.sector);

    // WRITE SECTOR : réécrire le contenu identique ne change pas l'image (CRC exacts)
    uint8_t* copy = malloc(size);
    memcpy(copy, img, size);
    telestrat_fdc_write(&f, 2, 7);
    telestrat_fdc_write(&f, 0, 0xA0);
    for (int i = 0; i < 256; i++) {
        fdc_wait(&f, 2000);
        telestrat_fdc_write(&f, 3, fdc_pattern(2, 1, 7, i));
    }
    fdc_wait(&f, 200);
    CHECK(memcmp(copy, img, size) == 0, "réécriture identique : image inchangée (CRC exacts)");
    CHECK(f.wd.disk[0].modified, "image marquée modifiée");

    // WRITE SECTOR avec d'autres données, relues ensuite
    telestrat_fdc_write(&f, 2, 7);
    telestrat_fdc_write(&f, 0, 0xA0);
    for (int i = 0; i < 256; i++) {
        fdc_wait(&f, 2000);
        telestrat_fdc_write(&f, 3, (uint8_t)(255 - i));
    }
    fdc_wait(&f, 200);
    telestrat_fdc_read(&f, 0, &v);
    telestrat_fdc_write(&f, 2, 7);
    telestrat_fdc_write(&f, 0, 0x80);
    n = fdc_read_bytes(&f, buf, sizeof(buf));
    CHECK(n == 256 && buf[0] == 255 && buf[255] == 0, "relecture après écriture");

    // Protection en écriture
    f.wd.disk[0].write_protect = true;
    telestrat_fdc_write(&f, 0, 0xA0);
    fdc_wait(&f, 100);
    telestrat_fdc_read(&f, 0, &v);
    CHECK((v & WD1793_ST_WPROT) && !f.wd.drq, "écriture refusée sur disque protégé (%02X)", v);
    f.wd.disk[0].write_protect = false;

    // Interruption forcée pendant une lecture
    telestrat_fdc_write(&f, 2, 1);
    telestrat_fdc_write(&f, 0, 0x80);
    fdc_wait(&f, 200);
    telestrat_fdc_write(&f, 0, 0xD8);
    CHECK(!f.wd.drq && f.wd.intrq && !(f.wd.status & WD1793_ST_BUSY), "FORCE INTERRUPT arrête la lecture");

    // Lecteur B vide : non prêt
    telestrat_fdc_write(&f, 4, 0x20);
    telestrat_fdc_write(&f, 0, 0x80);
    telestrat_fdc_read(&f, 0, &v);
    CHECK(v & WD1793_ST_NOT_READY, "lecteur B vide : non prêt (%02X)", v);
    free(copy);
    free(img);
}

static void test_fdc_write_track(void) {
    // Formate la piste 1 face 0 comme le ferait un DOS, puis relit un secteur
    telestrat_fdc_t f = {0};
    size_t size;
    uint8_t* img = make_disk(1, 2, 16, &size);
    uint8_t buf[512];
    telestrat_fdc_reset(&f);
    wd1793_insert(&f.wd, 0, img, size, false);
    telestrat_fdc_write(&f, 3, 1);
    telestrat_fdc_write(&f, 0, 0x10);
    fdc_wait(&f, 100);
    uint8_t stream[WD1793_TRACK_SIZE];
    int o = 0;
    for (int k = 0; k < 40; k++) stream[o++] = 0x4E;
    for (int sec = 1; sec <= 3; sec++) {
        for (int k = 0; k < 12; k++) stream[o++] = 0x00;
        for (int k = 0; k < 3; k++) stream[o++] = 0xF5;
        stream[o++] = 0xFE; stream[o++] = 1; stream[o++] = 0; stream[o++] = (uint8_t)(10 + sec); stream[o++] = 1;
        stream[o++] = 0xF7;
        for (int k = 0; k < 22; k++) stream[o++] = 0x4E;
        for (int k = 0; k < 12; k++) stream[o++] = 0x00;
        for (int k = 0; k < 3; k++) stream[o++] = 0xF5;
        stream[o++] = 0xFB;
        for (int i = 0; i < 256; i++) stream[o++] = 0xE5;
        stream[o++] = 0xF7;
        for (int k = 0; k < 24; k++) stream[o++] = 0x4E;
    }
    while (o < WD1793_TRACK_SIZE) stream[o++] = 0x4E;
    telestrat_fdc_write(&f, 0, 0xF0);
    int written = 0;
    for (int i = 0; i < WD1793_TRACK_SIZE; i++) {
        fdc_wait(&f, 2000);
        if (!f.wd.drq) break;
        telestrat_fdc_write(&f, 3, stream[i]);
        written++;
    }
    fdc_wait(&f, 200);
    CHECK(f.wd.intrq, "WRITE TRACK terminé (%d octets envoyés)", written);
    telestrat_fdc_write(&f, 2, 12);
    telestrat_fdc_write(&f, 0, 0x80);
    int n = fdc_read_bytes(&f, buf, sizeof(buf));
    CHECK(n == 256 && buf[0] == 0xE5 && buf[255] == 0xE5, "secteur formaté relu (%d octets)", n);
    // CRC de l'ID écrit par $F7 = CRC calculé sur A1 A1 A1 FE 01 00 0C 01
    uint16_t crc = 0xFFFF;
    const uint8_t idb[8] = {0xA1, 0xA1, 0xA1, 0xFE, 1, 0, 12, 1};
    for (int k = 0; k < 8; k++) crc = wd1793_crc(crc, idb[k]);
    uint8_t* id = f.wd.sec_id[f.wd.sec_index];
    CHECK(id[5] == (crc >> 8) && id[6] == (crc & 0xFF), "CRC de l'ID formaté = %02X%02X (attendu %04X)", id[5], id[6],
          crc);
    free(img);
}

// Mode flux : l'image est lue et écrite par rappels, piste par piste
typedef struct {
    uint8_t* img;
    size_t size;
    int reads, writes;
} stream_ctx_t;

static bool stream_read(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
    stream_ctx_t* c = ctx;
    if (off + len > c->size) return false;
    memcpy(buf, c->img + off, len);
    c->reads++;
    return true;
}

static bool stream_write(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
    stream_ctx_t* c = ctx;
    if (off + len > c->size) return false;
    memcpy(c->img + off, buf, len);
    c->writes++;
    return true;
}

static void test_fdc_streamed(void) {
    telestrat_fdc_t f = {0};
    size_t size;
    uint8_t* img = make_disk(2, 4, 16, &size);
    stream_ctx_t c = {img, size, 0, 0};
    uint8_t buf[512], v;
    telestrat_fdc_reset(&f);
    CHECK(wd1793_insert_streamed(&f.wd, 0, size, stream_read, stream_write, &c), "insertion en flux");
    telestrat_fdc_write(&f, 3, 3);
    telestrat_fdc_write(&f, 0, 0x10);
    fdc_wait(&f, 100);
    telestrat_fdc_write(&f, 2, 9);
    telestrat_fdc_write(&f, 0, 0x80);
    int n = fdc_read_bytes(&f, buf, sizeof(buf));
    CHECK(n == 256 && buf[10] == fdc_pattern(3, 0, 9, 10), "lecture en flux (%d octets)", n);
    int reads = c.reads;
    telestrat_fdc_write(&f, 2, 10);
    telestrat_fdc_write(&f, 0, 0x80);
    fdc_read_bytes(&f, buf, sizeof(buf));
    CHECK(c.reads == reads, "même piste : pas de relecture (%d lectures)", c.reads);
    // Écriture : réécrite dans le fichier à la fin de la commande
    telestrat_fdc_write(&f, 2, 4);
    telestrat_fdc_write(&f, 0, 0xA0);
    for (int i = 0; i < 256; i++) {
        fdc_wait(&f, 2000);
        telestrat_fdc_write(&f, 3, 0x5A);
    }
    fdc_wait(&f, 200);
    telestrat_fdc_read(&f, 0, &v);
    CHECK(c.writes == 1, "piste réécrite une fois (%d)", c.writes);
    // Relue par un contrôleur neuf en mode mémoire
    telestrat_fdc_t g = {0};
    telestrat_fdc_reset(&g);
    wd1793_insert(&g.wd, 0, img, size, true);
    telestrat_fdc_write(&g, 3, 3);
    telestrat_fdc_write(&g, 0, 0x10);
    fdc_wait(&g, 100);
    telestrat_fdc_write(&g, 2, 4);
    telestrat_fdc_write(&g, 0, 0x80);
    n = fdc_read_bytes(&g, buf, sizeof(buf));
    CHECK(n == 256 && buf[0] == 0x5A && buf[255] == 0x5A, "écriture en flux relue en mémoire");
    // Protection : sans rappel d'écriture
    wd1793_insert_streamed(&f.wd, 1, size, stream_read, NULL, &c);
    telestrat_fdc_write(&f, 4, 0x20);
    telestrat_fdc_write(&f, 0, 0xA0);
    telestrat_fdc_read(&f, 0, &v);
    CHECK(v & WD1793_ST_WPROT, "flux sans écriture : protégé (%02X)", v);
    free(img);
}

// --- ACIA : émission, réception, interruptions -------------------------------
static uint8_t acia_out[64];
static int acia_out_n;
static uint8_t acia_in[64];
static int acia_in_n, acia_in_pos;

static void acia_tx(uint8_t d, void* u) {
    (void)u;
    if (acia_out_n < 64) acia_out[acia_out_n++] = d;
}

static int acia_rx(void* u) {
    (void)u;
    return acia_in_pos < acia_in_n ? acia_in[acia_in_pos++] : -1;
}

static void test_acia_serial(void) {
    mos6551acia_t a = {0};
    a.tx_cb = acia_tx;
    a.rx_cb = acia_rx;
    a.cpu_freq = 1000000;
    mos6551acia_reset(&a);
    acia_out_n = acia_in_n = acia_in_pos = 0;
    // Prise Minitel de TELEMON : 1200 bauds, 7 bits, parité paire, 1 stop
    mos6551acia_write(&a, 3, 0x38);
    mos6551acia_write(&a, 2, 0x67);
    CHECK(mos6551acia_char_cycles(&a) == 8333, "1200 bauds 7E1 : %d cycles par caractère",
          (int)mos6551acia_char_cycles(&a));
    CHECK(mos6551acia_irq(&a), "IRQ d'émission à l'écriture de la commande, registre vide");
    uint8_t st = mos6551acia_read(&a, 1);
    CHECK((st & 0x90) == 0x90 && !mos6551acia_irq(&a), "état $%02X, IRQ effacée par la lecture", st);
    // Double tampon : le premier octet passe aussitôt dans le registre à décalage
    mos6551acia_write(&a, 0, 0xC1);
    CHECK((a.status & MOS6551_ST_TXEMPTY) && mos6551acia_irq(&a), "1er octet : registre de nouveau vide, IRQ");
    mos6551acia_read(&a, 1);
    mos6551acia_write(&a, 0, 0x42);
    CHECK(!(a.status & MOS6551_ST_TXEMPTY) && !mos6551acia_irq(&a), "2e octet en attente");
    for (int i = 0; i < 8333 / 4 + 1; i++) mos6551acia_tick(&a, 4);
    CHECK(acia_out_n == 1 && acia_out[0] == 0x41, "1er octet émis sur 7 bits ($%02X) après 8,3 ms",
          acia_out_n ? acia_out[0] : 0);
    CHECK((a.status & MOS6551_ST_TXEMPTY) && mos6551acia_irq(&a), "2e octet chargé : IRQ d'émission");
    for (int i = 0; i < 8333 / 4 + 1; i++) mos6551acia_tick(&a, 4);
    CHECK(acia_out_n == 2 && acia_out[1] == 0x42, "2e octet émis");
    // Réception : IRQ de réception interdite ($67), bit 7 de l'état levé quand même
    acia_in[acia_in_n++] = 0x13;
    acia_in[acia_in_n++] = 0x53;
    mos6551acia_read(&a, 1);
    mos6551acia_tick(&a, 4);
    mos6551acia_poll_rx(&a);
    st = mos6551acia_read(&a, 1);
    CHECK((st & 0x88) == 0x88 && !mos6551acia_irq(&a), "octet reçu : état $%02X, broche IRQ inactive", st);
    CHECK(mos6551acia_read(&a, 0) == 0x13, "donnée reçue $13");
    mos6551acia_tick(&a, 4);
    mos6551acia_poll_rx(&a);
    CHECK(!(a.status & MOS6551_ST_RXFULL), "octet suivant retenu jusqu'à la fin du caractère");
    for (int i = 0; i < 8333 / 4 + 1; i++) {
        mos6551acia_tick(&a, 4);
        mos6551acia_poll_rx(&a);
    }
    CHECK((a.status & MOS6551_ST_RXFULL) && a.rx == 0x53, "2e octet reçu au rythme du débit");
    // IRQ de réception autorisée ($65) : broche active jusqu'à la lecture de la donnée
    mos6551acia_write(&a, 2, 0x65);
    CHECK(mos6551acia_irq(&a), "IRQ de réception (commande $65)");
    mos6551acia_read(&a, 1);
    CHECK(mos6551acia_irq(&a), "la lecture de l'état ne suffit pas : donnée non lue");
    mos6551acia_read(&a, 0);
    CHECK(!mos6551acia_irq(&a), "IRQ tombée après lecture de la donnée");
    // Récepteur inactif (DTR = 0) : rien n'entre
    mos6551acia_write(&a, 2, 0x62);
    acia_in[acia_in_n++] = 0x55;
    for (int i = 0; i < 3000; i++) {
        mos6551acia_tick(&a, 4);
        mos6551acia_poll_rx(&a);
    }
    CHECK(!(a.status & MOS6551_ST_RXFULL), "DTR inactif : pas de réception");
}

// --- Prise Minitel ------------------------------------------------------------
typedef struct {
    bool ringing, answered, dialed, online, hung_up;
    uint8_t sent[64];
    int n_sent;
    int rx[8], rx_n, rx_pos;
} fake_line_t;

static bool fl_dial(void* c) { ((fake_line_t*)c)->dialed = ((fake_line_t*)c)->online = true; return true; }
static void fl_answer(void* c) {
    fake_line_t* l = c;
    l->answered = l->online = true;
    l->ringing = false;
}
static void fl_hangup(void* c) {
    fake_line_t* l = c;
    l->hung_up = true;
    l->online = false;
}
static bool fl_incoming(void* c) { return ((fake_line_t*)c)->ringing; }
static bool fl_carrier(void* c) { return ((fake_line_t*)c)->online; }
static int fl_recv(void* c) {
    fake_line_t* l = c;
    return l->rx_pos < l->rx_n ? l->rx[l->rx_pos++] : -1;
}
static void fl_send(void* c, uint8_t d) {
    fake_line_t* l = c;
    if (l->n_sent < 64) l->sent[l->n_sent++] = d;
}

static void minitel_send_str(minitel_port_t* p, const uint8_t* b, int n) {
    for (int i = 0; i < n; i++) minitel_port_from_telestrat(p, b[i]);
}

static void test_minitel_port(void) {
    fake_line_t fl = {0};
    minitel_line_t line = {fl_dial, fl_answer, fl_hangup, fl_incoming, fl_carrier, fl_recv, fl_send, &fl};
    minitel_port_t p;
    minitel_port_init(&p, &line);

    // Sonnerie : rafales à 50 Hz pendant 1,5 s, silence ensuite (période 5 s)
    fl.ringing = true;
    int edges = 0, silent_edges = 0;
    bool prev = false;
    for (int ms = 0; ms < 5000; ms++) {
        bool lvl = minitel_port_tick(&p, 1000);
        if (prev && !lvl) {
            if (ms < 1500) edges++;
            else silent_edges++;
        }
        prev = lvl;
    }
    CHECK(edges >= 70 && edges <= 76 && silent_edges == 0, "sonnerie : %d fronts en 1,5 s, %d dans le silence", edges,
          silent_edges);

    // XLIGNE (STUM 1B) : OPPO -> SEP $50 ; CONNEXION -> SEP $59, décroche ;
    // porteuse après 3 s de 390 Hz (mode opposé) -> SEP $53 vers prise et modem
    const uint8_t xligne[] = {0x1B, 0x39, 0x6F, 0x1B, 0x39, 0x68};
    minitel_send_str(&p, xligne, 6);
    CHECK(fl.answered && p.opposition && p.state == MINITEL_CONNECTING, "XLIGNE : appel décroché (état %d)", p.state);
    int r1 = minitel_port_to_telestrat(&p), r2 = minitel_port_to_telestrat(&p);
    int r3 = minitel_port_to_telestrat(&p), r4 = minitel_port_to_telestrat(&p);
    CHECK(r1 == 0x13 && r2 == 0x50 && r3 == 0x13 && r4 == 0x59, "OPPO puis CONNEXION : %02X %02X %02X %02X", r1, r2,
          r3, r4);
    CHECK(minitel_port_to_telestrat(&p) == -1, "pas de SEP $53 avant la porteuse");
    CHECK(!minitel_port_tick(&p, 1000), "plus de sonnerie une fois décroché");
    for (int ms = 0; ms < 2900; ms++) minitel_port_tick(&p, 1000);
    CHECK(p.state == MINITEL_CONNECTING, "mode opposé : pas connecté avant 3 s de 390 Hz");
    for (int ms = 0; ms < 200; ms++) minitel_port_tick(&p, 1000);
    r1 = minitel_port_to_telestrat(&p);
    r2 = minitel_port_to_telestrat(&p);
    CHECK(r1 == 0x13 && r2 == 0x53 && p.state == MINITEL_ONLINE, "connexion signalée : %02X %02X", r1, r2);
    CHECK(fl.n_sent == 2 && fl.sent[0] == 0x13 && fl.sent[1] == 0x53, "SEP $53 aussi vers le modem");

    // En ligne : les données passent, les séquences Videotex ESC aussi, pas les PRO
    const uint8_t page[] = {0x0C, 'A', 0x1B, 0x42, 'B', 0x1B, 0x3A, 0x69, 0x43, 'C'};
    minitel_send_str(&p, page, sizeof(page));
    CHECK(fl.n_sent == 8 && fl.sent[4] == 0x1B && fl.sent[5] == 0x42 && fl.sent[7] == 'C',
          "données transmises (%d octets), PRO2 filtrée", fl.n_sent);
    fl.rx[fl.rx_n++] = 0x13;
    fl.rx[fl.rx_n++] = 0x41;
    CHECK(minitel_port_to_telestrat(&p) == 0x13 && minitel_port_to_telestrat(&p) == 0x41, "ENVOI du correspondant reçu");

    // Le correspondant raccroche : SEP $59 puis SEP $53
    fl.online = false;
    minitel_port_tick(&p, 1000);
    r1 = minitel_port_to_telestrat(&p);
    r2 = minitel_port_to_telestrat(&p);
    r3 = minitel_port_to_telestrat(&p);
    r4 = minitel_port_to_telestrat(&p);
    CHECK(r1 == 0x13 && r2 == 0x59 && r3 == 0x13 && r4 == 0x53 && p.state == MINITEL_IDLE && !p.opposition,
          "déconnexion signalée : %02X %02X %02X %02X", r1, r2, r3, r4);

    // Minitel en terminal : CONNEXION sans appel entrant -> SEP $59, appel sortant ;
    // porteuse en mode standard après 1,7 s ; PRO1 DECONNEXION -> SEP $59 $53
    minitel_send_str(&p, xligne + 3, 3);
    CHECK(fl.dialed && p.state == MINITEL_CONNECTING && minitel_port_to_telestrat(&p) == 0x13 &&
              minitel_port_to_telestrat(&p) == 0x59,
          "CONNEXION sans appel entrant : SEP $59, appel sortant");
    for (int ms = 0; ms < 1800; ms++) minitel_port_tick(&p, 1000);
    CHECK(p.state == MINITEL_ONLINE, "mode standard : connecté après 1,7 s");
    while (minitel_port_to_telestrat(&p) >= 0) {
    }
    const uint8_t decon[] = {0x1B, 0x39, 0x67};
    minitel_send_str(&p, decon, 3);
    r1 = minitel_port_to_telestrat(&p);
    r2 = minitel_port_to_telestrat(&p);
    r3 = minitel_port_to_telestrat(&p);
    r4 = minitel_port_to_telestrat(&p);
    CHECK(fl.hung_up && p.state == MINITEL_IDLE && r2 == 0x59 && r4 == 0x53, "XDECON : raccroché, SEP $59 $53");

    // Échec : pas de porteuse en 40 s -> second SEP $59
    fl.online = false;
    fl.dialed = false;
    minitel_send_str(&p, xligne + 3, 3);
    fl.online = false;
    minitel_port_to_telestrat(&p);
    minitel_port_to_telestrat(&p);
    for (int s40 = 0; s40 < 41; s40++) minitel_port_tick(&p, 1000000);
    r1 = minitel_port_to_telestrat(&p);
    r2 = minitel_port_to_telestrat(&p);
    CHECK(p.state == MINITEL_IDLE && r1 == 0x13 && r2 == 0x59, "échec de connexion : second SEP $59");
}

// --- Modem Hayes (faux modem) --------------------------------------------------
static char modem_out[512];
static int modem_out_n;

static void modem_write(void* ctx, const uint8_t* d, uint32_t n) {
    (void)ctx;
    for (uint32_t i = 0; i < n && modem_out_n < 511; i++) modem_out[modem_out_n++] = (char)d[i];
    modem_out[modem_out_n] = 0;
}

static void modem_says(hayes_line_t* h, const char* s) {
    while (*s) hayes_line_feed(h, (uint8_t)*s++);
}

static void test_hayes_line(void) {
    hayes_line_t h;
    modem_out_n = 0;
    hayes_line_init(&h, modem_write, NULL, "go.minipavi.fr:516", 3615);
    minitel_line_t l = hayes_line_line(&h);
    CHECK(strcmp(modem_out, "ATE0V1\rATS0=0\rAT$SP=3615\r") == 0, "initialisation : %s", modem_out);
    modem_says(&h, "\r\nOK\r\n\r\nOK\r\n\r\nRING\r\n");
    CHECK(l.incoming(l.ctx), "RING : appel entrant");
    hayes_line_tick(&h, 9000000);
    CHECK(!l.incoming(l.ctx), "plus de RING depuis 9 s : l'appel a cessé");
    modem_says(&h, "RING\r\n");
    modem_out_n = 0;
    l.answer(l.ctx);
    CHECK(strcmp(modem_out, "ATA\r") == 0 && !l.carrier(l.ctx), "décroché : ATA");
    modem_says(&h, "\r\nCONNECT 1200\r\n");
    CHECK(l.carrier(l.ctx), "CONNECT : en ligne");
    modem_says(&h, "\x13\x41");
    CHECK(l.recv(l.ctx) == 0x13 && l.recv(l.ctx) == 0x41 && l.recv(l.ctx) == -1, "données du correspondant");
    modem_out_n = 0;
    l.send(l.ctx, 'X');
    CHECK(modem_out_n == 1 && modem_out[0] == 'X', "données vers le correspondant");
    modem_says(&h, "\r\nNO CARRIER\r\n");
    CHECK(!l.carrier(l.ctx), "NO CARRIER en ligne : porteuse perdue");
    // Appel sortant puis raccrochage par +++ / ATH avec gardes d'une seconde
    modem_out_n = 0;
    CHECK(l.dial(l.ctx) && strcmp(modem_out, "ATDgo.minipavi.fr:516\r") == 0, "appel sortant : %s", modem_out);
    modem_says(&h, "CONNECT\r\n");
    CHECK(l.carrier(l.ctx), "appel sortant établi");
    modem_out_n = 0;
    l.hangup(l.ctx);
    hayes_line_tick(&h, 500000);
    CHECK(modem_out_n == 0, "garde avant +++");
    hayes_line_tick(&h, 700000);
    CHECK(strcmp(modem_out, "+++") == 0, "+++ après la garde");
    hayes_line_tick(&h, 1200000);
    CHECK(strcmp(modem_out, "+++ATH\r") == 0 && !l.carrier(l.ctx), "ATH après la garde : %s", modem_out);
    // Appel sortant refusé
    modem_says(&h, "OK\r\n");
    l.dial(l.ctx);
    modem_says(&h, "\r\nBUSY\r\n");
    CHECK(!l.carrier(l.ctx) && h.state == HAYES_COMMAND, "BUSY : retour en mode commande");
    hayes_line_t h2;
    hayes_line_init(&h2, modem_write, NULL, "", 0);
    minitel_line_t l2 = hayes_line_line(&h2);
    CHECK(!l2.dial(l2.ctx), "sans numéro : pas d'appel sortant");
}

// --- Rendu de l'écran : identique au rendu d'origine (oric.h) -----------------
// Copie exacte de l'ancien telestrat_screen_update (oric_screen_update de
// reload), comme référence
static void ref_render(const uint8_t* ram, uint8_t* pattr_io, bool blink_state, uint8_t* fb) {
    uint8_t pattr = *pattr_io;
    for (int y = 0; y < TELESTRAT_SCREEN_HEIGHT; y++) {
        uint8_t lattr = 0, fgcol = 7, bgcol = 0;
        uint8_t* p = &fb[y * (TELESTRAT_SCREEN_WIDTH / 2)];
        for (int x = 0; x < 40; x++) {
            uint8_t ch, pat;
            if ((pattr & 0x04) && y < 200) {
                ch = pat = ram[0xA000 + y * 40 + x];
            } else {
                ch = ram[0xBB80 + (y >> 3) * 40 + x];
                int off = (lattr & 0x02 ? y >> 1 : y) & 7;
                const uint8_t* base;
                if (pattr & 0x04) {
                    base = ram + ((lattr & 0x01) ? 0x9C00 : 0x9800);
                } else {
                    base = ram + ((lattr & 0x01) ? 0xB800 : 0xB400);
                }
                pat = base[((ch & 0x7F) << 3) | off];
            }
            if (!(ch & 0x60)) {
                pat = 0x00;
                switch (ch & 0x18) {
                    case 0x00: fgcol = ch & 7; break;
                    case 0x08: lattr = ch & 7; break;
                    case 0x10: bgcol = ch & 7; break;
                    case 0x18: pattr = ch & 7; break;
                }
            }
            uint8_t cf = fgcol, cb = bgcol;
            if (ch & 0x80) {
                cb ^= 0x07;
                cf ^= 0x07;
            }
            if ((lattr & 0x04) && blink_state) cf = cb;
            *p = (pat & 0x20 ? cf : cb) << 4;
            *p++ |= (pat & 0x10 ? cf : cb);
            *p = (pat & 0x08 ? cf : cb) << 4;
            *p++ |= (pat & 0x04 ? cf : cb);
            *p = (pat & 0x02 ? cf : cb) << 4;
            *p++ |= (pat & 0x01 ? cf : cb);
        }
    }
    *pattr_io = pattr;
}

static void test_screen_render(void) {
    static uint8_t fb[TELESTRAT_FRAMEBUFFER_SIZE];
    const uint8_t prog[] = {0x4C, 0x00, 0xC0};
    load_program(prog, sizeof(prog));
    boot();
    uint32_t rnd = 12345;
    int bad = 0, frames = 0;
    for (int it = 0; it < 300; it++) {
        // Écran aléatoire ; attributs fréquents une fois sur deux
        for (int a = 0x9800; a < 0xC000; a++) {
            rnd = rnd * 1103515245u + 12345u;
            uint8_t v = (uint8_t)(rnd >> 16);
            if ((it & 1) && (v & 3) == 0) v &= 0x9F;  // attribut série (bits 5-6 à 0)
            sys.ram[a] = v;
        }
        sys.pattr = (uint8_t)(it * 7) & 7;
        for (int b = 0; b < 2; b++) {
            // blink_counter : bit 5 = état du clignotement ; forcer le rendu
            sys.blink_counter = b ? 0x20 : 0x00;
            sys.screen_dirty = true;
            uint8_t pattr = sys.pattr;
            ref_render(sys.ram, &pattr, sys.blink_counter & 0x20, fb);
            telestrat_screen_update(&sys);
            frames++;
            if (memcmp(fb, sys.fb, sizeof(fb)) != 0 || pattr != sys.pattr) bad++;
        }
    }
    CHECK(bad == 0, "rendu identique au rendu d'origine : %d écrans différents sur %d", bad, frames);
}

// --- Plans 1 bpp de l'affichage DVI (firmware) -------------------------------
static void test_video_planes(void) {
    telestrat_video_init();
    static uint32_t pl[3][960 / 32];
    uint8_t line[TELESTRAT_VIDEO_BYTES_PER_LINE];
    uint32_t rnd = 777;
    int bad = 0, margins = 0;
    const unsigned offsets[] = {120, 40, 0, 7};
    for (int it = 0; it < 200; it++) {
        unsigned x0 = offsets[it & 3];
        for (int i = 0; i < TELESTRAT_VIDEO_BYTES_PER_LINE; i++) {
            rnd = rnd * 1103515245u + 12345u;
            line[i] = (uint8_t)(rnd >> 16) & 0x77;
        }
        memset(pl, 0xAA, sizeof(pl));  // marges : doivent rester intactes hors des mots de bord
        telestrat_video_line(line, pl[0], pl[1], pl[2], x0);
        for (int x = 0; x < TELESTRAT_VIDEO_PIXELS; x++) {
            int px = x / 3;
            int col = (px & 1) ? (line[px >> 1] & 15) : (line[px >> 1] >> 4);
            unsigned bit = x0 + (unsigned)x;
            for (int p = 0; p < 3; p++) {
                int got = (pl[p][bit >> 5] >> (bit & 31)) & 1;
                if (got != ((col >> p) & 1)) bad++;
            }
        }
        // Mots entièrement hors de la ligne : inchangés
        for (unsigned wd = 0; wd < 960 / 32; wd++) {
            unsigned lo = wd * 32, hi = lo + 31;
            if (hi < x0 || lo >= x0 + TELESTRAT_VIDEO_PIXELS) {
                for (int p = 0; p < 3; p++) margins += pl[p][wd] != 0xAAAAAAAAu;
            }
        }
    }
    CHECK(bad == 0, "plans 1 bpp : %d pixels faux", bad);
    CHECK(margins == 0, "plans 1 bpp : %d mots de marge modifiés", margins);
}

static void test_acia(void) {
    mos6551acia_t a = {0};
    mos6551acia_reset(&a);
    CHECK(mos6551acia_read(&a, 1) == MOS6551_ST_TXEMPTY, "statut ACIA au RESET");
    CHECK(mos6551acia_read(&a, 2) == MOS6551_CMD_IRQDIS, "commande ACIA au RESET");
    mos6551acia_write(&a, 2, 0xEB);
    mos6551acia_write(&a, 1, 0x00);  // RESET logiciel
    CHECK(mos6551acia_read(&a, 2) == 0xE2, "RESET logiciel garde la parité : %02X", a.command);
    CHECK(!mos6551acia_irq(&a), "pas d'IRQ ACIA");
}

int main(void) {
    test_reset_bank();
    test_bank_switch_and_ram();
    test_bank_ddr_keeps_inputs();
    test_fdc_no_disk();
    test_fdc_disk();
    test_fdc_write_track();
    test_fdc_streamed();
    test_acia();
    test_screen_render();
    test_video_planes();
    test_acia_serial();
    test_minitel_port();
    test_hayes_line();
    printf("test_telestrat : %d/%d vérifications réussies\n", checks - failures, checks);
    return failures ? 1 : 0;
}
