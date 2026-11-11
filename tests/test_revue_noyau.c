// test_revue_noyau.c — tests des corrections de la zone N (noyau : systems/telestrat.h,
// telestrat_ref.h, telestrat_state.h) de la revue de code (docs/CRITIQUE.md, N-01 à N-26)
//
// Même environnement que test_telestrat.c : un petit programme 6502 en banque 7.
// Lancer depuis la racine du projet (N-24 relit les sources des en-têtes).

#define _POSIX_C_SOURCE 200809L
#define CHIPS_IMPL
#define RGBA8(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chips/chips_common.h"
#include "chips/w65c02cpu.h"
#include "chips/via6522.h"
// Compteur de T1 lu sans effet de bord (VIA du socle)
#define TV_T1(v) ((uint16_t)(via_peek((v), 4) | via_peek((v), 5) << 8))
#include "chips/ay38910psg.h"
#include "chips/kbd.h"
#include "chips/clk.h"
#include "devices/wd1793.h"
#include "devices/telestrat_fdc.h"
#include "devices/mos6551acia.h"
#include "systems/telestrat.h"
#include "systems/telestrat_state.h"

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

// Assemble un programme en $C000 de rom7 (vecteur RESET -> $C000, IRQ -> $C100)
static void load_program(const uint8_t* code, size_t n) {
    memset(rom7, 0xEA, sizeof(rom7));
    memcpy(rom7, code, n);
    rom7[0x3FFC] = 0x00;
    rom7[0x3FFD] = 0xC0;
    rom7[0x3FFE] = 0x00;
    rom7[0x3FFF] = 0xC1;
}

static void boot_with(const telestrat_desc_t* base) {
    telestrat_desc_t d = *base;
    d.banks[0].type = TELESTRAT_BANK_RAM;
    d.banks[1].type = TELESTRAT_BANK_RAM;
    d.banks[6] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom6};
    d.banks[7] = (telestrat_bank_desc_t){TELESTRAT_BANK_ROM, rom7};
    telestrat_init(&sys, &d);
    telestrat_reset(&sys);
}

static void boot(void) {
    telestrat_desc_t d;
    memset(&d, 0, sizeof(d));
    boot_with(&d);
}

static void run(int cycles) {
    for (int i = 0; i < cycles; i++) telestrat_tick(&sys);
}

// Boucle infinie en $C000 (JMP $C000)
static void idle_program(void) {
    const uint8_t prog[] = {0x4C, 0x00, 0xC0};
    load_program(prog, sizeof(prog));
}

/*-- Instantanés en mémoire ---------------------------------------------------*/
typedef struct {
    uint8_t* data;
    uint32_t len, pos, cap;
} mem_state_t;

static bool mem_state_write(void* ctx, void* d, uint32_t n) {
    mem_state_t* m = (mem_state_t*)ctx;
    if (m->len + n > m->cap) return false;
    memcpy(m->data + m->len, d, n);
    m->len += n;
    return true;
}

static bool mem_state_read(void* ctx, void* d, uint32_t n) {
    mem_state_t* m = (mem_state_t*)ctx;
    if (m->pos + n > m->len) return false;
    memcpy(d, m->data + m->pos, n);
    m->pos += n;
    return true;
}

static uint8_t buf[300000];

// Positions des morceaux dans le fichier (sans texte de plate-forme)
typedef struct {
    uint32_t regs, ram, bank_ram, via, via2, psg, kbd, acia, fdc, misc, crc, end;
} layout_t;

static layout_t layout(uint32_t info_len, uint32_t total) {
    layout_t l;
    l.regs = (uint32_t)sizeof(telestrat_state_header_t) + info_len;
    l.ram = l.regs + (uint32_t)sizeof(telestrat_regs_t);
    l.bank_ram = l.ram + (uint32_t)sizeof(sys.ram);
    l.via = l.bank_ram + (uint32_t)sizeof(sys.bank_ram);
    l.via2 = l.via + (uint32_t)sizeof(sys.via);
    l.psg = l.via2 + (uint32_t)sizeof(sys.via2);
    l.kbd = l.psg + (uint32_t)sizeof(sys.psg);
    l.acia = l.kbd + (uint32_t)sizeof(sys.kbd);
    l.fdc = l.acia + (uint32_t)sizeof(sys.acia);
    l.misc = l.fdc + (uint32_t)sizeof(_telestrat_state_fdc_t);
    l.crc = l.misc + (uint32_t)sizeof(_telestrat_state_misc_t);
    l.end = total;
    return l;
}

