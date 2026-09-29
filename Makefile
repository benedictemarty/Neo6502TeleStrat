# Neo6502TeleStrat — Oric Telestrat pour Olimex Neo6502
#
#   make          ROM -> en-têtes, banc PC sans écran, tests
#   make test     tests unitaires + tests de démarrage
#   make uf2      firmware telestrat.uf2 pour le Neo6502 (arm-none-eabi-gcc)
#   make charge   charge du RP2040 sans carte (docs/PERFORMANCE.md)
#   make clean
#
# Dépend de reload-emulator (puces 6502/6522/AY, clavier, mémoire ; SDK Pico) :
RELOAD_DIR ?= $(HOME)/reload-emulator

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers
CPPFLAGS += -Isrc -I$(RELOAD_DIR)/src

BUILD := build
ROMS_H := src/roms/telestrat_roms.h
HEADERS := src/systems/telestrat.h src/devices/wd1793.h src/devices/telestrat_fdc.h src/devices/mos6551acia.h $(ROMS_H) src/devices/minitel_port.h platforms/pc/line_tcp.h src/devices/hayes_line.h

all: test

$(ROMS_H): tools/fetch_roms.py
	python3 tools/fetch_roms.py

$(BUILD)/telestrat_headless: platforms/pc/telestrat_headless.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD)/telestrat_headless_ref: platforms/pc/telestrat_headless.c $(HEADERS) src/systems/telestrat_ref.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTELESTRAT_REF -o $@ $<

$(BUILD)/replay: tests/replay.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD)/test_telestrat: tests/test_telestrat.c $(HEADERS) platforms/rp2040/src/telestrat_video.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Iplatforms/rp2040/src $(CFLAGS) -o $@ $<

$(BUILD):
	mkdir -p $@

headless: $(BUILD)/telestrat_headless

test: $(BUILD)/test_telestrat $(BUILD)/telestrat_headless $(BUILD)/telestrat_headless_ref $(BUILD)/replay
	$(BUILD)/test_telestrat
	sh tests/test_boot.sh $(BUILD)/telestrat_headless
	sh tests/test_telematic.sh $(BUILD)/telestrat_headless
	sh tests/test_minitel_emul.sh $(BUILD)/telestrat_headless
	sh tests/test_replay.sh $(BUILD)/telestrat_headless_ref $(BUILD)/replay

uf2: $(ROMS_H)
	cmake -S platforms/rp2040 -B $(BUILD)/rp2040 -DRELOAD_DIR=$(RELOAD_DIR) $(if $(NEO_SLOT_TELESTRAT),-DNEO_MULTIBOOT_DIR=$(NEO_MULTIBOOT_DIR) -DNEO_SLOT_TELESTRAT=$(NEO_SLOT_TELESTRAT))
	$(MAKE) -C $(BUILD)/rp2040 -j8 telestrat
	@ls -l $(BUILD)/rp2040/telestrat.uf2

clean:
	rm -rf $(BUILD)

.PHONY: all headless test uf2 clean

# Charge du RP2040 sans carte (docs/PERFORMANCE.md) : trace du banc PC rejouée
# par la cible ARM telestrat_bench dans un émulateur Cortex-M0+ (python3 +
# unicorn + capstone : PYTHON=chemin/vers/python d'un venv qui les a).
PYTHON ?= python3
CHARGE_DSK ?= $(HOME)/oriclib/games/dsk/STRATSED.DSK
charge: $(BUILD)/telestrat_headless uf2
	$(MAKE) -C $(BUILD)/rp2040 telestrat_bench
	cp $(CHARGE_DSK) $(BUILD)/charge.dsk
	$(BUILD)/telestrat_headless -c oricutron -0 $(BUILD)/charge.dsk -f 1500 -w 500 \
	    -t '1~~~~~~DIR\n' -B $(BUILD)/charge > /dev/null
	cp $(CHARGE_DSK) $(BUILD)/charge.dsk
	$(PYTHON) tools/rp2040_load.py $(BUILD)/rp2040/telestrat_bench.elf $(BUILD)/rp2040/telestrat.elf \
	    $(BUILD)/charge --disk $(BUILD)/charge.dsk --every 10

.PHONY: charge
