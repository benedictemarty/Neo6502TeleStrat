#pragma once

// wd1793.h
//
// Contrôleur de disquettes Western Digital WD1793 sur images « MFM_DISK »
// (format d'Oricutron/Euphoric : en-tête de 256 octets « MFM_DISK », nombre de
// faces, de pistes, géométrie ; puis pistes brutes de 6400 octets rangées face
// par face : décalage = 256 + (face * pistes + piste) * 6400).
//
// Écrit d'après la fiche technique du WD1793 (commandes de types I à IV,
// registre d'état). Délais en cycles CPU à 1 MHz : 32 cycles par octet
// (MFM 250 kbit/s), 60 cycles avant le premier octet, 180 entre deux
// secteurs, 20 pour une recherche de piste (valeurs d'Oricutron, qui sert
// d'oracle ; la mécanique réelle est bien plus lente).
//
// Deux modes d'image : en mémoire (wd1793_insert, toute l'image adressable)
// ou « en flux » (wd1793_insert_streamed : la piste courante est chargée dans
// un tampon par un rappel, puis réécrite par un autre si elle a été modifiée ;
// pour la clé USB du Neo6502, l'image de 1 Mo ne tenant pas en RAM).
//
// Pas encore gérés : délais de pas et de rotation réalistes, perte de données
// (LOST DATA), vérification des CRC en lecture, précompensation.
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

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define WD1793_NUM_DRIVES     4
#define WD1793_TRACK_SIZE     6400
#define WD1793_HEADER_SIZE    256
#define WD1793_MAX_SECTORS    32

// Registre d'état
#define WD1793_ST_BUSY      (0x01)
#define WD1793_ST_INDEX     (0x02)  // type I
#define WD1793_ST_DRQ       (0x02)  // types II et III
#define WD1793_ST_TRACK0    (0x04)  // type I
#define WD1793_ST_LOST      (0x04)  // types II et III
#define WD1793_ST_CRC       (0x08)
#define WD1793_ST_SEEK_ERR  (0x10)  // type I
#define WD1793_ST_RNF       (0x10)  // types II et III
#define WD1793_ST_HEAD      (0x20)  // type I
#define WD1793_ST_RECTYPE   (0x20)  // lecture : marque de données effacées
#define WD1793_ST_WPROT     (0x40)
#define WD1793_ST_NOT_READY (0x80)

#define WD1793_DELAY_FIRST_BYTE 60
#define WD1793_DELAY_BYTE       32
#define WD1793_DELAY_NEXT_SEC   180
#define WD1793_DELAY_SEEK       20
#define WD1793_DELAY_END        32
#define WD1793_DELAY_WRITE      500

typedef enum {
    WD1793_OP_NONE = 0,
    WD1793_OP_READ_SECTOR,
    WD1793_OP_WRITE_SECTOR,
    WD1793_OP_READ_ADDRESS,
    WD1793_OP_READ_TRACK,
    WD1793_OP_WRITE_TRACK,
} wd1793_op_t;

// Lecture / écriture de `len` octets à `offset` dans le fichier image
typedef bool (*wd1793_io_t)(void* ctx, uint32_t offset, uint8_t* buf, uint32_t len);

typedef struct {
    uint8_t* image;  // image MFM_DISK complète (mode mémoire)
    wd1793_io_t read;   // mode flux (image == NULL)
    wd1793_io_t write;
    void* ctx;
    bool present;
    size_t size;
    uint8_t sides;
    uint8_t tracks;
    bool write_protect;
    bool modified;
    uint8_t head;  // piste physique sous la tête de ce lecteur
} wd1793_disk_t;