static bool save_state(mem_state_t* m, const char* info, const char** err) {
    m->len = m->pos = 0;
    m->data = buf;
    m->cap = sizeof(buf);
    return telestrat_state_save(&sys, info, mem_state_write, m, err);
}

static bool load_state(mem_state_t* m, const char** err) {
    char info[TELESTRAT_STATE_INFO_MAX + 1];
    m->pos = 0;
    return telestrat_state_load_info(mem_state_read, m, info, sizeof(info), err) &&
           telestrat_state_load_machine(&sys, mem_state_read, m, err);
}

static void fix_crc(const layout_t* l) {
    uint32_t crc = _telestrat_state_crc(0, buf + l->regs, l->crc - l->regs);
    memcpy(buf + l->crc, &crc, sizeof(crc));
}

static void hooks_now(_telestrat_state_hooks_t* h) {
    memset(h, 0, sizeof(*h));
    _telestrat_state_hooks_take(&sys, h);
}

/*-- N-01 : instantané tronqué, rappels de l'hôte jamais ceux du fichier --------*/
static void test_n01_truncated(void) {
    idle_program();
    boot();
    run(300);
    mem_state_t m;
    const char* err = "";
    CHECK(save_state(&m, "", &err), "N-01 : instantané enregistré (%s)", err);
    const uint32_t total = m.len;
    const layout_t l = layout(0, total);
    // Rappels du fichier faussés (comme un instantané d'un autre lancement)
#define POISON(off, field) memset(buf + (off) + offsetof(via6522_t, field), 0xAA, sizeof(((via6522_t*)0)->field))
    for (int k = 0; k < 2; k++) {
        const uint32_t o = k ? l.via2 : l.via;
        POISON(o, porta_read);
        POISON(o, porta_write);
        POISON(o, portb_read);
        POISON(o, portb_write);
        POISON(o, userdata);
        POISON(o, irq_callback);
        POISON(o, irq_userdata);
    }
    memset(buf + l.psg + offsetof(ay38910psg_t, in_cb), 0xAA, sizeof(void*));
    memset(buf + l.psg + offsetof(ay38910psg_t, out_cb), 0xAA, sizeof(void*));
    memset(buf + l.psg + offsetof(ay38910psg_t, user_data), 0xAA, sizeof(void*));
    memset(buf + l.acia + offsetof(mos6551acia_t, tx_cb), 0xAA, sizeof(void*));
    memset(buf + l.acia + offsetof(mos6551acia_t, rx_cb), 0xAA, sizeof(void*));
    memset(buf + l.acia + offsetof(mos6551acia_t, user_data), 0xAA, sizeof(void*));
#undef POISON
    const uint32_t cuts[] = {l.via + (uint32_t)sizeof(sys.via),      // entre via et via2 (cas du constat)
                             l.via + 40,                              // au milieu du VIA 1
                             l.via2 + (uint32_t)sizeof(sys.via2),     // après le VIA 2
                             l.psg + 100,                             // dans l'AY
                             l.psg + (uint32_t)sizeof(sys.psg),       // après l'AY
                             l.kbd + 5,                               // dans le clavier
                             l.acia + 3,                              // dans l'ACIA
                             l.acia + (uint32_t)sizeof(sys.acia),     // après l'ACIA
                             l.fdc + 2,                               // dans le contrôleur
                             l.crc,                                   // CRC manquant
                             l.crc + 2,                               // CRC incomplet
                             total};                                  // complet mais rappels du fichier : CRC faux
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
        boot();
        _telestrat_state_hooks_t before, after;
        hooks_now(&before);
        mem_state_t mm = {buf, cuts[i], 0, sizeof(buf)};
        const bool ok = load_state(&mm, &err);
        hooks_now(&after);
        CHECK(!ok, "N-01 : coupure à %u refusée", (unsigned)cuts[i]);
        CHECK(!memcmp(&before, &after, sizeof(before)), "N-01 : coupure à %u : rappels de l'hôte conservés",
              (unsigned)cuts[i]);
        CHECK(sys.via.portb_read != NULL && sys.via2.portb_read != NULL && sys.psg.out_cb != NULL &&
                  sys.acia.rx_cb != NULL && sys.via.userdata == &sys,
              "N-01 : coupure à %u : pointeurs de ce programme", (unsigned)cuts[i]);
    }
}

