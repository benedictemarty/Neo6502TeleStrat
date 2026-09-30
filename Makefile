# Neo6502TeleStrat — Oric Telestrat pour Olimex Neo6502
#
#   make          ROM -> en-têtes, banc PC sans écran, tests
#   make test     tests unitaires + tests de démarrage
#   make uf2      firmware telestrat.uf2 pour le Neo6502 (arm-none-eabi-gcc)
#   make charge   charge du RP2040 sans carte (docs/PERFORMANCE.md)
#   make clean
#
# Dépend de reload-emulator (puces 6502/6522/AY, clavier, mémoire ; SDK Pico),
# épinglé sur une étiquette de socle vérifiée (tools/reload_socle.sh, clone
# local dans ~/.cache/reload-socle). Changer d'étiquette : RELOAD_SOCLE=… ;
# un autre arbre (ex. la tête de reload) : RELOAD_DIR=~/reload-emulator.
RELOAD_SOCLE ?= socle-2026-10-01
ifeq ($(origin RELOAD_DIR),undefined)
RELOAD_DIR := $(HOME)/.cache/reload-socle/$(RELOAD_SOCLE)
_SOCLE := $(shell sh tools/reload_socle.sh $(RELOAD_SOCLE) >&2 || echo erreur)
ifneq ($(_SOCLE),)
$(error socle reload $(RELOAD_SOCLE) indisponible (tools/reload_socle.sh))
endif
endif

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers
CPPFLAGS += -Isrc -I$(RELOAD_DIR)/src
# Menu à l'écran : osd.h du socle, grille du Telestrat (960 x 544 : 120 x 34 cases)
CPPFLAGS += -DOSD_COLS=120 -DOSD_ROWS=34