typedef struct {
    // Registres
    uint8_t status;
    uint8_t track;
    uint8_t sector;
    uint8_t data;
    // Sélection (pilotée par l'interface : Microdisc $0314)
    uint8_t drive;
    uint8_t side;
    bool step_in;
    wd1793_disk_t disk[WD1793_NUM_DRIVES];
    // Sorties
    bool intrq;
    bool drq;
    // Opération en cours
    wd1793_op_t op;
    bool multi;
    uint8_t* field;  // secteur : marque de données puis données ; adresse : ID
    int len;
    int offs;
    uint8_t rectype;
    uint16_t crc;
    int32_t drq_delay;
    int32_t int_delay;
    int16_t int_status;  // état chargé à l'échéance de int_delay (-1 : inchangé)
    // Pistes en cache (secteurs trouvés sur la piste courante)
    int cached_drive, cached_side, cached_track;
    int num_sectors;
    int sec_index;
    uint8_t* sec_id[WD1793_MAX_SECTORS];    // pointe sur la marque $FE
    uint8_t* sec_data[WD1793_MAX_SECTORS];  // pointe sur la marque $FB/$F8
    // Mode flux : piste chargée (lecteur, face, piste ; -1 = aucune)
    int buf_drive, buf_side, buf_track;
    bool buf_dirty;
    uint8_t track_buf[WD1793_TRACK_SIZE];
} wd1793_t;

