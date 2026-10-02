cmake_minimum_required(VERSION 3.12)

# Firmware telestrat.uf2 pour Olimex Neo6502. Réutilise le SDK Pico, PicoDVI,
# tinyusb et les sources de plate-forme de reload-emulator (RELOAD_DIR).
#
#   cmake -S platforms/rp2040 -B build/rp2040 [-DRELOAD_DIR=~/.cache/reload-socle/socle-2026-10-01-26]
#         [-DTELESTRAT_RAM64K=ON] [-DNEO_MULTIBOOT_DIR=... -DNEO_SLOT_TELESTRAT=n]

# Par défaut : le socle reload épinglé (tools/reload_socle.sh, comme le Makefile)
set(RELOAD_DIR "$ENV{HOME}/.cache/reload-socle/socle-2026-10-01-26" CACHE PATH "reload-emulator (puces, SDK Pico, PicoDVI, tinyusb)")
if(NOT EXISTS "${RELOAD_DIR}/platforms/rp2040/lib/pico-sdk/pico_sdk_init.cmake")
    message(FATAL_ERROR "reload-emulator absent de ${RELOAD_DIR} : lancer tools/reload_socle.sh (ou -DRELOAD_DIR=...)")
endif()
set(RELOAD_RP2040 ${RELOAD_DIR}/platforms/rp2040)

set(PICO_SDK_PATH ${RELOAD_RP2040}/lib/pico-sdk)
# TinyUSB 0.21.0 (sous-module third_party/tinyusb) à la place de celui de
# reload-emulator (2023) : son pilote hôte RP2040 refondu (hathach/tinyusb#3561)
# lit la clé USB, seule ou avec un clavier ; l'ancien bloquait sur le premier
# READ10 de 512 octets (vu sur carte le 2026-09-29).
set(TELESTRAT_TINYUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/tinyusb CACHE PATH "TinyUSB")
if (NOT EXISTS ${TELESTRAT_TINYUSB_DIR}/src/tusb.h)
    message(FATAL_ERROR "TinyUSB absent : git submodule update --init third_party/tinyusb")
endif()
set(PICO_TINYUSB_PATH ${TELESTRAT_TINYUSB_DIR})
set(PICO_BOARD olimex_neo6502)

include(${RELOAD_RP2040}/pico_sdk_import.cmake)