/*-- N-02 : temps différé rattrapé à l'enregistrement --------------------------*/
static void test_n02_deferred(void) {
    // T1 du VIA 1 en roue libre, puis boucle au repos (longues fenêtres sautées)
    const uint8_t prog[] = {0xA9, 0x40, 0x8D, 0x0B, 0x03,   // LDA #$40, STA $030B (ACR : T1 libre)
                            0xA9, 0xFF, 0x8D, 0x04, 0x03,   // LDA #$FF, STA $0304 (T1L)
                            0x8D, 0x05, 0x03,               // STA $0305 (T1H : lance)
                            0x4C, 0x0D, 0xC0};              // JMP self
    load_program(prog, sizeof(prog));
    boot();
    run(200);
    int guard = 0;
    while (sys.deferred == 0 && guard++ < 5000) run(1);
    CHECK(sys.deferred != 0, "N-02 : un temps différé existe avant l'enregistrement");
    mem_state_t m;
    const char* err = "";
    CHECK(save_state(&m, "", &err), "N-02 : instantané enregistré (%s)", err);
    CHECK(sys.deferred == 0, "N-02 : temps différé appliqué par l'enregistrement (%u)", (unsigned)sys.deferred);
    // Suite de la machine d'origine, puis suite de l'instantané relu : même T1
    run(3000);
    _telestrat_catch_up(&sys);
    const uint16_t t1 = TV_T1(&sys.via);
    const uint32_t ticks = sys.system_ticks;
    CHECK(load_state(&m, &err), "N-02 : instantané relu (%s)", err);
    run(3000);
    _telestrat_catch_up(&sys);
    CHECK(sys.system_ticks == ticks && TV_T1(&sys.via) == t1, "N-02 : relu puis 3000 cycles : T1 %04X (attendu %04X)",
          TV_T1(&sys.via), t1);
}

/*-- N-03 : la lecture de SR n'est pas sans effet de bord ----------------------*/
static void test_n03_sr_read(void) {
    // Décalage terminé (IFR.SR levé, IRQ masquée par SEI), longue fenêtre de repos ; la lecture
    // de SR efface le drapeau : la ligne IRQ doit retomber avant le CLI qui suit, sinon le
    // gestionnaire est appelé pour rien (IFR.SR absent : compteur $20)
    const uint8_t prog[] = {0x78,                            // SEI
                            0xA9, 0x08, 0x8D, 0x0B, 0x03,   // LDA #$08, STA $030B (ACR : décalage en entrée sous φ2)
                            0xA9, 0x84, 0x8D, 0x0E, 0x03,   // LDA #$84, STA $030E (IER : SR)
                            0xAD, 0x0A, 0x03,               // LDA $030A (lance un décalage)
                            0xA2, 0x40, 0xCA, 0xD0, 0xFD,   // LDX #$40 ; DEX ; BNE (attend la fin du décalage)
                            0xAD, 0x0A, 0x03,               // LDA $030A (efface le drapeau, relance)
                            0x58,                           // CLI
                            0x4C, 0x17, 0xC0};              // JMP self
    load_program(prog, sizeof(prog));
    const uint8_t handler[] = {0xAD, 0x0D, 0x03,   // LDA $030D (IFR)
                               0x29, 0x04,         // AND #$04
                               0xD0, 0x02,         // BNE +2
                               0xE6, 0x20,         // INC $20 (IRQ sans drapeau SR : parasite)
                               0xAD, 0x0A, 0x03,   // LDA $030A (efface le drapeau, relance)
                               0xE6, 0x21,         // INC $21
                               0x40};              // RTI
    memcpy(rom7 + 0x100, handler, sizeof(handler));
    boot();
    run(3000);
    CHECK(sys.ram[0x21] > 20, "N-03 : le registre à décalage interrompt (%u IRQ)", sys.ram[0x21]);
    CHECK(sys.ram[0x20] == 0, "N-03 : aucune IRQ parasite après lecture de SR (%u)", sys.ram[0x20]);
}