// CRC-CCITT (x^16 + x^12 + x^5 + 1), comme le WD1793
static inline uint16_t wd1793_crc(uint16_t crc, uint8_t value) {
    crc ^= (uint16_t)value << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static inline uint16_t _wd1793_crc_mark(uint8_t mark) {
    uint16_t crc = 0xFFFF;
    crc = wd1793_crc(crc, 0xA1);
    crc = wd1793_crc(crc, 0xA1);
    crc = wd1793_crc(crc, 0xA1);
    return wd1793_crc(crc, mark);
}

static inline uint32_t _wd1793_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void wd1793_reset(wd1793_t* w) {
    wd1793_disk_t disks[WD1793_NUM_DRIVES];
    memcpy(disks, w->disk, sizeof(disks));
    memset(w, 0, sizeof(*w));
    memcpy(w->disk, disks, sizeof(disks));
    for (int i = 0; i < WD1793_NUM_DRIVES; i++) w->disk[i].head = 0;
    w->int_status = -1;
    w->cached_drive = -1;
    w->buf_drive = -1;
    w->sector = 1;
}

// Réécrit la piste tamponnée si elle a été modifiée (mode flux)
static inline bool wd1793_flush(wd1793_t* w) {
    if (!w->buf_dirty || w->buf_drive < 0) return true;
    wd1793_disk_t* d = &w->disk[w->buf_drive];
    w->buf_dirty = false;
    if (!d->write) return false;
    uint32_t off = WD1793_HEADER_SIZE + ((uint32_t)w->buf_side * d->tracks + (uint32_t)w->buf_track) * WD1793_TRACK_SIZE;
    return d->write(d->ctx, off, w->track_buf, WD1793_TRACK_SIZE);
}

static inline bool _wd1793_check_header(const uint8_t* h, size_t size, uint32_t* sides, uint32_t* tracks) {
    if (size < WD1793_HEADER_SIZE || memcmp(h, "MFM_DISK", 8) != 0) return false;
    *sides = _wd1793_le32(h + 8);
    *tracks = _wd1793_le32(h + 12);
    if (*sides < 1 || *sides > 2 || *tracks < 1 || *tracks > 255) return false;
    return size >= WD1793_HEADER_SIZE + (size_t)*sides * *tracks * WD1793_TRACK_SIZE;
}

// Insère une image (vérifie l'en-tête). Retourne false si l'image est invalide.
static inline bool wd1793_insert(wd1793_t* w, int drive, uint8_t* image, size_t size, bool write_protect) {
    if (drive < 0 || drive >= WD1793_NUM_DRIVES) return false;
    wd1793_disk_t* d = &w->disk[drive];
    uint32_t sides, tracks;
    if (!image || !_wd1793_check_header(image, size, &sides, &tracks)) return false;
    if (w->buf_drive == drive) {
        wd1793_flush(w);
        w->buf_drive = -1;
    }
    d->image = image;
    d->read = d->write = NULL;
    d->present = true;
    d->size = size;
    d->sides = (uint8_t)sides;
    d->tracks = (uint8_t)tracks;
    d->write_protect = write_protect;
    d->modified = false;
    if (w->cached_drive == drive) w->cached_drive = -1;
    return true;
}

// Insère une image lue et écrite par rappels (write NULL : protégée)
static inline bool wd1793_insert_streamed(wd1793_t* w, int drive, size_t size, wd1793_io_t read, wd1793_io_t write,
                                          void* ctx) {
    if (drive < 0 || drive >= WD1793_NUM_DRIVES || !read) return false;
    uint8_t header[WD1793_HEADER_SIZE];
    uint32_t sides, tracks;
    if (!read(ctx, 0, header, sizeof(header)) || !_wd1793_check_header(header, size, &sides, &tracks)) return false;
    if (w->buf_drive == drive) {
        wd1793_flush(w);
        w->buf_drive = -1;
    }
    wd1793_disk_t* d = &w->disk[drive];
    d->image = NULL;
    d->read = read;
    d->write = write;
    d->ctx = ctx;
    d->present = true;
    d->size = size;
    d->sides = (uint8_t)sides;
    d->tracks = (uint8_t)tracks;
    d->write_protect = write == NULL;
    d->modified = false;
    if (w->cached_drive == drive) w->cached_drive = -1;
    return true;
}

static inline void wd1793_eject(wd1793_t* w, int drive) {
    if (drive < 0 || drive >= WD1793_NUM_DRIVES) return;
    if (w->buf_drive == drive) {
        wd1793_flush(w);
        w->buf_drive = -1;
    }
    w->disk[drive].image = NULL;
    w->disk[drive].read = w->disk[drive].write = NULL;
    w->disk[drive].present = false;
    if (w->cached_drive == drive) w->cached_drive = -1;
}

static inline bool _wd1793_ready(const wd1793_t* w) { return w->disk[w->drive].present; }

static inline uint8_t* _wd1793_raw_track(wd1793_t* w) {
    wd1793_disk_t* d = &w->disk[w->drive];
    if (!d->present || w->side >= d->sides || d->head >= d->tracks) return NULL;
    uint32_t off = WD1793_HEADER_SIZE + ((uint32_t)w->side * d->tracks + d->head) * WD1793_TRACK_SIZE;
    if (d->image) return d->image + off;
    if (w->buf_drive == w->drive && w->buf_side == w->side && w->buf_track == d->head) return w->track_buf;
    wd1793_flush(w);
    w->buf_drive = -1;
    if (!d->read(d->ctx, off, w->track_buf, WD1793_TRACK_SIZE)) return NULL;
    w->buf_drive = w->drive;
    w->buf_side = w->side;
    w->buf_track = d->head;
    return w->track_buf;
}

// Une écriture a modifié la piste courante
static inline void _wd1793_touch(wd1793_t* w) {
    w->disk[w->drive].modified = true;
    if (!w->disk[w->drive].image) w->buf_dirty = true;
}

// Repère les champs ID ($A1 $A1 $A1 $FE) et données ($FB/$F8) de la piste
static inline void _wd1793_cache_track(wd1793_t* w) {
    wd1793_disk_t* d = &w->disk[w->drive];
    if (w->cached_drive == w->drive && w->cached_side == w->side && w->cached_track == d->head) return;
    w->cached_drive = w->drive;
    w->cached_side = w->side;
    w->cached_track = d->head;
    w->num_sectors = 0;
    w->sec_index = -1;
    uint8_t* t = _wd1793_raw_track(w);
    if (!t) return;
    int i = 0;
    while (i + 10 < WD1793_TRACK_SIZE && w->num_sectors < WD1793_MAX_SECTORS) {
        if (!(t[i] == 0xA1 && t[i + 1] == 0xA1 && t[i + 2] == 0xA1 && t[i + 3] == 0xFE)) {
            i++;
            continue;
        }
        uint8_t* id = &t[i + 3];
        int n = id[4] & 3;
        int j = i + 3 + 7;  // après l'ID et son CRC
        uint8_t* data = NULL;
        // La marque de données suit dans les 43 octets (espace de l'IDAM, fiche WD1793)
        for (int k = 0; k < 64 && j + k < WD1793_TRACK_SIZE; k++) {
            if (t[j + k] == 0xFB || t[j + k] == 0xF8) {
                data = &t[j + k];
                break;
            }
        }
        if (data && (data - t) + 1 + (128 << n) + 2 > WD1793_TRACK_SIZE) data = NULL;
        w->sec_id[w->num_sectors] = id;
        w->sec_data[w->num_sectors] = data;
        w->num_sectors++;
        i = data ? (int)(data - t) + 1 + (128 << n) : j;
    }
}

// Cherche un secteur de la piste courante (au plus deux tours de disque)
static inline int _wd1793_find_sector(wd1793_t* w, uint8_t id_sector) {
    _wd1793_cache_track(w);
    if (w->num_sectors == 0) return -1;
    for (int n = 0; n < 2 * w->num_sectors; n++) {
        w->sec_index = (w->sec_index + 1) % w->num_sectors;
        uint8_t* id = w->sec_id[w->sec_index];
        if (id[3] == id_sector && id[1] == w->track && w->sec_data[w->sec_index]) {
            return w->sec_index;
        }
    }
    return -1;
}

static inline void _wd1793_end(wd1793_t* w, int32_t delay, int16_t status) {
    w->op = WD1793_OP_NONE;
    w->drq = false;
    w->status &= ~WD1793_ST_DRQ;
    if (delay > 0) {
        w->int_delay = delay;
        w->int_status = status;
    } else {
        if (status >= 0) w->status = (uint8_t)status;
        w->intrq = true;
    }
}

static inline void _wd1793_type1(wd1793_t* w, uint8_t cmd, int target) {
    wd1793_disk_t* d = &w->disk[w->drive];
    w->status = WD1793_ST_BUSY | ((cmd & 0x08) ? WD1793_ST_HEAD : 0);
    int dir = 0;
    if (target >= 0) {
        // RESTORE / SEEK : on déplace la tête jusqu'à ce que TR = cible
        dir = target - w->track;
        int head = (int)d->head + dir;
        if (head < 0) head = 0;
        if (head > 255) head = 255;
        d->head = (uint8_t)head;
        w->track = (uint8_t)target;
    } else {
        dir = w->step_in ? 1 : -1;
        int head = (int)d->head + dir;
        if (head < 0) head = 0;
        if (head > 255) head = 255;
        d->head = (uint8_t)head;
        if (cmd & 0x10) w->track = (uint8_t)(w->track + dir);  // bit u : mise à jour de TR
    }
    if (target == 0 && (cmd & 0xF0) == 0x00) {
        // RESTORE : la tête revient sur la piste 0 physique
        d->head = 0;
        w->track = 0;
    }
    uint8_t st = (cmd & 0x08) ? WD1793_ST_HEAD : 0;
    if (!_wd1793_ready(w)) st |= WD1793_ST_NOT_READY;
    if (d->head == 0) st |= WD1793_ST_TRACK0;
    if (cmd & 0x04) {
        // Vérification : la piste lue doit correspondre au registre de piste
        bool found = false;
        _wd1793_cache_track(w);
        for (int i = 0; i < w->num_sectors; i++) {
            if (w->sec_id[i][1] == w->track) found = true;
        }
        if (!found) st |= WD1793_ST_SEEK_ERR;
    }
    if (d->present && d->head >= d->tracks) st |= WD1793_ST_SEEK_ERR;
    if (d->present) st |= WD1793_ST_INDEX;
    if (d->present && d->write_protect) st |= WD1793_ST_WPROT;
    _wd1793_end(w, WD1793_DELAY_SEEK, st);
}

static inline void _wd1793_start_sector(wd1793_t* w, bool write, int32_t delay) {
    int s = _wd1793_find_sector(w, w->sector);
    if (s < 0) {
        _wd1793_end(w, 0, WD1793_ST_RNF);
        return;
    }
    w->field = w->sec_data[s];
    w->len = 128 << (w->sec_id[s][4] & 3);
    w->offs = 0;
    w->rectype = (w->field[0] == 0xF8) ? WD1793_ST_RECTYPE : 0;
    w->crc = _wd1793_crc_mark(write ? 0xFB : w->field[0]);
    w->status = WD1793_ST_BUSY;
    w->drq_delay = delay;
}

static inline void wd1793_command(wd1793_t* w, uint8_t cmd) {
    w->intrq = false;
    if ((cmd & 0xF0) == 0xD0) {
        // Type IV : interruption forcée
        bool busy = w->op != WD1793_OP_NONE;
        w->op = WD1793_OP_NONE;
        w->drq = false;
        w->drq_delay = 0;
        w->int_delay = 0;
        w->int_status = -1;
        if (!busy) {
            // État de type I
            w->status = 0;
            if (!_wd1793_ready(w)) w->status |= WD1793_ST_NOT_READY;
            if (w->disk[w->drive].head == 0) w->status |= WD1793_ST_TRACK0;
        } else {
            w->status &= ~(WD1793_ST_BUSY | WD1793_ST_DRQ);
        }
        if (cmd & 0x0F) w->intrq = true;
        return;
    }
    if (w->status & WD1793_ST_BUSY) return;  // commande ignorée pendant une opération
    switch (cmd & 0xF0) {
        case 0x00: _wd1793_type1(w, cmd, 0); return;        // RESTORE
        case 0x10: _wd1793_type1(w, cmd, w->data); return;  // SEEK
        case 0x20: case 0x30: _wd1793_type1(w, cmd, -1); return;  // STEP
        case 0x40: case 0x50: w->step_in = true; _wd1793_type1(w, cmd, -1); return;
        case 0x60: case 0x70: w->step_in = false; _wd1793_type1(w, cmd, -1); return;
        default: break;
    }
    if (!_wd1793_ready(w)) {
        _wd1793_end(w, 0, WD1793_ST_NOT_READY);
        return;
    }
    w->multi = (cmd & 0x10) != 0;
    switch (cmd & 0xE0) {
        case 0x80:  // READ SECTOR
            w->op = WD1793_OP_READ_SECTOR;
            _wd1793_start_sector(w, false, WD1793_DELAY_FIRST_BYTE);
            break;
        case 0xA0:  // WRITE SECTOR
            if (w->disk[w->drive].write_protect) {
                _wd1793_end(w, 0, WD1793_ST_WPROT);
                break;
            }
            w->op = WD1793_OP_WRITE_SECTOR;
            _wd1793_start_sector(w, true, WD1793_DELAY_WRITE);
            break;
        case 0xC0: {  // READ ADDRESS
            _wd1793_cache_track(w);
            if (w->num_sectors == 0) {
                _wd1793_end(w, 0, WD1793_ST_RNF);
                break;
            }
            w->sec_index = (w->sec_index + 1) % w->num_sectors;
            w->op = WD1793_OP_READ_ADDRESS;
            w->field = w->sec_id[w->sec_index];  // $FE puis 6 octets
            w->len = 6;
            w->offs = 0;
            w->status = WD1793_ST_BUSY;
            w->drq_delay = WD1793_DELAY_FIRST_BYTE;
            break;
        }
        case 0xE0:  // READ TRACK / WRITE TRACK
            w->field = _wd1793_raw_track(w);
            if (!w->field) {
                _wd1793_end(w, 0, WD1793_ST_RNF);
                break;
            }
            if ((cmd & 0x10) && w->disk[w->drive].write_protect) {
                _wd1793_end(w, 0, WD1793_ST_WPROT);
                break;
            }
            w->op = (cmd & 0x10) ? WD1793_OP_WRITE_TRACK : WD1793_OP_READ_TRACK;
            w->len = WD1793_TRACK_SIZE;
            w->offs = 0;
            w->status = WD1793_ST_BUSY;
            w->drq_delay = (cmd & 0x10) ? WD1793_DELAY_WRITE : WD1793_DELAY_FIRST_BYTE;
            if (cmd & 0x10) w->cached_drive = -1;
            break;
        default: break;
    }
}

// Fin d'un secteur : secteur suivant (multi) ou fin de commande
static inline void _wd1793_sector_done(wd1793_t* w) {
    if (w->multi) {
        w->sector++;
        bool write = w->op == WD1793_OP_WRITE_SECTOR;
        int s = _wd1793_find_sector(w, w->sector);
        if (s >= 0) {
            _wd1793_start_sector(w, write, WD1793_DELAY_NEXT_SEC);
            return;
        }
        // Fin de piste : comme Oricutron, fin sans erreur
    }
    if (w->op == WD1793_OP_WRITE_SECTOR) wd1793_flush(w);
    _wd1793_end(w, WD1793_DELAY_END, w->rectype);
}

static inline uint8_t wd1793_read(wd1793_t* w, uint8_t reg) {
    switch (reg & 3) {
        case 0:
            w->intrq = false;
            if (w->op == WD1793_OP_NONE && !(w->status & WD1793_ST_BUSY) && !_wd1793_ready(w)) {
                w->status |= WD1793_ST_NOT_READY;
            }
            return w->status;
        case 1: return w->track;
        case 2: return w->sector;
        default: break;
    }
    if (!w->drq) return w->data;
    w->drq = false;
    w->status &= ~WD1793_ST_DRQ;
    switch (w->op) {
        case WD1793_OP_READ_SECTOR:
            w->data = w->field[1 + w->offs++];
            w->crc = wd1793_crc(w->crc, w->data);
            if (w->offs >= w->len) {
                _wd1793_sector_done(w);
            } else {
                w->drq_delay = WD1793_DELAY_BYTE;
            }
            break;
        case WD1793_OP_READ_ADDRESS:
            w->data = w->field[1 + w->offs++];
            if (w->offs >= w->len) {
                w->sector = w->field[1];  // la piste lue va dans le registre de secteur
                _wd1793_end(w, WD1793_DELAY_END, 0);
            } else {
                w->drq_delay = WD1793_DELAY_BYTE;
            }
            break;
        case WD1793_OP_READ_TRACK:
            w->data = w->field[w->offs++];
            if (w->offs >= w->len) {
                _wd1793_end(w, WD1793_DELAY_END, 0);
            } else {
                w->drq_delay = WD1793_DELAY_BYTE;
            }
            break;
        default: break;
    }
    return w->data;
}

static inline void wd1793_write(wd1793_t* w, uint8_t reg, uint8_t data) {
    switch (reg & 3) {
        case 0: wd1793_command(w, data); return;
        case 1: w->track = data; return;
        case 2: w->sector = data; return;
        default: break;
    }
    w->data = data;
    if (!w->drq) return;
    w->drq = false;
    w->status &= ~WD1793_ST_DRQ;
    switch (w->op) {
        case WD1793_OP_WRITE_SECTOR:
            if (w->offs == 0) w->field[0] = 0xFB;
            w->field[1 + w->offs++] = data;
            w->crc = wd1793_crc(w->crc, data);
            _wd1793_touch(w);
            if (w->offs >= w->len) {
                w->field[1 + w->len] = (uint8_t)(w->crc >> 8);
                w->field[2 + w->len] = (uint8_t)w->crc;
                _wd1793_sector_done(w);
            } else {
                w->drq_delay = WD1793_DELAY_BYTE;
            }
            break;
        case WD1793_OP_WRITE_TRACK: {
            // $F5 : $A1 et remise à zéro du CRC ; $F6 : $C2 ; $F7 : écrit les 2 octets de CRC
            uint8_t b = data;
            if (data == 0xF5) {
                // Le CRC couvre les trois $A1 de synchronisation qui précèdent la marque
                b = 0xA1;
                w->crc = 0xFFFF;
                for (int k = 0; k < 3; k++) w->crc = wd1793_crc(w->crc, 0xA1);
            } else if (data == 0xF6) {
                b = 0xC2;
            } else if (data == 0xF7) {
                uint16_t crc = w->crc;
                w->field[w->offs++] = (uint8_t)(crc >> 8);
                b = (uint8_t)crc;
            } else {
                w->crc = wd1793_crc(w->crc, data);
            }
            if (w->offs < w->len) w->field[w->offs++] = b;
            _wd1793_touch(w);
            if (w->offs >= w->len) {
                wd1793_flush(w);
                _wd1793_end(w, WD1793_DELAY_END, 0);
            } else {
                w->drq_delay = WD1793_DELAY_BYTE;
            }
            break;
        }
        default: break;
    }
}

// Avance de `cycles` cycles CPU
static inline void wd1793_tick(wd1793_t* w, int cycles) {
    if (w->int_delay > 0) {
        w->int_delay -= cycles;
        if (w->int_delay <= 0) {
            w->int_delay = 0;
            if (w->int_status >= 0) w->status = (uint8_t)w->int_status;
            w->int_status = -1;
            w->intrq = true;
        }
    }
    if (w->drq_delay > 0) {
        w->drq_delay -= cycles;
        if (w->drq_delay <= 0) {
            w->drq_delay = 0;
            if (w->op != WD1793_OP_NONE) {
                w->drq = true;
                w->status |= WD1793_ST_DRQ | WD1793_ST_BUSY;
            }
        }
    }
}
