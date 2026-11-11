# Neo6502TeleStrat — Oric Telestrat pour Olimex Neo6502
#
#   make          compile le banc PC et ses outils (sans lancer de test)
#   make test     tests unitaires + tests de démarrage (les tests ignorés sont
#                 comptés et listés ; STRICT=1 : un test ignoré fait échouer)
#   make test-asan  test_telestrat et le banc sous AddressSanitizer/UBSan
#   make uf2      firmware telestrat.uf2 pour le Neo6502 (arm-none-eabi-gcc)
#   make charge   charge du RP2040 sans carte (docs/PERFORMANCE.md)
#   make compile_flags.txt   options pour clangd (chemin du socle courant)
#   make clean
#
# Dépend de reload-emulator (puces 6502/6522/AY, clavier, mémoire ; SDK Pico),
# épinglé sur l'étiquette de tools/SOCLE (seule source : lue aussi par
# tools/reload_socle.sh et platforms/rp2040/CMakeLists.txt), cloné localement par
# tools/reload_socle.sh dans ${XDG_CACHE_HOME:-~/.cache}/reload-socle. Le dépôt
# reload se cherche dans $RELOAD_REPO, ~/Projets/reload-emulator, ~/reload-emulator.
# Changer d'étiquette : RELOAD_SOCLE=… ; un autre arbre (ex. la tête de reload) :
# RELOAD_DIR=~/Projets/reload-emulator. Options : WERROR=1 (-Werror), BUILD=répertoire.
RELOAD_SOCLE ?= $(shell tr -d ' \t\r\n' < tools/SOCLE)
SOCLE_CACHE ?= $(if $(XDG_CACHE_HOME),$(XDG_CACHE_HOME),$(HOME)/.cache)/reload-socle
ifeq ($(origin RELOAD_DIR),undefined)
RELOAD_DIR := $(SOCLE_CACHE)/$(RELOAD_SOCLE)
# « make clean » ne demande pas le socle
ifneq ($(MAKECMDGOALS),clean)
_SOCLE := $(shell RELOAD_SOCLE_DIR=$(SOCLE_CACHE) sh tools/reload_socle.sh $(RELOAD_SOCLE) >&2 || echo erreur)
ifneq ($(_SOCLE),)
$(error socle reload $(RELOAD_SOCLE) indisponible : voir le message de tools/reload_socle.sh ci-dessus, ou make RELOAD_DIR=…)
endif
endif
endif

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wno-unused-function -Wno-missing-field-initializers
ifneq ($(WERROR),)
CFLAGS  += -Werror
endif
CFLAGS  += $(SANITIZE)
ifneq ($(SANITIZE),)
# Les programmes du banc ne libèrent pas tout en sortant : pas de rapport de fuites
export ASAN_OPTIONS ?= detect_leaks=0
endif
CPPFLAGS += -Isrc -I$(RELOAD_DIR)/src -Iplatforms/pc
# Menu à l'écran : osd.h du socle, grille du Telestrat (960 x 544 : 120 x 34 cases)
CPPFLAGS += -DOSD_COLS=120 -DOSD_ROWS=34
# VIA du socle (via6522, v0.16.29) : compilé avec chaque programme du système
VIA_SRC := $(RELOAD_DIR)/src/chips/via6522.c