/*-- N-04, N-08 : validation, CRC, pointeurs à zéro, endianness ----------------*/
static void test_n04_validation(void) {
    idle_program();
    boot();
    run(500);
    mem_state_t m;
    const char* err = "";
    CHECK(save_state(&m, "", &err), "N-04 : instantané enregistré (%s)", err);
    const layout_t l = layout(0, m.len);
    CHECK(m.len == l.crc + 4, "N-04 : disposition du fichier (%u / %u)", (unsigned)m.len, (unsigned)(l.crc + 4));
    static uint8_t good[sizeof(buf)];
    memcpy(good, buf, m.len);
    const uint32_t len = m.len;

    CHECK(load_state(&m, &err), "N-04 : instantané intact relu (%s)", err);

    // Un octet changé : CRC faux
    memcpy(buf, good, len);
    buf[l.ram + 0x1234] ^= 0x10;
    m.len = len;
    CHECK(!load_state(&m, &err) && strstr(err, "CRC"), "N-04 : octet de RAM abîmé : %s", err);

    // Valeurs hors bornes, CRC refait : refusées
    struct {
        const char* nom;
        uint32_t off;
        uint32_t valeur;
        int taille;
    } cas[] = {
        {"printer_ack", l.misc + (uint32_t)offsetof(_telestrat_state_misc_t, printer_ack), 99, 4},
        {"psg_next", l.misc + (uint32_t)offsetof(_telestrat_state_misc_t, psg_next), 0x00F00000, 4},
        {"psg_next_sample", l.misc + (uint32_t)offsetof(_telestrat_state_misc_t, psg_next_sample), 0x7F000000, 4},
        {"sample_acc", l.misc + (uint32_t)offsetof(_telestrat_state_misc_t, sample_acc), 2000000, 4},
        {"blink_counter", l.misc + (uint32_t)offsetof(_telestrat_state_misc_t, blink_counter), 200, 4},
        {"head[2]", l.fdc + (uint32_t)offsetof(_telestrat_state_fdc_t, head) + 2, 200, 1},
        {"drive", l.fdc + (uint32_t)offsetof(_telestrat_state_fdc_t, drive), 9, 1},
        {"sr_count du VIA 2", l.via2 + (uint32_t)offsetof(via6522_t, sr_count), 77, 1},
        {"addr de l'AY", l.psg + (uint32_t)offsetof(ay38910psg_t, addr), 31, 1},
    };
    for (size_t i = 0; i < sizeof(cas) / sizeof(cas[0]); i++) {
        memcpy(buf, good, len);
        memcpy(buf + cas[i].off, &cas[i].valeur, (size_t)cas[i].taille);
        fix_crc(&l);
        m.len = len;
        CHECK(!load_state(&m, &err) && strstr(err, "hors bornes"), "N-04 : %s hors bornes refusé : %s", cas[i].nom, err);
    }
    // Contre-épreuve : une valeur dans les bornes avec CRC refait est acceptée
    memcpy(buf, good, len);
    const uint32_t ack = 12;
    memcpy(buf + l.misc + offsetof(_telestrat_state_misc_t, printer_ack), &ack, 4);
    fix_crc(&l);
    m.len = len;
    CHECK(load_state(&m, &err), "N-04 : valeurs dans les bornes acceptées (%s)", err);
}

