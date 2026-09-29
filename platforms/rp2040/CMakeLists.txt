cmake_minimum_required(VERSION 3.12)

# Firmware telestrat.uf2 pour Olimex Neo6502. Réutilise le SDK Pico, PicoDVI,
# tinyusb et les sources de plate-forme de reload-emulator (RELOAD_DIR).
#
#   cmake -S platforms/rp2040 -B build/rp2040 -DRELOAD_DIR=~/reload-emulator
#         [-DTELESTRAT_RAM64K=ON] [-DNEO_MULTIBOOT_DIR=... -DNEO_SLOT_TELESTRAT=n]

set(RELOAD_DIR "$ENV{HOME}/reload-emulator" CACHE PATH "reload-emulator (puces, SDK Pico, PicoDVI, tinyusb)")
set(RELOAD_RP2040 ${RELOAD_DIR}/platforms/rp2040)

set(PICO_SDK_PATH ${RELOAD_RP2040}/lib/pico-sdk)
set(PICO_TINYUSB_PATH ${RELOAD_RP2040}/lib/tinyusb)
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
add_subdirectory(${RELOAD_RP2040}/lib/fatfs/source fatfs)

add_executable(telestrat
    ${CMAKE_CURRENT_SOURCE_DIR}/src/telestrat.c
    ${RELOAD_RP2040}/src/hid_app.c
    ${RELOAD_RP2040}/src/audio.c
    ${RELOAD_RP2040}/src/msc_app.c
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
    ${RELOAD_RP2040}/lib/fatfs/source
)

target_compile_options(telestrat PRIVATE -Wall)
option(TELESTRAT_VIDEO_480 "800x480 à 295,2 MHz (1,20 V) au lieu de 960x544 à 372 MHz (1,30 V)" OFF)

# Version affichée par le menu (fichier VERSION du dépôt)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION TELESTRAT_VER LIMIT_COUNT 1)
target_compile_definitions(telestrat PRIVATE TELESTRAT_VERSION="v${TELESTRAT_VER}")

option(TELESTRAT_RS232_UART "prise RS232 possible sur l'UART0 de l'UEXT (TELESTRA.CFG rs232=uext)" ON)

target_compile_definitions(telestrat PRIVATE
    DVI_DEFAULT_SERIAL_CONFIG=olimex_neo6502_cfg
    $<$<BOOL:${TELESTRAT_VIDEO_480}>:TELESTRAT_VIDEO_480>
    $<$<BOOL:${TELESTRAT_RAM64K}>:TELESTRAT_RAM64K>
    $<$<BOOL:${TELESTRAT_RAM64K}>:OSD_MENU_FILES=32>
    $<$<BOOL:${TELESTRAT_RS232_UART}>:TELESTRAT_RS232_UART>
    $<$<NOT:$<BOOL:${TELESTRAT_RAM64K}>>:TELESTRAT_MAX_RAM_BANKS=1>
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
target_link_libraries(telestrat_bench pico_stdlib)
pico_set_float_implementation(telestrat_bench compiler)
pico_set_double_implementation(telestrat_bench compiler)
pico_set_mem_ops_implementation(telestrat_bench compiler)
pico_enable_stdio_uart(telestrat_bench 0)

# Coût d'une ligne affichée par le cœur 1 (tools/osd_cost.py) : menu contre
# image du Telestrat, exécuté dans le même émulateur Cortex-M0+
add_executable(telestrat_osd_bench EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/bench/osd_bench.c)
target_include_directories(telestrat_osd_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(telestrat_osd_bench PRIVATE -Wall -g)
target_link_libraries(telestrat_osd_bench pico_stdlib)
pico_set_mem_ops_implementation(telestrat_osd_bench compiler)
pico_enable_stdio_uart(telestrat_osd_bench 0)