BUILD := build
# Disquette système des tests (STRATSED) et de la mesure de charge
STRATSED_DSK ?= $(HOME)/oriclib/games/dsk/STRATSED.DSK
export STRATSED_DSK
# Tests ignorés : chaque script y ajoute une ligne (tests/lib.sh)
SKIP_LOG := $(abspath $(BUILD))/skip.log
export SKIP_LOG
ROMS_H := src/roms/telestrat_roms.h
HEADERS := $(RELOAD_DIR)/src/devices/oric_tape.h $(RELOAD_DIR)/src/devices/oric_tape_rec.h $(RELOAD_DIR)/src/devices/oric_tape_turbo.h src/systems/telestrat.h $(RELOAD_DIR)/src/devices/wd1793.h $(RELOAD_DIR)/src/devices/oric_dsk.h src/devices/telestrat_fdc.h src/devices/mos6551acia.h $(ROMS_H) src/devices/minitel_port.h platforms/pc/line_tcp.h platforms/pc/menu_pc.h src/devices/hayes_line.h src/devices/modem_mux.h src/devices/drive_set.h $(RELOAD_DIR)/src/osd/osd.h src/osd/osd_menu.h src/osd/osd_config.h src/osd/rom_pool.h src/devices/byte_fifo.h src/osd/rom_builtin.h src/devices/printer_out.h src/devices/printer_fx80.h src/devices/plotter_mcp40.h platforms/pc/printer_files.h src/systems/telestrat_state.h $(RELOAD_DIR)/src/chips/via6522.h $(RELOAD_DIR)/src/chips/ay38910psg.h $(RELOAD_DIR)/src/chips/w65c02cpu.h $(RELOAD_DIR)/src/chips/kbd.h $(RELOAD_DIR)/src/chips/clk.h $(RELOAD_DIR)/src/chips/chips_common.h $(RELOAD_DIR)/src/devices/neo_storage.h $(RELOAD_DIR)/platforms/pc/neo_storage_posix.h src/devices/neo_writer.h $(RELOAD_DIR)/src/devices/neo_tnfs.h $(RELOAD_DIR)/platforms/pc/neo_tnfs_udp.h $(wildcard src/*/*.h) platforms/pc/banc_config.h
# Banc PC : fichiers par neo_storage du socle, pilote POSIX (neo_storage_posix.h)
PC_STORAGE := -I$(RELOAD_DIR)/platforms/pc

all: headless $(BUILD)/telestrat_headless_ref $(BUILD)/replay $(BUILD)/printer_render $(BUILD)/test_telestrat

# Générée seulement si absente (le script est en prérequis d'ordre : le modifier ne
# déclenche pas une régénération, qui exige les ROM ou le réseau) ; make roms force.
$(ROMS_H): | tools/fetch_roms.py
	python3 tools/fetch_roms.py --out $(ROMS_H)

roms:
	python3 tools/fetch_roms.py --out $(ROMS_H)

$(BUILD)/telestrat_headless: platforms/pc/telestrat_headless.c $(VIA_SRC) $(HEADERS) platforms/rp2040/src/telestrat_frame.h platforms/rp2040/src/telestrat_video.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(PC_STORAGE) -Iplatforms/rp2040/src $(CFLAGS) -o $@ $< $(VIA_SRC)

$(BUILD)/telestrat_headless_ref: platforms/pc/telestrat_headless.c $(VIA_SRC) $(HEADERS) src/systems/telestrat_ref.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(PC_STORAGE) $(CFLAGS) -DTELESTRAT_REF -o $@ $< $(VIA_SRC)

$(BUILD)/printer_render: platforms/pc/printer_render.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(PC_STORAGE) $(CFLAGS) -o $@ $<

$(BUILD)/replay: tests/replay.c $(VIA_SRC) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(VIA_SRC)

$(BUILD)/test_telestrat: tests/test_telestrat.c $(VIA_SRC) $(HEADERS) platforms/rp2040/src/telestrat_video.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Iplatforms/rp2040/src $(CFLAGS) -o $@ $< $(VIA_SRC)

# Tests de la revue de code (docs/CRITIQUE.md) : un programme par zone,
# tests/test_revue_<zone>.c, même environnement que test_telestrat
REVUE_SRC := $(wildcard tests/test_revue_*.c)
REVUE_BIN := $(patsubst tests/%.c,$(BUILD)/%,$(REVUE_SRC))
$(BUILD)/test_revue_%: tests/test_revue_%.c $(VIA_SRC) $(HEADERS) platforms/rp2040/src/telestrat_video.h | $(BUILD)
	$(CC) $(CPPFLAGS) -Iplatforms/rp2040/src $(CFLAGS) -o $@ $< $(VIA_SRC)

# SingleStepTests de Tom Harte (docs/TESTS.md) sur le cœur du socle
# ($(RELOAD_DIR)/src/chips/w65c02cpu.h) ; ignorés sans données
# ($(RELOAD_DIR)/tools/cputest/fetch_harte.sh).
HARTE_DIR ?= $(HOME)/.cache/65x02
$(BUILD)/harte_c02: $(RELOAD_DIR)/tools/cputest/harte.c $(RELOAD_DIR)/src/chips/w65c02cpu.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DUSE_65C02 -o $@ $<

# Remise à zéro du journal des tests ignorés, une fois par appel de make
skip-reset:
	@mkdir -p $(BUILD)
	@: > $(SKIP_LOG)

cpu_harte: skip-reset $(BUILD)/harte_c02
	@mkdir -p $(BUILD)
	@if [ -d $(HARTE_DIR)/wdc65c02/v1 ]; then $(BUILD)/harte_c02 -x 5c $(HARTE_DIR)/wdc65c02/v1 > $(BUILD)/harte_c02.log; \
	  r=$$?; tail -1 $(BUILD)/harte_c02.log; exit $$r; \
	else echo "SKIP cpu_harte : données absentes ($(HARTE_DIR))"; echo "cpu_harte : données absentes ($(HARTE_DIR))" >> $(SKIP_LOG); fi

$(BUILD):
	mkdir -p $@

headless: $(BUILD)/telestrat_headless

TESTS_SH := tests/test_boot.sh tests/test_telematic.sh tests/test_minitel_emul.sh tests/test_rs232.sh \
    tests/test_menu.sh tests/test_tape.sh tests/test_stratoric.sh tests/test_printer.sh tests/test_state.sh \
    tests/test_profiles.sh tests/test_replay.sh tests/test_golden.sh tests/test_tnfs.sh

# Chaque script reçoit les binaires dont il a besoin (voir son en-tête)
define LANCE_SCRIPTS
set -e; for t in $(TESTS_SH); do \
	  b=$$(basename $$t .sh); \
	  case $$b in \
	    test_printer) a="$(BUILD)/telestrat_headless $(BUILD)/printer_render";; \
	    test_replay) a="$(BUILD)/telestrat_headless_ref $(BUILD)/replay $(BUILD)/telestrat_headless";; \
	    *) a="$(BUILD)/telestrat_headless";; \
	  esac; \
	  echo "sh $$t $$a"; sh $$t $$a; \
	done
endef

# Bilan des tests ignorés (SKIP) : STRICT=1 les transforme en échec
define BILAN_SKIP
n=$$(grep -c . $(SKIP_LOG) 2>/dev/null || true); \
	if [ "$${n:-0}" -gt 0 ]; then \
	  echo "== $$n test(s) IGNORÉ(S) (SKIP) :"; sed 's/^/   SKIP /' $(SKIP_LOG); \
	  if [ -n "$(STRICT)" ]; then echo "STRICT=1 : un test ignoré fait échouer"; exit 1; fi; \
	else echo "== aucun test ignoré"; fi
endef

test: cpu_harte $(BUILD)/test_telestrat $(REVUE_BIN) $(BUILD)/telestrat_headless $(BUILD)/telestrat_headless_ref $(BUILD)/replay $(BUILD)/printer_render
	$(BUILD)/test_telestrat
	@set -e; for t in $(REVUE_BIN); do $$t; done
	@$(LANCE_SCRIPTS)
	python3 tests/test_carte.py
	@$(BILAN_SKIP)

# Test unitaire et banc sous AddressSanitizer + UBSan (-O1) ; mêmes scripts que
# make test, dans un sous-répertoire de BUILD (ni cpu_harte ni programmes de revue)
ASAN_BUILD := $(BUILD)/asan
ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -O1 -g
test-asan:
	$(MAKE) BUILD=$(ASAN_BUILD) SANITIZE="$(ASAN_FLAGS)" test-asan-run

test-asan-run: skip-reset $(BUILD)/test_telestrat $(BUILD)/telestrat_headless $(BUILD)/telestrat_headless_ref $(BUILD)/replay $(BUILD)/printer_render
	$(BUILD)/test_telestrat
	@$(LANCE_SCRIPTS)
	@$(BILAN_SKIP)

compile_flags.txt: Makefile tools/SOCLE
	printf -- '-Isrc\n-I$(RELOAD_DIR)/src\n-Iplatforms/pc\n-I$(RELOAD_DIR)/platforms/pc\n-std=c11\n-DCHIPS_IMPL\n-DOSD_COLS=120\n-DOSD_ROWS=34\n' > $@

uf2: $(ROMS_H)
	@# Socle changé (autre RELOAD_DIR) : les caches CMake (sous-projets du SDK
	@# compris) gardent l'ancien chemin ; dossier recréé (témoin .reload_dir)
	@if [ -d $(BUILD)/rp2040 ] && [ "$$(cat $(BUILD)/rp2040/.reload_dir 2>/dev/null)" != "$(RELOAD_DIR)" ]; then \
	    echo "uf2 : socle changé, $(BUILD)/rp2040 recréé"; rm -rf $(BUILD)/rp2040; fi
	cmake -S platforms/rp2040 -B $(BUILD)/rp2040 -DRELOAD_DIR=$(RELOAD_DIR) $(if $(or $(NEO_SLOT_TELESTRAT),$(ROMS_CLE)),-DTELESTRAT_ROMS_CLE=ON,-DTELESTRAT_ROMS_CLE=OFF) $(if $(NEO_SLOT_TELESTRAT),-DNEO_MULTIBOOT_DIR=$(NEO_MULTIBOOT_DIR) -DNEO_SLOT_TELESTRAT=$(NEO_SLOT_TELESTRAT))
	echo '$(RELOAD_DIR)' > $(BUILD)/rp2040/.reload_dir
	$(MAKE) -C $(BUILD)/rp2040 -j8 telestrat
	@ls -l $(BUILD)/rp2040/telestrat.uf2

clean:
	rm -rf $(BUILD)

.PHONY: all headless test test-asan test-asan-run cpu_harte skip-reset uf2 clean roms

# Charge du RP2040 sans carte (docs/PERFORMANCE.md) : trace du banc PC rejouée
# par la cible ARM telestrat_bench dans un émulateur Cortex-M0+ (python3 +
# unicorn + capstone : PYTHON=chemin/vers/python d'un venv qui les a).
PYTHON ?= python3
CHARGE_DSK ?= $(STRATSED_DSK)
charge: $(BUILD)/telestrat_headless uf2
	@test -f $(CHARGE_DSK) || { echo "charge : disquette $(CHARGE_DSK) absente (CHARGE_DSK=… ou STRATSED_DSK=…)"; exit 1; }
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