static void test_n08_file_contents(void) {
    idle_program();
    boot();
    run(500);
    // Pointeurs de l'hôte bien présents dans la machine
    sys.via.irq_callback = (void (*)(bool, void*))(uintptr_t)0x1234;
    sys.via.irq_userdata = (void*)(uintptr_t)0x5678;
    mem_state_t m;
    const char* err = "";
    _telestrat_state_hooks_t before, after;
    hooks_now(&before);
    CHECK(save_state(&m, "x=1\n", &err), "N-08 : instantané enregistré (%s)", err);
    hooks_now(&after);
    CHECK(!memcmp(&before, &after, sizeof(before)) && sys.via.userdata == &sys && sys.acia.user_data == &sys,
          "N-08 : rappels de l'hôte remis après l'enregistrement");
    const layout_t l = layout(4, m.len);
    // Aucun pointeur dans le fichier
    static const uint8_t zero[sizeof(void*)];
    CHECK(!memcmp(buf + l.via + offsetof(via6522_t, userdata), zero, sizeof(void*)) &&
              !memcmp(buf + l.via2 + offsetof(via6522_t, portb_read), zero, sizeof(void*)) &&
              !memcmp(buf + l.via + offsetof(via6522_t, irq_callback), zero, sizeof(void*)) &&
              !memcmp(buf + l.via + offsetof(via6522_t, irq_userdata), zero, sizeof(void*)),
          "N-08 : pointeurs des VIA à zéro dans le fichier");
    CHECK(!memcmp(buf + l.psg + offsetof(ay38910psg_t, in_cb), zero, sizeof(void*)) &&
              !memcmp(buf + l.psg + offsetof(ay38910psg_t, out_cb), zero, sizeof(void*)) &&
              !memcmp(buf + l.psg + offsetof(ay38910psg_t, user_data), zero, sizeof(void*)),
          "N-08 : pointeurs de l'AY à zéro dans le fichier");
    CHECK(!memcmp(buf + l.acia + offsetof(mos6551acia_t, tx_cb), zero, sizeof(void*)) &&
              !memcmp(buf + l.acia + offsetof(mos6551acia_t, rx_cb), zero, sizeof(void*)) &&
              !memcmp(buf + l.acia + offsetof(mos6551acia_t, user_data), zero, sizeof(void*)),
          "N-08 : pointeurs de l'ACIA à zéro dans le fichier");
    telestrat_state_header_t h;
    memcpy(&h, buf, sizeof(h));
    CHECK(h.version == TELESTRAT_STATE_VERSION && h.endian == TELESTRAT_STATE_ENDIAN && h.info_len == 4,
          "N-08 : en-tête (version %u, endian %08X)", (unsigned)h.version, (unsigned)h.endian);
    // Endianness différente ou signature d'une autre disposition : refusées
    char info[16];
    h.endian = 0x04030201u;
    memcpy(buf, &h, sizeof(h));
    m.pos = 0;
    CHECK(!telestrat_state_load_info(mem_state_read, &m, info, sizeof(info), &err) && strstr(err, "variante"),
          "N-08 : autre endianness refusée : %s", err);
    h.endian = TELESTRAT_STATE_ENDIAN;
    h.signature ^= 1;
    memcpy(buf, &h, sizeof(h));
    m.pos = 0;
    CHECK(!telestrat_state_load_info(mem_state_read, &m, info, sizeof(info), &err), "N-08 : autre signature refusée");
    // La signature dépend des positions de champs et pas seulement des tailles
    CHECK(telestrat_state_signature() == telestrat_state_signature() && telestrat_state_signature() != 2166136261u,
          "N-08 : signature calculée");
}

/*-- N-05 : telestrat_printer_resume ---------------------------------------------*/
static int printed;
static void printer_cb(uint8_t d, void* u) {
    (void)d;
    (void)u;
    printed++;
}

static void test_n05_printer_resume(void) {
    idle_program();
    telestrat_desc_t d;
    memset(&d, 0, sizeof(d));
    d.printer.func = printer_cb;
    boot_with(&d);
    run(400);
    // Fenêtre de repos longue en cours, imprimante qui redevient prête
    sys.printer_wait = true;
    sys.quiet_until = sys.system_ticks + 1000;
    telestrat_printer_resume(&sys);
    CHECK(!sys.printer_wait && sys.printer_ack == 40, "N-05 : ACK lancé");
    CHECK(sys.quiet_until == sys.system_ticks && sys.inputs_dirty, "N-05 : pas complet demandé tout de suite");
    bool low = false;
    for (int i = 0; i < 8; i++) {
        run(1);
        if (!sys.via.ca1_pin) low = true;
    }
    CHECK(low, "N-05 : CA1 passe à 0 dans les 8 cycles");
    // Sans attente en cours : rien ne change
    sys.printer_ack = 0;
    sys.quiet_until = sys.system_ticks + 1000;
    telestrat_printer_resume(&sys);
    CHECK(sys.printer_ack == 0 && sys.quiet_until == sys.system_ticks + 1000, "N-05 : sans attente, aucun effet");
}

