#pragma once

// neo6502_bus.h — bus du vrai W65C02S de l'Olimex Neo6502, intégré au tick
//
// Même séquence d'opérations GPIO et de NOP que chips/wdc65C02cpu.h de
// reload-emulator (broches Olimex : données/adresses multiplexées sur GPIO
// 0-7 par trois tampons OE1 = adresse basse, OE2 = adresse haute, OE3 =
// données ; RW en 11, horloge en 21, IRQ en 25, NMI en 27, RESET en 26), ce
// qui garde les temps de bus éprouvés par oric.uf2 ; mais en fonctions
// `static inline` : plus d'appel de fonction à chaque cycle (84 cycles M0+
// par cycle 6502 avec le pilote appelé, docs/PERFORMANCE.md). La ligne IRQ
// n'est réécrite que lorsqu'elle change.
//
// Interface identique à chips/wdc65C02cpu.h (macros MOS6502CPU_*).
//
// ## Licence zlib/libpng
//
// Copyright (c) 2023 Veselin Sladkov (wdc65C02cpu.h, dont ce fichier est dérivé)
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

#include <stdint.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define NEO_BUS_MASK  (0xFFu)
#define NEO_OE1_PIN   (8)
#define NEO_OE2_PIN   (9)
#define NEO_OE3_PIN   (10)
#define NEO_RW_PIN    (11)
#define NEO_CLOCK_PIN (21)
#define NEO_RESET_PIN (26)
#define NEO_IRQ_PIN   (25)
#define NEO_NMI_PIN   (27)

typedef struct {
    uint16_t addr;
    bool rw;    // true = lecture
    bool irq;   // niveau demandé (true = IRQ active)
} neo6502bus_t;

#define NEO_NOP6() __asm volatile("nop\n nop\n nop\n nop\n nop\n nop\n")

static inline void neo6502bus_init(neo6502bus_t* c) {
    gpio_init_mask(NEO_BUS_MASK);
    const uint pins[] = {NEO_OE1_PIN, NEO_OE2_PIN, NEO_OE3_PIN};
    for (int i = 0; i < 3; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 1);
    }
    gpio_init(NEO_RW_PIN);
    gpio_set_dir(NEO_RW_PIN, GPIO_IN);
    gpio_init(NEO_CLOCK_PIN);
    gpio_set_dir(NEO_CLOCK_PIN, GPIO_OUT);
    gpio_init(NEO_RESET_PIN);
    gpio_set_dir(NEO_RESET_PIN, GPIO_OUT);
    gpio_init(NEO_IRQ_PIN);
    gpio_set_dir(NEO_IRQ_PIN, GPIO_OUT);
    gpio_put(NEO_IRQ_PIN, 1);
    gpio_init(NEO_NMI_PIN);
    gpio_set_dir(NEO_NMI_PIN, GPIO_OUT);
    gpio_put(NEO_NMI_PIN, 1);
    c->irq = false;
    gpio_put(NEO_RESET_PIN, 0);
    sleep_us(1000);
    gpio_put(NEO_RESET_PIN, 1);
}

static inline void neo6502bus_reset(void) {
    gpio_put(NEO_RESET_PIN, 0);
    sleep_us(1000);
    gpio_put(NEO_RESET_PIN, 1);
}

static inline void neo6502bus_nmi(void) {
    gpio_put(NEO_NMI_PIN, 0);
    sleep_us(1000);
    gpio_put(NEO_NMI_PIN, 1);
}

// Front descendant de l'horloge, lecture de l'adresse et de R/W, front montant
static inline void neo6502bus_tick(neo6502bus_t* c) {
    gpio_put(NEO_CLOCK_PIN, 0);

    gpio_set_dir_masked(NEO_BUS_MASK, 0);
    gpio_put(NEO_OE1_PIN, 0);
    NEO_NOP6();
    uint16_t addr = gpio_get_all() & 0xFF;
    gpio_put(NEO_OE1_PIN, 1);

    gpio_put(NEO_OE2_PIN, 0);
    NEO_NOP6();
    addr |= (gpio_get_all() << 8) & 0xFF00;
    gpio_put(NEO_OE2_PIN, 1);
    c->addr = addr;
    c->rw = gpio_get(NEO_RW_PIN);

    gpio_put(NEO_CLOCK_PIN, 1);
}

// Donnée écrite par le 65C02 (cycle d'écriture)
static inline uint8_t neo6502bus_get_data(void) {
    gpio_set_dir_masked(NEO_BUS_MASK, 0);
    gpio_put(NEO_OE3_PIN, 0);
    NEO_NOP6();
    uint8_t data = gpio_get_all() & 0xFF;
    gpio_put(NEO_OE3_PIN, 1);
    return data;
}

// Donnée présentée au 65C02 (cycle de lecture)
static inline void neo6502bus_set_data(uint8_t data) {
    gpio_set_dir_masked(NEO_BUS_MASK, NEO_BUS_MASK);
    gpio_put_masked(NEO_BUS_MASK, data);
    gpio_put(NEO_OE3_PIN, 0);
    gpio_put(NEO_OE3_PIN, 1);
}

static inline void neo6502bus_set_irq(neo6502bus_t* c, bool state) {
    if (state != c->irq) {
        c->irq = state;
        gpio_put(NEO_IRQ_PIN, state ? 0 : 1);
    }
}

#define MOS6502CPU_T                 neo6502bus_t
#define MOS6502CPU_DESC_T            int
#define MOS6502CPU_INIT(c, desc)     neo6502bus_init(c)
#define MOS6502CPU_RESET(c)          neo6502bus_reset()
#define MOS6502CPU_NMI(c)            neo6502bus_nmi()
#define MOS6502CPU_TICK(c)           neo6502bus_tick(c)
#define MOS6502CPU_GET_ADDR(c)       ((c)->addr)
#define MOS6502CPU_GET_DATA(c)       neo6502bus_get_data()
#define MOS6502CPU_SET_DATA(c, data) neo6502bus_set_data(data)
#define MOS6502CPU_SET_IRQ(c, state) neo6502bus_set_irq(c, state)
#define MOS6502CPU_SET_NMI(c, state) ((void)0)
#define MOS6502CPU_SYNC(c)           (false)
