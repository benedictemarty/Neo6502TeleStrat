// usb_msc.c — clé USB (stockage de masse) : montage FatFs et accès aux secteurs
//
// Remplace msc_app.c de reload-emulator pour le Telestrat. Différence : le
// montage (f_mount, qui lit des secteurs) ne se fait plus dans le rappel de
// fin d'INQUIRY, appelé de l'intérieur de tuh_task alors que le pilote MSC de
// TinyUSB n'a pas fini l'échange en cours (la lecture suivante y est
// refusée : montage en échec, vu sur carte le 2026-09-29), mais dans la
// boucle principale (usb_msc_poll). Les lectures et écritures de secteurs
// attendent la fin du transfert en appelant tuh_task, avec un délai maximal.
//
// ## Licence zlib/libpng
//
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

#include <stdio.h>

#include "tusb.h"
#include "pico/time.h"
#include "ff.h"
#include "diskio.h"

#define MSC_IO_TIMEOUT_US 1000000  // transfert abandonné au-delà

static FATFS msc_volumes[CFG_TUH_DEVICE_MAX + 1];
static volatile bool msc_busy[CFG_TUH_DEVICE_MAX + 1];
static volatile bool msc_ok[CFG_TUH_DEVICE_MAX + 1];
static scsi_inquiry_resp_t msc_inquiry_resp;
static volatile uint8_t msc_pending;  // adresse d'une clé à monter (0 : aucune)

bool msc_inquiry_complete = false;  // clé montée (lu par telestrat.c)
volatile int msc_mount_result = -1; // dernier résultat de f_mount (diagnostic SWD)
// Diagnostic SWD des accès aux secteurs : demandes refusées par TinyUSB,
// délais dépassés, réponses en erreur (CSW), réussies
volatile uint32_t msc_diag[4];

static bool msc_inquiry_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data) {
    if (cb_data->csw->status != 0) {
        printf("USB : INQUIRY en échec\n");
        return false;
    }
    msc_pending = dev_addr;  // monté par usb_msc_poll, hors de tuh_task
    return true;
}

void tuh_msc_mount_cb(uint8_t dev_addr) {
    tuh_msc_inquiry(dev_addr, 0, &msc_inquiry_resp, msc_inquiry_cb, 0);
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
    char path[3] = {(char)('0' + dev_addr), ':', 0};
    f_unmount(path);
    if (msc_pending == dev_addr) msc_pending = 0;
}

// Boucle principale : montage de la clé annoncée
void usb_msc_poll(void) {
    const uint8_t dev_addr = msc_pending;
    if (!dev_addr) return;
    msc_pending = 0;
    if (dev_addr >= FF_VOLUMES) {
        printf("USB : clé à l'adresse %u, au-delà des %d volumes FatFs\n", dev_addr, FF_VOLUMES);
        return;
    }
    char path[3] = {(char)('0' + dev_addr), ':', 0};
    const FRESULT r = f_mount(&msc_volumes[dev_addr], path, 1);
    msc_mount_result = r;
    if (r != FR_OK) {
        printf("USB : montage en échec (%d)\n", r);
        return;
    }
    f_chdrive(path);
    f_chdir("/");
    printf("USB : clé %.8s %.16s montée\n", msc_inquiry_resp.vendor_id, msc_inquiry_resp.product_id);
    msc_inquiry_complete = true;
}

static bool msc_io_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data) {
    msc_ok[dev_addr] = cb_data->csw->status == 0;
    msc_busy[dev_addr] = false;
    return true;
}

static bool msc_wait(BYTE pdrv) {
    const uint32_t start = time_us_32();
    while (msc_busy[pdrv]) {
        tuh_task();
        if (time_us_32() - start > MSC_IO_TIMEOUT_US) {
            msc_busy[pdrv] = false;
            msc_diag[1]++;
            printf("USB : délai dépassé\n");
            return false;
        }
    }
    msc_diag[msc_ok[pdrv] ? 3 : 2]++;
    return msc_ok[pdrv];
}

DSTATUS disk_status(BYTE pdrv) { return tuh_msc_mounted(pdrv) ? 0 : STA_NODISK; }

DSTATUS disk_initialize(BYTE pdrv) { return tuh_msc_mounted(pdrv) ? 0 : STA_NOINIT; }

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    msc_busy[pdrv] = true;
    if (!tuh_msc_read10(pdrv, 0, buff, (uint32_t)sector, (uint16_t)count, msc_io_cb, 0)) {
        msc_busy[pdrv] = false;
        msc_diag[0]++;
        return RES_ERROR;
    }
    return msc_wait(pdrv) ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    msc_busy[pdrv] = true;
    if (!tuh_msc_write10(pdrv, 0, buff, (uint32_t)sector, (uint16_t)count, msc_io_cb, 0)) {
        msc_busy[pdrv] = false;
        return RES_ERROR;
    }
    return msc_wait(pdrv) ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    switch (cmd) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT: *(LBA_t*)buff = tuh_msc_get_block_count(pdrv, 0); return RES_OK;
        case GET_SECTOR_SIZE: *(WORD*)buff = (WORD)tuh_msc_get_block_size(pdrv, 0); return RES_OK;
        case GET_BLOCK_SIZE: *(DWORD*)buff = 1; return RES_OK;
        default: return RES_PARERR;
    }
}