/*-- N-06 : printer_wait enregistré ------------------------------------------------*/
static void test_n06_printer_wait(void) {
    idle_program();
    boot();
    run(300);
    sys.printer_wait = true;
    mem_state_t m;
    const char* err = "";
    CHECK(save_state(&m, "", &err), "N-06 : instantané enregistré (%s)", err);
    sys.printer_wait = false;
    CHECK(load_state(&m, &err), "N-06 : instantané relu (%s)", err);
    CHECK(sys.printer_wait, "N-06 : attente de l'imprimante retrouvée");
    sys.printer_wait = false;
    CHECK(save_state(&m, "", &err) && load_state(&m, &err) && !sys.printer_wait, "N-06 : pas d'attente : pas d'attente");
}

/*-- N-09 : capture avec une pile qui reboucle -------------------------------------*/
static void test_n09_stack_wrap(void) {
    for (int s0 = 0; s0 < 4; s0++) {
        const uint8_t prog[] = {0xA2, (uint8_t)s0, 0x9A, 0x4C, 0x03, 0xC0};  // LDX #s0, TXS, JMP self
        load_program(prog, sizeof(prog));
        boot();
        run(60);
        telestrat_regs_t r;
        CHECK(sys.cpu.S == s0, "N-09 : S = %02X (attendu %02X)", sys.cpu.S, s0);
        CHECK(telestrat_cpu_capture(&sys, &r), "N-09 : capture avec S = %02X", s0);
        CHECK(r.s == s0 && r.pc >= 0xC003 && r.pc <= 0xC005, "N-09 : S = %02X, PC = %04X", r.s, r.pc);
        run(20);
        CHECK(sys.cpu.S == s0, "N-09 : S = %02X rendu par le RTI", sys.cpu.S);
        CHECK(telestrat_cpu_restore(&sys, &r), "N-09 : restitution avec S = %02X", s0);
        run(20);
        CHECK(sys.cpu.S == s0 && sys.cpu.addr >= 0xC003 && sys.cpu.addr <= 0xC005, "N-09 : après restitution S = %02X",
              sys.cpu.S);
    }
}

/*-- N-11 : RESET --------------------------------------------------------------------*/
static telestrat_t clone;
static void test_n11_reset(void) {
    const uint8_t prog[] = {0xA9, 0x40, 0x8D, 0x0B, 0x03,   // ACR : T1 libre
                            0xA9, 0xFF, 0x8D, 0x04, 0x03, 0x8D, 0x05, 0x03,  // T1 lancé
                            0x4C, 0x0D, 0xC0};
    load_program(prog, sizeof(prog));
    boot();
    run(200);
    int guard = 0;
    while (sys.deferred < 12 && guard++ < 5000) run(1);
    CHECK(sys.deferred >= 12, "N-11 : temps différé avant le RESET");
    // Valeur attendue du compteur, temps différé appliqué
    memcpy(&clone, &sys, sizeof(clone));
    _telestrat_catch_up(&clone);
    const uint16_t attendu = TV_T1(&clone.via);
    sys.strobe = true;
    sys.printer_ack = 32;
    sys.printer_wait = true;
    telestrat_reset(&sys);
    CHECK(sys.deferred == 0 && TV_T1(&sys.via) == attendu, "N-11 : compteur de T1 après RESET %04X (attendu %04X)",
          TV_T1(&sys.via), attendu);
    CHECK(!sys.strobe && sys.printer_ack == 0 && !sys.printer_wait, "N-11 : imprimante remise à zéro");
}