BUILD := build
ROMS_H := src/roms/telestrat_roms.h
HEADERS := $(RELOAD_DIR)/src/devices/oric_tape.h $(RELOAD_DIR)/src/devices/oric_tape_rec.h $(RELOAD_DIR)/src/devices/oric_tape_turbo.h src/systems/telestrat.h $(RELOAD_DIR)/src/devices/wd1793.h $(RELOAD_DIR)/src/devices/oric_dsk.h src/devices/telestrat_fdc.h src/devices/mos6551acia.h $(ROMS_H) src/devices/minitel_port.h platforms/pc/line_tcp.h platforms/pc/menu_pc.h src/devices/hayes_line.h src/devices/modem_mux.h src/devices/drive_set.h $(RELOAD_DIR)/src/osd/osd.h src/osd/osd_menu.h src/osd/osd_font.h src/osd/osd_config.h src/osd/rom_pool.h src/devices/byte_fifo.h src/osd/rom_builtin.h src/devices/printer_out.h src/devices/printer_fx80.h src/devices/plotter_mcp40.h platforms/pc/printer_files.h src/systems/telestrat_state.h $(wildcard src/chips/*.h) $(RELOAD_DIR)/src/chips/ay38910psg.h $(RELOAD_DIR)/src/chips/w65c02cpu.h $(RELOAD_DIR)/src/chips/kbd.h $(RELOAD_DIR)/src/chips/clk.h $(RELOAD_DIR)/src/chips/chips_common.h

all: test

$(ROMS_H): tools/fetch_roms.py
	python3 tools/fetch_roms.py

$(BUILD)/telestrat_headless: platforms/pc/telestrat_headless.c $(HEADERS) platforms/rp2040/src/telestrat_frame.h platforms/rp2040/src/telestrat_video.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Iplatforms/rp2040/src $(CFLAGS) -o $@ $<

$(BUILD)/telestrat_headless_ref: platforms/pc/telestrat_headless.c $(HEADERS) src/systems/telestrat_ref.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTELESTRAT_REF -o $@ $<

$(BUILD)/printer_render: platforms/pc/printer_render.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD)/replay: tests/replay.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

$(BUILD)/test_telestrat: tests/test_telestrat.c $(HEADERS) platforms/rp2040/src/telestrat_video.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Iplatforms/rp2040/src $(CFLAGS) -o $@ $<

# SingleStepTests de Tom Harte (docs/TESTS.md) sur le cœur du socle
# ($(RELOAD_DIR)/src/chips/w65c02cpu.h) ; ignorés sans données
# ($(RELOAD_DIR)/tools/cputest/fetch_harte.sh).
HARTE_DIR ?= $(HOME)/.cache/65x02
$(BUILD)/harte_c02: $(RELOAD_DIR)/tools/cputest/harte.c $(RELOAD_DIR)/src/chips/w65c02cpu.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DUSE_65C02 -o $@ $<

cpu_harte: $(BUILD)/harte_c02
	@if [ -d $(HARTE_DIR)/wdc65c02/v1 ]; then $(BUILD)/harte_c02 -x 5c $(HARTE_DIR)/wdc65c02/v1 > $(BUILD)/harte_c02.log; \
	  r=$$?; tail -1 $(BUILD)/harte_c02.log; exit $$r; \
	else echo "cpu_harte : données absentes ($(HARTE_DIR)), ignoré"; fi

$(BUILD):
	mkdir -p $@

headless: $(BUILD)/telestrat_headless

test: cpu_harte $(BUILD)/test_telestrat $(BUILD)/telestrat_headless $(BUILD)/telestrat_headless_ref $(BUILD)/replay $(BUILD)/printer_render
	$(BUILD)/test_telestrat
	sh tests/test_boot.sh $(BUILD)/telestrat_headless
	sh tests/test_telematic.sh $(BUILD)/telestrat_headless
	sh tests/test_minitel_emul.sh $(BUILD)/telestrat_headless
	sh tests/test_rs232.sh $(BUILD)/telestrat_headless
	sh tests/test_menu.sh $(BUILD)/telestrat_headless
	sh tests/test_tape.sh $(BUILD)/telestrat_headless
	sh tests/test_stratoric.sh $(BUILD)/telestrat_headless
	sh tests/test_printer.sh $(BUILD)/telestrat_headless $(BUILD)/printer_render
	sh tests/test_state.sh $(BUILD)/telestrat_headless
	sh tests/test_profiles.sh $(BUILD)/telestrat_headless
	sh tests/test_replay.sh $(BUILD)/telestrat_headless_ref $(BUILD)/replay $(BUILD)/telestrat_headless
	python3 tests/test_carte.py

uf2: $(ROMS_H)
	@# Socle changé (autre RELOAD_DIR) : les caches CMake (sous-projets du SDK
	@# compris) gardent l'ancien chemin ; dossier recréé (témoin .reload_dir)
	@if [ -d $(BUILD)/rp2040 ] && [ "$$(cat $(BUILD)/rp2040/.reload_dir 2>/dev/null)" != "$(RELOAD_DIR)" ]; then \
	    echo "uf2 : socle changé, $(BUILD)/rp2040 recréé"; rm -rf $(BUILD)/rp2040; fi
	cmake -S platforms/rp2040 -B $(BUILD)/rp2040 -DRELOAD_DIR=$(RELOAD_DIR) $(if $(NEO_SLOT_TELESTRAT),-DNEO_MULTIBOOT_DIR=$(NEO_MULTIBOOT_DIR) -DNEO_SLOT_TELESTRAT=$(NEO_SLOT_TELESTRAT))
	echo '$(RELOAD_DIR)' > $(BUILD)/rp2040/.reload_dir
	$(MAKE) -C $(BUILD)/rp2040 -j8 telestrat
	@ls -l $(BUILD)/rp2040/telestrat.uf2

clean:
	rm -rf $(BUILD)

.PHONY: all headless test cpu_harte uf2 clean

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

# Coût d'une ligne affichée par le cœur 1 : menu contre image (même émulateur)
charge-menu: uf2
	$(MAKE) -C $(BUILD)/rp2040 telestrat_osd_bench
	$(PYTHON) tools/osd_cost.py $(BUILD)/rp2040/telestrat_osd_bench.elf

.PHONY: charge charge-menu