project(neo6502telestrat C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

option(TELESTRAT_RAM64K "Cartouche RAM 64 Ko (banques 1-4) à la place de TELE-ASS/TELEMATIC" OFF)

# Multi-boot Neo6502 (Neo6502firmware/multiboot), comme reload-emulator
set(NEO_MULTIBOOT_DIR "" CACHE PATH "Neo6502firmware/multiboot (memmap_slot_N.ld)")
set(NEO_SLOT_TELESTRAT "" CACHE STRING "Slot de flash multi-boot (vide = image seule)")

add_subdirectory(${RELOAD_RP2040}/lib/PicoDVI/software/libdvi libdvi)
# FatFs : copie du projet (third_party/fatfs), ffconf.h réglé pour la RAM
# (FF_FS_TINY, noms longs de 64 caractères)
add_library(fatfs INTERFACE)
target_sources(fatfs INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/fatfs/ff.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/fatfs/ffsystem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/fatfs/ffunicode.c
)
target_include_directories(fatfs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/fatfs)

add_executable(telestrat
    ${CMAKE_CURRENT_SOURCE_DIR}/src/telestrat.c
    ${RELOAD_RP2040}/src/hid_app.c  # socle : clavier, manettes, touches multimédia
    ${RELOAD_RP2040}/src/audio.c
    ${RELOAD_RP2040}/src/msc_app.c  # socle : montage de la clé hors de tuh_task (msc_poll)
    ${RELOAD_RP2040}/src/neo_storage_fatfs.c  # socle : volume 0 de neo_storage (la clé) sur FatFs
    ${RELOAD_RP2040}/src/neo_cdc_serial.c  # socle : ports série USB du modem (ligne, TNFS)
)


# Ordre : nos en-têtes (dont tusb_config.h avec le CDC hôte), puis la plate-forme
# RP2040 de reload (chips/wdc65C02cpu.h),
# puis les puces communes de reload.
target_include_directories(telestrat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${RELOAD_RP2040}/src
    ${RELOAD_DIR}/src
    ${RELOAD_RP2040}/lib/PicoDVI/software/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/fatfs
)

target_compile_options(telestrat PRIVATE -Wall)
option(TELESTRAT_VIDEO_480 "800x480 à 295,2 MHz (1,20 V) au lieu de 960x544 à 372 MHz (1,30 V)" OFF)

# Version affichée par le menu (fichier VERSION du dépôt)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION TELESTRAT_VER LIMIT_COUNT 1)
target_compile_definitions(telestrat PRIVATE TELESTRAT_VERSION="v${TELESTRAT_VER}")
# Menu à l'écran : osd.h du socle, grille du Telestrat (960 x 544 : 120 x 34 cases)
set(TELESTRAT_OSD_GRID OSD_COLS=120 OSD_ROWS=34)
target_compile_definitions(telestrat PRIVATE ${TELESTRAT_OSD_GRID})

# VIA du socle (via6522, v0.16.29) : timer 1 juste par pas de 4 cycles (N + 2),
# mode paresseux. Placement (TELESTRAT_VIA6522_PLACE) :
#   RAM     : tout en RAM (défaut, variante standard ; sur carte 59 %, contre
#             64 % en flash : le modèle de make charge sous-estime la flash) ;
#   PARTIEL : chemin de chaque pas en RAM (VIA6522_HOT : via_advance,
#             via_update…), accès aux registres en flash (VIA6522_HOT_ACCESS)
#             (défaut, variante RAM 64 Ko, où tout en RAM déborde ; tampon
#             audio de 1 Ko pour la place) ;
#   FLASH   : tout en flash (sur carte en RAM 64 Ko : 113 %, ne tient pas).
# (L'ancienne mos6522via, copie figée, est supprimée depuis v0.16.32.)
if(TELESTRAT_RAM64K)
    set(TELESTRAT_VIA6522_PLACE PARTIEL CACHE STRING "placement de via6522 : RAM, PARTIEL ou FLASH")
    target_compile_definitions(telestrat PRIVATE SAMPLES_BUFFER_SIZE=1024)
else()
    set(TELESTRAT_VIA6522_PLACE RAM CACHE STRING "placement de via6522 : RAM, PARTIEL ou FLASH")
endif()
set(_via_ram "VIA6522_HOT=__attribute__((section(\".time_critical.via\")))")
if(TELESTRAT_VIA6522_PLACE STREQUAL "FLASH")
    set(TELESTRAT_VIA_DEFS "")
elseif(TELESTRAT_VIA6522_PLACE STREQUAL "PARTIEL")
    set(TELESTRAT_VIA_DEFS ${_via_ram} "VIA6522_HOT_ACCESS=")
else()
    set(TELESTRAT_VIA_DEFS ${_via_ram})
endif()
target_sources(telestrat PRIVATE ${RELOAD_DIR}/src/chips/via6522.c)
if(TELESTRAT_VIA_DEFS)
    target_compile_definitions(telestrat PRIVATE ${TELESTRAT_VIA_DEFS})
endif()

option(TELESTRAT_RS232_UART "prise RS232 possible sur l'UART0 de l'UEXT (TELESTRA.CFG rs232=uext)" ON)

target_compile_definitions(telestrat PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=olimex_neo6502_cfg
    # Une seule clé montée, un seul FATFS (588 octets) en lecteur « 0: »
    MSC_VOLUMES=1
    # Fichiers ouverts en même temps (FIL de neo_storage_fatfs.c, 88 octets
    # chacun) : 4 lecteurs, cassette lue, cassette enregistrée, imprimante
    # texte, plus un pour les actions du menu, TELESTRA.CFG ou la sonde
    # (ouverts l'un après l'autre) ; plus la page ou le tracé en cours (FX-80,
    # MCP-40) hors de la variante RAM 64 Ko
    $<$<BOOL:${TELESTRAT_RAM64K}>:NEO_FATFS_FILES=8>
    $<$<NOT:$<BOOL:${TELESTRAT_RAM64K}>>:NEO_FATFS_FILES=9>
    $<$<BOOL:${TELESTRAT_VIDEO_480}>:TELESTRAT_VIDEO_480>
    $<$<BOOL:${TELESTRAT_RAM64K}>:TELESTRAT_RAM64K>
    $<$<BOOL:${TELESTRAT_RAM64K}>:OSD_MENU_FILES=16>
    $<$<BOOL:${TELESTRAT_RAM64K}>:DIAG_TX_SIZE=128>
    $<$<BOOL:${TELESTRAT_RAM64K}>:BYTE_FIFO_SIZE=64>
    $<$<BOOL:${TELESTRAT_RS232_UART}>:TELESTRAT_RS232_UART>
    $<$<NOT:$<BOOL:${TELESTRAT_RAM64K}>>:TELESTRAT_MAX_RAM_BANKS=1>
    # Tas réservé à l'édition des liens (au lieu des 2 Ko par défaut) : dvi_init
    # alloue DVI_N_TMDS_BUFFERS tampons de 3 x 480 mots (17 280 octets pour 3),
    # plus ses files, et malloc (newlib) demande la mémoire par pages : mesuré
    # sur carte, il faut ~21,8 Ko (21 204 échouent, 21 800 passent). Un manque
    # fait échouer l'édition des liens (« region RAM overflowed ») au lieu de
    # planter la carte au démarrage (« Out of memory » dans dvi_init)
    $<$<NOT:$<BOOL:${TELESTRAT_RAM64K}>>:PICO_HEAP_SIZE=0x5800>
    $<$<NOT:$<BOOL:${TELESTRAT_RAM64K}>>:OSD_MENU_FILES=40>
        $<$<BOOL:${TELESTRAT_RAM64K}>:PICO_HEAP_SIZE=0x5800>
)

target_link_libraries(telestrat
    pico_stdlib
    pico_multicore
    pico_util
    libdvi
    tinyusb_host
    fatfs
)

# Image disque intégrée à la flash (lecture seule), insérée dans le lecteur A
# tant qu'aucune clé USB ne fournit de .dsk : -DTELESTRAT_FLASH_DISK=chemin.dsk
set(TELESTRAT_FLASH_DISK "" CACHE FILEPATH "image MFM_DISK intégrée à la flash (vide : aucune)")
if (NOT "${TELESTRAT_FLASH_DISK}" STREQUAL "")
    set(FLASH_DISK_H ${CMAKE_CURRENT_BINARY_DIR}/telestrat_flash_disk.h)
    add_custom_command(OUTPUT ${FLASH_DISK_H}
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/bin2h.py ${TELESTRAT_FLASH_DISK} ${FLASH_DISK_H} telestrat_flash_disk
        DEPENDS ${TELESTRAT_FLASH_DISK} ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/bin2h.py)
    target_sources(telestrat PRIVATE ${FLASH_DISK_H})
    target_include_directories(telestrat PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(telestrat PRIVATE TELESTRAT_FLASH_DISK_H)
    message(STATUS "telestrat : image disque en flash ${TELESTRAT_FLASH_DISK}")
endif()

pico_add_extra_outputs(telestrat)
pico_enable_stdio_usb(telestrat 0)
# Messages sur l'UART0 de l'UEXT (GPIO 28/29, carte olimex_neo6502), sauf si
# TELESTRA.CFG y met la prise RS232 (rs232=uext)
pico_enable_stdio_uart(telestrat 1)

if (NOT "${NEO_SLOT_TELESTRAT}" STREQUAL "")
    if ("${NEO_MULTIBOOT_DIR}" STREQUAL "")
        message(FATAL_ERROR "NEO_MULTIBOOT_DIR doit pointer sur Neo6502firmware/multiboot")
    endif()
    pico_set_linker_script(telestrat ${NEO_MULTIBOOT_DIR}/memmap_slot_${NEO_SLOT_TELESTRAT}.ld)
    target_compile_definitions(telestrat PRIVATE NEO_MULTIBOOT_SLOT=${NEO_SLOT_TELESTRAT})
    message(STATUS "telestrat lié pour le slot multi-boot ${NEO_SLOT_TELESTRAT}")
endif()

# Mesure de charge sans carte (tools/rp2040_load.py) : jamais flashé, exécuté
# dans un émulateur Cortex-M0+ ; flottants et mem* du compilateur (pas de ROM
# de démarrage dans l'émulateur), diviseur matériel conservé.
add_executable(telestrat_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/telestrat_bench.c)
# BENCH_CHIPS_DIR : puces (chips/*.h) d'une version figée de reload, pour
# mesurer indépendamment des modifications en cours dans RELOAD_DIR
set(BENCH_CHIPS_DIR "" CACHE PATH "répertoire contenant chips/ (vide : RELOAD_DIR/src)")
target_include_directories(telestrat_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    ${BENCH_CHIPS_DIR}
    ${RELOAD_DIR}/src
)
target_compile_options(telestrat_bench PRIVATE -Wall -g)
target_compile_definitions(telestrat_bench PRIVATE ${TELESTRAT_OSD_GRID})
# Même VIA, même placement que le firmware (charge mesurée à l'identique)
target_sources(telestrat_bench PRIVATE ${RELOAD_DIR}/src/chips/via6522.c)
if(TELESTRAT_VIA_DEFS)
    target_compile_definitions(telestrat_bench PRIVATE ${TELESTRAT_VIA_DEFS})
endif()
target_link_libraries(telestrat_bench pico_stdlib)
pico_set_float_implementation(telestrat_bench compiler)
pico_set_double_implementation(telestrat_bench compiler)
pico_set_mem_ops_implementation(telestrat_bench compiler)
pico_enable_stdio_uart(telestrat_bench 0)

# Coût d'une ligne affichée par le cœur 1 (tools/osd_cost.py) : menu contre
# image du Telestrat, exécuté dans le même émulateur Cortex-M0+
add_executable(telestrat_osd_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/osd_bench.c)
target_include_directories(telestrat_osd_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src ${CMAKE_CURRENT_SOURCE_DIR}/src ${RELOAD_DIR}/src)
target_compile_definitions(telestrat_osd_bench PRIVATE ${TELESTRAT_OSD_GRID})
target_compile_options(telestrat_osd_bench PRIVATE -Wall -g)
target_link_libraries(telestrat_osd_bench pico_stdlib)
pico_set_mem_ops_implementation(telestrat_osd_bench compiler)
pico_enable_stdio_uart(telestrat_osd_bench 0)