/*-- N-12/N-18/N-22/N-23 : vérifiés à la compilation -----------------------------------*/
static void test_n12_public_api(void) {
    // Ces fonctions sont définies dans la partie CHIPS_IMPL, déclarées par la partie publique
    void (*fn[])(telestrat_t*) = {telestrat_printer_resume};
    CHECK(fn[0] == telestrat_printer_resume, "N-05/N-12 : telestrat_printer_resume est une fonction publique");
    bool (*ins)(telestrat_t*, int, uint8_t*, size_t, bool) = telestrat_insert_disk;
    void (*ring)(telestrat_t*, bool) = telestrat_set_ring;
    void (*kd)(telestrat_t*, int) = telestrat_key_down;
    void (*ku)(telestrat_t*, int) = telestrat_key_up;
    void (*ku2)(telestrat_t*, uint32_t) = telestrat_kbd_update;
    bool (*cap)(telestrat_t*, telestrat_regs_t*) = telestrat_cpu_capture;
    bool (*res)(telestrat_t*, const telestrat_regs_t*) = telestrat_cpu_restore;
    CHECK(ins && ring && kd && ku && ku2 && cap && res, "N-12/N-13 : API publique déclarée");
#ifndef TELESTRAT_NOINLINE
    CHECK(0, "N-18 : TELESTRAT_NOINLINE non défini");
#endif
}

/*-- N-16 : caractérisation des reflets du Microdisc (non vérifié sur matériel) ------------*/
static void test_n16_mirror(void) {
    const uint8_t prog[] = {0xA9, 0x5A, 0x8D, 0x16, 0x03,   // LDA #$5A, STA $0316 (reflet du T1LL du VIA 1)
                            0x4C, 0x05, 0xC0};
    load_program(prog, sizeof(prog));
    boot();
    run(60);
    CHECK((sys.via.t1_latch & 0xFF) == 0x5A,
          "N-16 (caractérisation, non vérifié sur matériel) : $0316 tombe sur le VIA 1 (T1 latch %04X)",
          sys.via.t1_latch);
}

/*-- N-17 : débit audio borné, pas de débordement ------------------------------------------*/
static uint32_t audio_count;
static void audio_cb(const uint8_t sample, void* user_data) {
    (void)sample;
    (void)user_data;
    audio_count++;
}

static void test_n17_rates(void) {
    idle_program();
    telestrat_desc_t d;
    memset(&d, 0, sizeof(d));
    d.audio.callback.func = audio_cb;
    d.audio.sample_rate = 3000000;
    audio_count = 0;
    boot_with(&d);
    CHECK(sys.sample_rate == TELESTRAT_FREQUENCY, "N-17 : débit borné à 1 MHz (%u)", (unsigned)sys.sample_rate);
    run(1000);
    CHECK(audio_count == 1000, "N-17 : un échantillon par cycle au plus (%u / 1000)", (unsigned)audio_count);
    CHECK(_telestrat_delay_quiet(INT32_MAX) == 536870911u && _telestrat_delay_quiet(0) == UINT32_MAX &&
              _telestrat_delay_quiet(1) == 0 && _telestrat_delay_quiet(5) == 1,
          "N-17 : _telestrat_delay_quiet sans débordement");
}

/*-- N-25 : table vidéo prête dès telestrat_init ------------------------------------------------*/
static void test_n25_lut(void) {
    memset(telestrat_pair_lut, 0, sizeof(telestrat_pair_lut));
    idle_program();
    boot();
    CHECK(telestrat_pair_lut[0xFF] == 0x77 && telestrat_pair_lut[(2 << 6) | (1 << 3) | 2] == 0x12 &&
              telestrat_pair_lut[(1 << 6) | (1 << 3) | 2] == 0x21,
          "N-25 : table des pixels initialisée par telestrat_init");
    telestrat_screen_update(&sys);  // sans test paresseux : ne doit pas planter
}

