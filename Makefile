# Neo6502TeleStrat — Oric Telestrat pour Olimex Neo6502
#
#   make          ROM -> en-têtes, banc PC sans écran, tests
#   make test     tests unitaires + tests de démarrage
#   make uf2      firmware telestrat.uf2 pour le Neo6502 (arm-none-eabi-gcc)
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
HEADERS := src/systems/telestrat.h src/devices/wd1793.h src/devices/telestrat_fdc.h src/devices/mos6551acia.h $(ROMS_H)

all: test

$(ROMS_H): tools/fetch_roms.py
	python3 tools/fetch_roms.py

$(BUILD)/telestrat_headless: platforms/pc/telestrat_headless.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD)/test_telestrat: tests/test_telestrat.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD):
	mkdir -p $@

headless: $(BUILD)/telestrat_headless

test: $(BUILD)/test_telestrat $(BUILD)/telestrat_headless
	$(BUILD)/test_telestrat
	sh tests/test_boot.sh $(BUILD)/telestrat_headless

uf2: $(ROMS_H)
	cmake -S platforms/rp2040 -B $(BUILD)/rp2040 -DRELOAD_DIR=$(RELOAD_DIR) $(if $(NEO_SLOT_TELESTRAT),-DNEO_MULTIBOOT_DIR=$(NEO_MULTIBOOT_DIR) -DNEO_SLOT_TELESTRAT=$(NEO_SLOT_TELESTRAT))
	$(MAKE) -C $(BUILD)/rp2040 -j8 telestrat
	@ls -l $(BUILD)/rp2040/telestrat.uf2

clean:
	rm -rf $(BUILD)

.PHONY: all headless test uf2 clean
