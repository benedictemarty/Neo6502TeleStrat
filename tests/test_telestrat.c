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
#include "devices/modem_mux.h"
#include "devices/drive_set.h"
#include "devices/byte_fifo.h"
#include "osd/osd_menu.h"
#include "osd/osd_config.h"
#include "systems/telestrat.h"
#include "osd/rom_pool.h"
#include "roms/telestrat_roms.h"
#include "osd/rom_builtin.h"
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

// Cartouche changée à chaud par le menu, puis contenu d'origine
static void test_bank_hot_swap(void) {
    const uint8_t prog[] = {0x4C, 0x00, 0xC0};
    load_program(prog, sizeof(prog));
    boot();
    static uint8_t cart[0x4000];
    memset(cart, 0x42, sizeof(cart));
    telestrat_set_bank_rom(&sys, 5, cart);
    telestrat_select_bank(&sys, 5);
    CHECK(telestrat_peek(&sys, 0xC000) == 0x42 && sys.bank_wr[5] == NULL, "cartouche en banque 5, lecture seule");
    telestrat_set_bank_rom(&sys, 6, cart);
    telestrat_restore_bank(&sys, 6);
    CHECK(sys.bank_rd[6] == rom6, "banque 6 : HYPER-BASIC d'origine rendu");
    telestrat_set_bank_rom(&sys, 1, cart);
    telestrat_restore_bank(&sys, 1);
    CHECK(sys.bank_type[1] == TELESTRAT_BANK_RAM && sys.bank_wr[1] != NULL, "banque 1 : RAM d'origine rendue");
    telestrat_restore_bank(&sys, 5);
    CHECK(sys.rd_cur == NULL, "banque courante vidée : bus flottant");
    telestrat_select_bank(&sys, 7);
}

// Emplacements de banque : ROM intégrées copiées, cartouches de la clé
static void test_rom_pool(void) {
    static uint8_t slots[3][OSD_BANK_BYTES];
    static uint8_t flash7[OSD_BANK_BYTES], flash6[OSD_BANK_BYTES], img[0x2000];
    rom_pool_t p;
    rom_pool_init(&p, slots, 3);  // 2 ROM intégrées + 1 supplémentaire
    const uint8_t prog[] = {0x4C, 0x00, 0xC0};
    load_program(prog, sizeof(prog));
    memcpy(flash7, rom7, sizeof(flash7));
    memset(flash6, 0x66, sizeof(flash6));
    telestrat_desc_t d = {0};
    d.banks[0].type = TELESTRAT_BANK_RAM;
    d.banks[1].type = TELESTRAT_BANK_RAM;
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&p, 6, flash6)};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom_pool_builtin(&p, 7, flash7)};
    telestrat_init(&sys, &d);
    telestrat_reset(&sys);
    CHECK(sys.bank_rd[6] == slots[0] && slots[0][5] == 0x66 && rom_pool_free(&p) == 1, "ROM intégrée copiée en RAM");
    run(50);
    CHECK(sys.bank == 7, "le 65C02 tourne depuis l'emplacement de TELEMON");
    const char* err = "";
    for (int i = 0; i < 0x2000; i++) img[i] = (uint8_t)(i ^ 0x5A);
    uint8_t* dst = rom_pool_claim(&p, &sys, 6, sizeof(img), &err);
    CHECK(dst == slots[0], "cartouche en banque 6 : emplacement de HYPER-BASIC réutilisé");
    memcpy(dst, img, sizeof(img));
    rom_pool_commit(&p, &sys, 6, sizeof(img), "orix.rom");
    CHECK(slots[0][0x2003] == (3 ^ 0x5A) && sys.bank_rd[6] == slots[0] && !strcmp(p.name[6], "orix.rom"),
          "8 Ko répétés, banque branchée");
    dst = rom_pool_claim(&p, &sys, 5, sizeof(img), &err);
    CHECK(dst == slots[2] && rom_pool_free(&p) == 0, "banque 5 vide : emplacement supplémentaire");
    rom_pool_commit(&p, &sys, 5, sizeof(img), "forth.rom");
    CHECK(!rom_pool_claim(&p, &sys, 1, sizeof(img), &err) && strstr(err, "plus de place"),
          "banque 1 : plus de place (%s)", err);
    CHECK(sys.bank_type[1] == TELESTRAT_BANK_RAM, "refus : banque 1 intacte (RAM)");
    CHECK(!rom_pool_claim(&p, &sys, 4, 1000, &err) && strstr(err, "taille"), "taille refusée avant la place");
    rom_pool_restore(&p, &sys, 6);
    CHECK(slots[0][5] == 0x66 && sys.bank_rd[6] == slots[0] && !p.name[6][0], "banque 6 : HYPER-BASIC recopié de la flash");
    rom_pool_restore(&p, &sys, 5);
    CHECK(rom_pool_free(&p) == 1 && sys.bank_rd[5] == NULL, "banque 5 : emplacement libéré, banque vide");
    dst = rom_pool_claim(&p, &sys, 1, sizeof(img), &err);
    rom_pool_abort(&p, &sys, 1);
    CHECK(rom_pool_free(&p) == 1 && sys.bank_type[1] == TELESTRAT_BANK_RAM, "lecture échouée : banque 1 rendue à sa RAM");
}