/*-- N-26 : texte d'info coupé sur une frontière UTF-8 ---------------------------------------------*/
static void test_n26_utf8(void) {
    idle_program();
    boot();
    run(200);
    static char info[1100];
    memset(info, 'a', 1023);
    info[1023] = (char)0xC3;  // « é » à cheval sur la limite de 1024 octets
    info[1024] = (char)0xA9;
    memset(info + 1025, 'b', 20);
    info[1045] = 0;
    mem_state_t m;
    const char* err = "";
    CHECK(save_state(&m, info, &err), "N-26 : instantané enregistré (%s)", err);
    telestrat_state_header_t h;
    memcpy(&h, buf, sizeof(h));
    CHECK(h.info_len == 1023, "N-26 : texte coupé avant le caractère (%u)", (unsigned)h.info_len);
    static char lu[TELESTRAT_STATE_INFO_MAX + 1];
    m.pos = 0;
    CHECK(telestrat_state_load_info(mem_state_read, &m, lu, sizeof(lu), &err) && strlen(lu) == 1023 &&
              ((uint8_t)lu[1022] & 0xC0) != 0x80,
          "N-26 : texte relu (%s)", err);
    // Texte ASCII de 1030 octets : coupé à 1024
    memset(info, 'z', 1030);
    info[1030] = 0;
    CHECK(save_state(&m, info, &err), "N-26 : ASCII long enregistré (%s)", err);
    memcpy(&h, buf, sizeof(h));
    CHECK(h.info_len == TELESTRAT_STATE_INFO_MAX, "N-26 : ASCII coupé à la limite (%u)", (unsigned)h.info_len);
}

/*-- N-24 : tables communes aux deux en-têtes (système optimisé / référence) ------------------------*/
static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

// Texte de s entre `from` (inclus) et `to` (exclu, cherché après from) ; NULL si absent
static char* block(const char* s, const char* from, const char* to) {
    const char* a = strstr(s, from);
    if (!a) return NULL;
    const char* b = strstr(a, to);
    if (!b) return NULL;
    char* r = (char*)malloc((size_t)(b - a) + 1);
    if (!r) return NULL;
    memcpy(r, a, (size_t)(b - a));
    r[b - a] = 0;
    return r;
}

static void test_n24_shared_tables(void) {
    char* a = slurp("src/systems/telestrat.h");
    char* b = slurp("src/systems/telestrat_ref.h");
    if (!a || !b) {
        printf("N-24 : sources des en-têtes introuvables (lancer depuis la racine du projet), ignoré\n");
        free(a);
        free(b);
        return;
    }
    static const struct {
        const char* nom;
        const char* from;
        const char* to;
    } blocs[] = {
        {"constantes (fréquence, écran, banques)", "#define TELESTRAT_FREQUENCY", "#define TELESTRAT_PALETTE_BITS"},
        {"palette", "static const uint32_t telestrat_palette[", "};"},
        {"types de banque", "typedef enum {\n    TELESTRAT_BANK_EMPTY", "#define TELESTRAT_JOY_RIGHT"},
        {"accès aux VIA (_tv_*)", "// Accès aux VIA du socle", "void telestrat_init(telestrat_t* sys, const"},
        {"attributs vidéo", "#define TELESTRAT_PATTR_HIRES", "\n\n"},
        {"matrice du clavier", "static void _telestrat_init_key_map(telestrat_t* sys) {", "#endif  // CHIPS_IMPL"},
    };
    for (size_t i = 0; i < sizeof(blocs) / sizeof(blocs[0]); i++) {
        char* x = block(a, blocs[i].from, blocs[i].to);
        char* y = block(b, blocs[i].from, blocs[i].to);
        CHECK(x && y && !strcmp(x, y), "N-24 : %s identique dans telestrat.h et telestrat_ref.h", blocs[i].nom);
        free(x);
        free(y);
    }
    free(a);
    free(b);
}

int main(void) {
    test_n01_truncated();
    test_n02_deferred();
    test_n03_sr_read();
    test_n04_validation();
    test_n08_file_contents();
    test_n05_printer_resume();
    test_n06_printer_wait();
    test_n09_stack_wrap();
    test_n11_reset();
    test_n12_public_api();
    test_n16_mirror();
    test_n17_rates();
    test_n25_lut();
    test_n26_utf8();
    test_n24_shared_tables();
    printf("test_revue_noyau : %d/%d vérifications réussies\n", checks - failures, checks);
    return failures ? 1 : 0;
}
