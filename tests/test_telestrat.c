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
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "systems/telestrat.h"

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
    CHECK(sys.ram[0x1002] == 0xFF, "banque vide = %02X (attendu FF)", sys.ram[0x1002]);
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

static void test_fdc(void) {
    telestrat_fdc_t f;
    uint8_t v;
    telestrat_fdc_reset(&f);
    CHECK(telestrat_fdc_read(&f, 4, &v) && v == 0xFF, "$0314 au repos = %02X (attendu FF)", v);
    telestrat_fdc_write(&f, 4, TELESTRAT_FDC_CTRL_INTENA);
    telestrat_fdc_write(&f, 0, 0x08);  // RESTORE
    CHECK(f.intrq, "INTRQ après RESTORE");
    CHECK(telestrat_fdc_irq(&f), "IRQ si INTENA");
    CHECK(telestrat_fdc_read(&f, 4, &v) && v == 0x7F, "$0314 avec INTRQ = %02X (attendu 7F)", v);
    CHECK(telestrat_fdc_read(&f, 0, &v) && (v & WD1793_ST_NOT_READY) && (v & WD1793_ST_TRACK0),
          "statut type I = %02X", v);
    CHECK(!f.intrq, "lecture du statut efface INTRQ");
    f.data = 12;
    telestrat_fdc_write(&f, 0, 0x18);  // SEEK
    CHECK(f.track == 12, "SEEK piste %d", f.track);
    telestrat_fdc_write(&f, 0, 0x48);  // STEP IN
    CHECK(f.track == 13, "STEP IN piste %d", f.track);
    telestrat_fdc_write(&f, 0, 0x68);  // STEP OUT
    CHECK(f.track == 12, "STEP OUT piste %d", f.track);
    telestrat_fdc_write(&f, 0, 0x80);  // READ SECTOR sans disquette
    telestrat_fdc_read(&f, 0, &v);
    CHECK(v == (WD1793_ST_NOT_READY | WD1793_ST_RNF), "READ SECTOR sans disque = %02X", v);
    CHECK(!telestrat_fdc_read(&f, 5, &v), "$0315 n'appartient pas au FDC");
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
    test_fdc();
    test_acia();
    printf("test_telestrat : %d/%d vérifications réussies\n", checks - failures, checks);
    return failures ? 1 : 0;
}