// ROM intégrées : cartouche STRATORIC complète (banques 7, 6, 5)
static void test_rom_builtin(void) {
    static uint8_t slots[3][OSD_BANK_BYTES];
    rom_pool_t p;
    rom_pool_init(&p, slots, 3);
    boot();
    const char* err = "";
    const rom_builtin_t* s = rom_builtin_find("@stratoric");
    CHECK(s && rom_builtin_find("ORIC BASIC 1.1 (Atmos)") == &rom_builtins[1], "recherche par identifiant et libellé");
    CHECK(rom_pool_load_builtin(&p, &sys, 7, s, &err), "STRATORIC chargée (%s)", err);
    CHECK(sys.bank_rd[7] && !memcmp(sys.bank_rd[7], telestrat_stratoric, 16) && !memcmp(sys.bank_rd[6], telestrat_atmos, 16) &&
              !memcmp(sys.bank_rd[5], telestrat_basic10, 16),
          "banques 7, 6, 5 : STRATORIC, BASIC 1.1, BASIC 1.0");
    CHECK(!strcmp(rom_builtin_label(p.name[7]), "STRATORIC 4.0") && !strcmp(p.name[5], "@basic10"),
          "noms : panneau court, identifiant pour TELESTRA.CFG");
    rom_pool_t q;
    rom_pool_init(&q, slots, 2);
    boot();
    CHECK(!rom_pool_load_builtin(&q, &sys, 7, s, &err) && strstr(err, "plus de place"), "deux emplacements : plus de place");
    telestrat_select_bank(&sys, 7);
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

// Format programmé, pour régler l'UART de la prise RS232 du Neo6502
static void test_acia_format(void) {
    mos6551acia_t a = {0};
    a.control = 0x38;  // prise Minitel de TELEMON : 1200 bauds, 7 bits, 1 stop
    a.command = 0x67;  // parité paire
    mos6551acia_format_t f = mos6551acia_format(&a);
    CHECK(f.baud == 1200 && f.data_bits == 7 && f.parity == 2 && f.stop_bits == 1, "format Minitel : 1200 7E1");
    a.control = 0x1E;  // prise RS232 de TELEMON : 9600 bauds, 8 bits, 1 stop
    a.command = 0x0B;  // sans parité
    f = mos6551acia_format(&a);
    CHECK(f.baud == 9600 && f.data_bits == 8 && f.parity == 0 && f.stop_bits == 1, "format RS232 : 9600 8N1");
    a.control = 0x9F;  // 19 200 bauds, 8 bits, 2 stop
    f = mos6551acia_format(&a);
    CHECK(f.baud == 19200 && f.stop_bits == 2, "19200 8N2");
    a.command = 0x20;  // parité impaire : 8 bits + parité -> un seul stop
    f = mos6551acia_format(&a);
    CHECK(f.parity == 1 && f.stop_bits == 1, "8 bits + parité : un seul stop");
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

// --- Un modem pour les deux prises (modem_mux.h) --------------------------------
static void test_modem_mux(void) {
    hayes_line_t h;
    modem_mux_t m;
    modem_mux_init(&m, &h);
    minitel_line_t l = hayes_line_line(&h);
    modem_out_n = 0;
    modem_mux_select(&m, true);  // RS232 au démarrage (PA4 = 1 au RESET)
    modem_mux_attach(&m, modem_write, NULL, "go.minipavi.fr:516", 3615);
    CHECK(modem_out_n == 0, "modem branché, RS232 : pas d'initialisation Hayes");
    modem_mux_select(&m, false);
    CHECK(strcmp(modem_out, "ATE0V1\rATS0=0\rAT$SP=3615\r") == 0, "retour Minitel : initialisation (%s)", modem_out);
    modem_mux_feed(&m, 'R');
    CHECK(modem_mux_rs232_recv(&m) == -1, "Minitel : rien vers la RS232");
    const char* ring = "\r\nRING\r\n";
    for (const char* c = ring; *c; c++) modem_mux_feed(&m, (uint8_t)*c);
    CHECK(l.incoming(l.ctx), "Minitel : RING vu par hayes_line");
    // Bascule sur la RS232 : octets bruts dans les deux sens
    modem_out_n = 0;
    modem_mux_select(&m, true);
    CHECK(!l.incoming(l.ctx) && !l.carrier(l.ctx), "RS232 : ligne Minitel au repos");
    modem_mux_rs232_send(&m, 'A');
    modem_mux_rs232_send(&m, 'T');
    CHECK(modem_out_n == 2 && modem_out[0] == 'A' && modem_out[1] == 'T', "RS232 : AT tapé par le Telestrat");
    const char* ok = "OK\r\nCONNECT\r\n";
    for (const char* c = ok; *c; c++) modem_mux_feed(&m, (uint8_t)*c);
    CHECK(modem_mux_rs232_recv(&m) == 'O' && modem_mux_rs232_recv(&m) == 'K', "RS232 : réponses du modem transmises");
    CHECK(!l.carrier(l.ctx), "RS232 : CONNECT non interprété par hayes_line");
    modem_mux_tick(&m, 3000000);
    CHECK(modem_out_n == 2, "RS232 : pas de temporisation Hayes");
    // Retour sur la prise Minitel : file RS232 vidée, modem réinitialisé
    modem_out_n = 0;
    modem_mux_select(&m, false);
    CHECK(modem_mux_rs232_recv(&m) == -1 && strncmp(modem_out, "ATE0V1", 6) == 0, "retour Minitel : réinitialisé");
    modem_out_n = 0;
    modem_mux_rs232_send(&m, 'X');
    CHECK(modem_out_n == 0, "Minitel : la RS232 n'écrit pas au modem");
    CHECK(l.dial(l.ctx) && strcmp(modem_out, "ATDgo.minipavi.fr:516\r") == 0, "Minitel : appel sortant (%s)", modem_out);
    modem_mux_detach(&m);
    modem_mux_select(&m, true);
    modem_out_n = 0;
    modem_mux_rs232_send(&m, 'Y');
    CHECK(modem_out_n == 0, "modem débranché : rien n'est écrit");
}

// --- Images de la clé dans les lecteurs A à D (drive_set.h) ---------------------
static void test_drive_set(void) {
    drive_set_t s;
    drive_set_init(&s);
    const char* none[4] = {NULL, NULL, NULL, NULL};
    drive_set_assign(&s, none);
    CHECK(s.slot[0] == -1, "clé sans image : lecteur A vide");
    drive_set_add(&s, "STRATSED.DSK");
    drive_set_add(&s, "TELEDIS.DSK");
    drive_set_add(&s, "JEUX.DSK");
    drive_set_assign(&s, none);
    CHECK(s.slot[0] == 0 && s.slot[1] == -1 && s.slot[2] == -1 && s.slot[3] == -1, "sans réglage : 1re image en A seule");
    const char* cfg[4] = {"", "jeux.dsk", "INCONNU.DSK", "Teledis.dsk"};
    drive_set_assign(&s, cfg);
    CHECK(s.slot[0] == 0 && s.slot[1] == 2 && s.slot[2] == -1 && s.slot[3] == 1,
          "b= et d= (casse ignorée), nom inconnu ignoré : %d %d %d %d", s.slot[0], s.slot[1], s.slot[2], s.slot[3]);
    const char* cfg2[4] = {"JEUX.DSK", "JEUX.DSK", NULL, NULL};
    drive_set_assign(&s, cfg2);
    CHECK(s.slot[0] == 2 && s.slot[1] == -1, "même image demandée deux fois : seul le 1er lecteur l'a");
}

// --- File d'octets (imprimante vers la clé) --------------------------------------------
static void test_byte_fifo(void) {
    static byte_fifo_t f;
    byte_fifo_init(&f);
    for (int i = 0; i < BYTE_FIFO_SIZE - 10; i++) byte_fifo_push(&f, (uint8_t)i);
    uint32_t n;
    const uint8_t* p = byte_fifo_peek(&f, &n);
    CHECK(n == BYTE_FIFO_SIZE - 10 && p[0] == 0 && p[5] == 5, "bloc contigu (%u)", n);
    byte_fifo_drop(&f, n - 3);
    for (int i = 0; i < 20; i++) byte_fifo_push(&f, (uint8_t)(0xA0 + i));
    p = byte_fifo_peek(&f, &n);
    CHECK(n == 13 && byte_fifo_count(&f) == 23, "bloc jusqu'à la fin du tampon, puis le reste (%u)", n);
    byte_fifo_drop(&f, n);
    p = byte_fifo_peek(&f, &n);
    CHECK(n == 10 && p[0] == 0xAA, "retour au début du tampon");
    byte_fifo_drop(&f, n);
    for (int i = 0; i < BYTE_FIFO_SIZE + 5; i++) byte_fifo_push(&f, 1);
    CHECK(byte_fifo_count(&f) == BYTE_FIFO_SIZE && f.lost == 5, "pleine : 5 octets perdus, comptés");
}

// --- Cassette (oric_tape.h) -------------------------------------------------------
static const uint8_t* tap_data;
static bool tap_read(void* ctx, uint32_t off, uint8_t* buf, uint32_t len) {
    (void)ctx;
    memcpy(buf, tap_data + off, len);
    return true;
}

// Durées des alternances d'un octet : 2 par bit, à partir du front montant
static int tape_byte_halves(oric_tape_t* t, int* halves) {
    int n = 0;
    for (int i = 0; i < 28; i++) {
        halves[n++] = oric_tape_next(t);
        oric_tape_toggle(t);
    }
    return n;
}

static void test_oric_tape(void) {
    CHECK(_oric_tape_frame(0x00) == (1 | 1 << 10 | 7 << 11), "trame de $00 : parité 1 (zéro 1, pair)");
    CHECK(_oric_tape_frame(0x01) == (1 | 1 << 2 | 7 << 11), "trame de $01 : parité 0");
    static const uint8_t img[] = {0x16, 0x16, 0x16, 0x24, 0, 0, 0, 0, 0x05, 0x02, 0x05, 0x01, 0, 'A', 0, 0xA5, 0x5A};
    tap_data = img;
    oric_tape_t t;
    oric_tape_init(&t);
    oric_tape_insert(&t, sizeof(img), tap_read, NULL);
    CHECK(!oric_tape_running(&t), "moteur arrêté : le signal ne bouge pas");
    oric_tape_set_motor(&t, true);
    CHECK(oric_tape_running(&t) && t.extra_sync == ORIC_TAPE_SYNC_MORE && t.header_end == 15,
          "moteur sur une synchro : 80 octets de plus, fin d'en-tête en 15 (%u)", t.header_end);
    CHECK(oric_tape_next(&t) == 4 && oric_tape_toggle(&t) == 1, "premier front montant aussitôt");
    // Le front montant a commencé le premier bit : on remonte à l'alternance basse
    int h[28];
    int first = oric_tape_next(&t);
    oric_tape_toggle(&t);  // fin du bit 0 (basse)
    CHECK(first == ORIC_TAPE_SHORT, "bit 0 de la trame : 1 (208 cycles)");
    tape_byte_halves(&t, h);
    // h[0] : alternance basse du bit 0 ; puis bit 1 (synchro 0), bits de $16
    CHECK(h[0] == ORIC_TAPE_SHORT && h[1] == ORIC_TAPE_LONG && h[2] == ORIC_TAPE_LONG,
          "bit 1 : synchro 0 (deux alternances de 416 cycles)");
    CHECK(h[3] == ORIC_TAPE_LONG && h[5] == ORIC_TAPE_SHORT && h[7] == ORIC_TAPE_SHORT,
          "$16 : bit 0 = 0, bits 1 et 2 = 1");
    // Avance jusqu'à la fin de l'en-tête : 80 + 15 octets
    int guard = 0;
    while (t.phase == ORIC_TAPE_DATA && guard++ < 100000) oric_tape_toggle(&t);
    CHECK(t.phase == ORIC_TAPE_GAP_RUN && t.pos == 15 && t.extra_sync == 0, "fin de l'en-tête : silence (pos %u)", t.pos);
    int gap = 0;
    while (t.phase == ORIC_TAPE_GAP_RUN) {
        gap += oric_tape_next(&t);
        oric_tape_toggle(&t);
    }
    CHECK(gap >= ORIC_TAPE_GAP && gap < ORIC_TAPE_GAP + 2 * ORIC_TAPE_SHORT && t.phase == ORIC_TAPE_DATA,
          "silence d'environ 1281 cycles (%d)", gap);
    while (t.phase == ORIC_TAPE_DATA && guard++ < 200000) oric_tape_toggle(&t);
    CHECK(t.phase == ORIC_TAPE_TAIL && t.pos == sizeof(img) && oric_tape_percent(&t) == 100, "fin des données");
    oric_tape_toggle(&t);
    oric_tape_toggle(&t);
    CHECK(!oric_tape_running(&t), "fin de bande : deux alternances puis plus rien");
    // Arrêt du moteur au milieu d'un octet : reprise à l'octet suivant
    oric_tape_rewind(&t);
    t.pos = 15;
    oric_tape_set_motor(&t, false);
    oric_tape_set_motor(&t, true);
    for (int i = 0; i < 9; i++) oric_tape_toggle(&t);
    oric_tape_set_motor(&t, false);
    CHECK(t.pos == 16 && !oric_tape_running(&t), "moteur arrêté en cours d'octet : octet suivant (%u)", t.pos);
    oric_tape_eject(&t);
    oric_tape_set_motor(&t, true);
    CHECK(!oric_tape_running(&t) && !t.inserted, "éjectée : rien ne bouge");
}

// --- Enregistreur (oric_tape_rec.h) : CSAVE ------------------------------------------
static uint8_t rec_buf[256];
static int rec_len, rec_opens, rec_closes;
static char rec_name[24];
static bool rec_open_cb(void* c, const char* n) {
    (void)c;
    snprintf(rec_name, sizeof(rec_name), "%s", n);
    rec_len = 0;
    rec_opens++;
    return true;
}
static void rec_write_cb(void* c, const uint8_t* d, uint32_t n) {
    (void)c;
    for (uint32_t i = 0; i < n && rec_len < 256; i++) rec_buf[rec_len++] = d[i];
}
static void rec_close_cb(void* c) {
    (void)c;
    rec_closes++;
}

static uint32_t rec_t;
// Octet émis comme la ROM ($E65E) : 0, 8 bits, parité, 1, 1, 1 ; une période
// de 432 cycles pour 1, 640 pour 0 (front montant au début)
static void rec_emit(oric_tape_rec_t* r, uint8_t b, bool bad_parity) {
    int ones = 0;
    for (int i = 0; i < 8; i++) ones += (b >> i) & 1;
    int bits[13] = {0};
    for (int i = 0; i < 8; i++) bits[1 + i] = (b >> i) & 1;
    bits[9] = ((ones & 1) ^ 1) ^ (bad_parity ? 1 : 0);
    bits[10] = bits[11] = bits[12] = 1;
    for (int i = 0; i < 13; i++) {
        const uint32_t period = bits[i] ? 432 : 640;
        oric_tape_rec_level(r, 1, rec_t);
        oric_tape_rec_level(r, 0, rec_t + period / 2);
        rec_t += period;
    }
}

static void test_oric_tape_rec(void) {
    const oric_tape_rec_out_t out = {rec_open_cb, rec_write_cb, rec_close_cb, NULL};
    oric_tape_rec_t r;
    oric_tape_rec_init(&r, &out);
    rec_opens = rec_closes = 0;
    rec_t = 1000;
    for (int i = 0; i < 20; i++) rec_emit(&r, 0x16, false);
    rec_emit(&r, 0x24, false);
    const uint8_t hdr[9] = {0, 0, 0, 0, 0x05, 0x03, 0x05, 0x01, 0};  // $0501-$0503 : 3 octets
    for (int i = 0; i < 9; i++) rec_emit(&r, hdr[i], false);
    const char* name = "jeu 1!";
    for (const char* c = name; *c; c++) rec_emit(&r, (uint8_t)*c, false);
    rec_emit(&r, 0, false);
    CHECK(rec_opens == 1 && !strcmp(rec_name, "JEU1.TAP") && oric_tape_rec_active(&r),
          "en-tête : fichier JEU1.TAP ouvert (%s)", rec_name);
    rec_emit(&r, 0xA5, false);
    rec_emit(&r, 0x5A, true);  // parité fausse : perdu
    rec_emit(&r, 0x5A, false);
    CHECK(r.bad == 1 && oric_tape_rec_active(&r), "parité fausse : trame rejetée");
    rec_emit(&r, 0x00, false);
    const uint8_t expect[] = {0x16, 0x16, 0x16, 0x24, 0, 0, 0, 0, 0x05, 0x03, 0x05, 0x01, 0, 'j', 'e', 'u', ' ', '1', '!',
                              0,    0xA5, 0x5A, 0x00};
    CHECK(rec_closes == 1 && !oric_tape_rec_active(&r) && rec_len == (int)sizeof(expect) &&
              !memcmp(rec_buf, expect, sizeof(expect)),
          "3 octets de données : .tap complet et fermé (%d octets)", rec_len);
    // Nom vide, moteur arrêté en cours de données
    for (int i = 0; i < 5; i++) rec_emit(&r, 0x16, false);
    rec_emit(&r, 0x24, false);
    const uint8_t hdr2[9] = {0, 0, 0, 0, 0x06, 0x00, 0x05, 0x01, 0};
    for (int i = 0; i < 9; i++) rec_emit(&r, hdr2[i], false);
    rec_emit(&r, 0, false);
    rec_emit(&r, 0x11, false);
    CHECK(!strcmp(rec_name, "SANSNOM.TAP") && oric_tape_rec_active(&r), "nom vide : SANSNOM.TAP");
    oric_tape_rec_motor_off(&r);
    CHECK(rec_closes == 2 && !oric_tape_rec_active(&r), "moteur arrêté : fichier fermé tel quel");
    rec_t += 100000;  // silence : trame abandonnée, pas d'octet parasite
    rec_emit(&r, 0x16, false);
    CHECK(rec_opens == 2 && r.phase == ORIC_REC_SYNC, "après un silence : de nouveau en attente d'un en-tête");
}

// --- Menu (OSD) : texte, rendu d'une ligne, navigation ---------------------------
static osd_surface_t osd_s;
static osd_menu_t osd_m;

static void test_osd_render(void) {
    const char* p = "é—x\xC3";
    CHECK(osd_next_char(&p) == 0xE9 && osd_next_char(&p) == OSD_EMDASH && osd_next_char(&p) == 'x',
          "UTF-8 : é en Latin-1, tiret cadratin, ASCII");
    CHECK(osd_strlen("Clé") == 3, "longueur en cellules d'une chaîne accentuée");
    uint32_t r[OSD_WIDTH / 32], g[OSD_WIDTH / 32], b[OSD_WIDTH / 32];
    osd_clear(&osd_s, OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_putc(&osd_s, 0, 0, 'A', OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_putc(&osd_s, 0, 1, 'A', OSD_ATTR(OSD_RED, OSD_BLACK));
    osd_render_line(&osd_s, 1, r, g, b);
    const uint32_t a1 = osd_font['A'][1];
    CHECK(r[0] == (a1 | a1 << 8) && g[0] == a1 && b[0] == a1, "encre blanche puis rouge (ligne 1 de A)");
    // Fond tramé bleu : un pixel sur deux, alterné d'une ligne à l'autre
    osd_fill(&osd_s, 1, 0, 1, OSD_COLS, OSD_ATTR(OSD_WHITE, OSD_BLUE | OSD_DITHER));
    osd_render_line(&osd_s, 8, r, g, b);
    uint32_t r2[OSD_WIDTH / 32], g2[OSD_WIDTH / 32], b2[OSD_WIDTH / 32];
    osd_render_line(&osd_s, 9, r2, g2, b2);
    CHECK(b[0] == 0x55555555u && b2[0] == 0xAAAAAAAAu && r[0] == 0 && g2[5] == 0, "fond tramé : damier bleu");
    // Grandes lettres : chaque pixel doublé sur deux cellules
    osd_clear(&osd_s, OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_puts_big(&osd_s, 0, 0, "T", OSD_ATTR(OSD_WHITE, OSD_BLACK));
    osd_render_line(&osd_s, 1, r, g, b);
    const uint16_t wide = _osd_widen(osd_font['T'][1]);
    CHECK((r[0] & 0xFFFF) == wide, "grande lettre : T élargi (%04X / %04X)", (unsigned)(r[0] & 0xFFFF), wide);
}

// Cassette et ROM intégrées dans le menu ; bandeau
static void test_osd_tape_menu(void) {
    osd_menu_init(&osd_m);
    const char* names[4] = {"JEUX.DSK", "aigle.tap", "orix.rom", "zorgon.tap"};
    const uint8_t kinds[4] = {OSD_FILE_DSK, OSD_FILE_TAP, OSD_FILE_ROM, OSD_FILE_TAP};
    for (int i = 0; i < 4; i++) {
        strcpy(osd_m.files[i].name, names[i]);
        osd_m.files[i].kind = kinds[i];
    }
    osd_m.nfiles = 4;
    osd_m.builtin[0] = "ORIC BASIC 1.1 (Atmos)";
    strcpy(osd_m.tape, "zorgon.tap");
    osd_m.cursor = OSD_ITEM_DRIVE0 + 3;
    osd_menu_key(&osd_m, OSD_KEY_DOWN);
    CHECK(osd_m.cursor == OSD_ITEM_TAPE, "sous le lecteur D : la cassette");
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(osd_m.page == OSD_PAGE_BROWSE && osd_m.browse_count == 2 && osd_m.browse_cursor == 2,
          "sélecteur des seules .tap, curseur sur la cassette en place");
    osd_menu_key(&osd_m, OSD_KEY_UP);
    osd_action_t a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_TAPE_INSERT && a.file == 1, "aigle.tap insérée (%d %d)", a.type, a.file);
    a = osd_menu_key(&osd_m, OSD_KEY_DEL);
    CHECK(a.type == OSD_ACT_TAPE_EJECT, "Suppr sur la cassette : éjecter");
    osd_menu_key(&osd_m, OSD_KEY_RIGHT);
    CHECK(osd_m.cursor == OSD_ITEM_BANK7 + 4, "droite depuis la cassette : banque 3");
    osd_m.cursor = OSD_ITEM_BANK7;  // banque 7
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(osd_m.browse_count == 2 && osd_m.browse_list[0] == -2, "banque : ROM intégrée d'abord, puis les .rom");
    osd_menu_key(&osd_m, OSD_KEY_DOWN);
    a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_LOAD_BUILTIN && a.target == 7 && a.file == 0, "ROM Atmos en banque 7");
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    osd_menu_key(&osd_m, 'O');
    CHECK(osd_m.browse_cursor == 1, "lettre O : la ROM intégrée (ORIC…) avant orix.rom");
    osd_menu_key(&osd_m, 'O');
    a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_LOAD_ROM && a.file == 2, "lettre O encore : orix.rom");
    osd_m.tape_percent = 50;
    osd_m.tape_motor = true;
    osd_menu_draw(&osd_m, &osd_s);
    CHECK(osd_s.ch[15][6] == OSD_TAPE_L && osd_s.ch[15][41] == OSD_TRI_R && osd_s.ch[15][49] == OSD_FULL &&
              osd_s.ch[15][54] == OSD_SHADE,
          "ligne Cassette : icône, moteur, barre à moitié");
    static osd_row_t row;
    osd_tape_banner(&row, "Lecture", "AIGLE.TAP", 40);
    CHECK(row.ch[18] == OSD_TAPE_L && !memcmp(&row.ch[30], "AIGLE.TAP", 9) && row.ch[66] == OSD_FULL &&
              row.ch[66 + 29] == OSD_SHADE,
          "bandeau : icône, nom, barre");
    uint32_t r[30], g[30], b[30];
    osd_render_cells(row.ch, row.attr, row.big, 0, 0, r, g, b);
    CHECK(b[0] == 0x55555555u && r[0] == 0, "bandeau : fond bleu tramé");
}

static void test_osd_config(void) {
    static uint8_t bank[OSD_BANK_BYTES], img[0x2000];
    for (int i = 0; i < 0x2000; i++) img[i] = (uint8_t)i;
    CHECK(osd_rom_fill(bank, img, 0x2000) && bank[0x2005] == 5 && bank[0x3FFF] == 0xFF && bank[0x0005] == 5,
          ".rom de 8 Ko répétée dans la banque");
    CHECK(!osd_rom_fill(bank, img, 0x1234) && !osd_rom_size_ok(0x8000), "taille refusée (0x1234, 32 Ko)");
    CHECK(osd_config_value("bank5=ORIX.ROM", "bank5") && !strcmp(osd_config_value("bank5=ORIX.ROM", "bank5"), "ORIX.ROM") &&
              !osd_config_value("bank51=X", "bank5") && !osd_config_value("a=X", "b"),
          "lecture d'une clé de réglage");
    const char* old = "dial=go.minipavi.fr:516\r\na=VIEUX.DSK\nlisten=3615\nbank3=OLD.ROM\n\nrs232=uext";
    const char* drives[4] = {"STRATSED.DSK", NULL, "", "Jeux 1987.dsk"};
    const char* banks[8] = {NULL, NULL, NULL, NULL, NULL, "orix.rom", NULL, NULL};
    char out[512];
    size_t n = osd_config_merge(old, drives, banks, out, sizeof(out));
    CHECK(n == strlen(out) && !strcmp(out, "dial=go.minipavi.fr:516\nlisten=3615\nrs232=uext\na=STRATSED.DSK\n"
                                           "d=Jeux 1987.dsk\nbank5=orix.rom\n"),
          "TELESTRA.CFG fusionné :\n%s", out);
    CHECK(osd_config_merge(old, drives, banks, out, 20) == 0, "tampon trop petit : 0");
}

static void osd_sample(void) {
    osd_menu_init(&osd_m);
    strcpy(osd_m.drive[0], "STRATSED.DSK");
    strcpy(osd_m.bank[6], "HYPER-BASIC");
    osd_m.bank_kind[6] = OSD_BANK_ROM;
    const char* names[5] = {"JEUX.DSK", "forth.rom", "STRATSED.DSK", "Demo.dsk", "orix.rom"};
    for (int i = 0; i < 5; i++) {
        strcpy(osd_m.files[i].name, names[i]);
        osd_m.files[i].kind = strstr(names[i], ".rom") ? OSD_FILE_ROM : OSD_FILE_DSK;
    }
    osd_m.nfiles = 5;
    osd_m.usb_present = true;
}

static void test_osd_menu(void) {
    osd_sample();
    osd_action_t a = osd_menu_key(&osd_m, OSD_KEY_ESC);
    CHECK(osd_m.cursor == OSD_ITEM_RESUME && a.type == OSD_ACT_RESUME, "ouverture sur « Reprendre », Échap reprend");
    osd_menu_key(&osd_m, OSD_KEY_HOME);
    a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_NONE && osd_m.page == OSD_PAGE_BROWSE && osd_m.browse_count == 3,
          "lecteur A : sélecteur des seules .dsk (%d)", osd_m.browse_count);
    CHECK(osd_m.browse_cursor == 2, "curseur sur l'image en place (STRATSED.DSK)");
    osd_menu_key(&osd_m, 'd');
    a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_INSERT && a.target == 0 && a.file == 3 && osd_m.page == OSD_PAGE_MAIN,
          "lettre d puis Entrée : Demo.dsk dans A (%d %d %d)", a.type, a.target, a.file);
    osd_menu_key(&osd_m, OSD_KEY_DOWN);
    a = osd_menu_key(&osd_m, OSD_KEY_DEL);
    CHECK(a.type == OSD_ACT_EJECT && a.target == 1, "Suppr sur B : éjecter");
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    osd_menu_key(&osd_m, OSD_KEY_HOME);
    a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_EJECT && a.target == 1, "sélecteur : 1re ligne = éjecter");
    osd_menu_key(&osd_m, OSD_KEY_RIGHT);
    CHECK(osd_m.cursor == OSD_ITEM_BANK7 + 1, "droite depuis B : banque 6 (%d)", osd_m.cursor);
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(osd_m.browse_count == 2, "banque : sélecteur des seules .rom");
    osd_menu_key(&osd_m, OSD_KEY_UP);
    a = osd_menu_key(&osd_m, OSD_KEY_ENTER);
    CHECK(a.type == OSD_ACT_LOAD_ROM && a.target == 6 && a.file == 4, "haut (bouclage) puis Entrée : orix.rom en banque 6");
    a = osd_menu_key(&osd_m, OSD_KEY_DEL);
    CHECK(a.type == OSD_ACT_RESTORE && a.target == 6, "Suppr sur une banque : contenu d'origine");
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    a = osd_menu_key(&osd_m, OSD_KEY_ESC);
    CHECK(a.type == OSD_ACT_NONE && osd_m.page == OSD_PAGE_MAIN, "Échap dans le sélecteur : retour sans action");
    osd_m.cursor = OSD_ITEM_RESET;
    CHECK(osd_menu_key(&osd_m, OSD_KEY_ENTER).type == OSD_ACT_RESET, "bouton RESET");
    osd_menu_key(&osd_m, OSD_KEY_RIGHT);
    CHECK(osd_menu_key(&osd_m, OSD_KEY_ENTER).type == OSD_ACT_SAVE, "bouton Enregistrer");
    osd_menu_key(&osd_m, OSD_KEY_LEFT);
    osd_menu_key(&osd_m, OSD_KEY_LEFT);
    CHECK(osd_m.cursor == OSD_ITEM_RESET, "gauche dans les boutons");
    // Sélecteur long : défilement
    for (int i = 0; i < 40; i++) {
        snprintf(osd_m.files[i].name, OSD_NAME_LEN, "IMG%02d.DSK", i);
        osd_m.files[i].kind = OSD_FILE_DSK;
    }
    osd_m.nfiles = 40;
    osd_m.cursor = 0;
    osd_menu_key(&osd_m, OSD_KEY_ENTER);
    osd_menu_key(&osd_m, OSD_KEY_END);
    CHECK(osd_m.browse_cursor == 40 && osd_m.browse_scroll == 41 - OSD_BROWSE_VISIBLE, "Fin : défilement (%d)",
          osd_m.browse_scroll);
    osd_menu_draw(&osd_m, &osd_s);
    CHECK(osd_s.ch[7 + OSD_BROWSE_VISIBLE][OSD_COLS / 2] != 0, "dessin du sélecteur défilé");
    // Image déjà dans le lecteur B : marquée « en B » dans le sélecteur de A
    strcpy(osd_m.drive[1], "IMG00.DSK");
    osd_menu_key(&osd_m, OSD_KEY_HOME);
    osd_menu_draw(&osd_m, &osd_s);
    CHECK(!memcmp(&osd_s.ch[9][22 + 76 - 18], "en B", 4), "image déjà en B signalée");
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
    test_bank_hot_swap();
    test_rom_pool();
    test_rom_builtin();
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
    test_acia_format();
    test_minitel_port();
    test_hayes_line();
    test_modem_mux();
    test_drive_set();
    test_byte_fifo();
    test_oric_tape();
    test_oric_tape_rec();
    test_osd_render();
    test_osd_menu();
    test_osd_config();
    test_osd_tape_menu();
    printf("test_telestrat : %d/%d vérifications réussies\n", checks - failures, checks);
    return failures ? 1 : 0;
}
