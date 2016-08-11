/*
 * fdd.c - (M)FM floppy disk drive emulation
 *
 * Written by
 *  Kajtar Zsolt <soci@c64.rulez.org>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>
#include <string.h>

#include "diskimage.h"
#include "lib.h"
#include "types.h"
#include "fdd.h"
#include "diskimage.h"
#include "drive.h"

const int fdd_data_rates[4] = {500, 300, 250, 1000}; /* kbit/s */
#define INDEXLEN (16)
static void fdd_flush_raw(fd_drive_t *drv);
static WORD *crc1021 = NULL;

struct fd_drive_s {
    int number;
    int disk_change; /* out signal */
    int write_protect; /* out signal */
    int track; /* physical head position */
    int tracks; /* total tracks in image */
    int head; /* head selected */
    int sectors; /* physical sectors per track */
    int motor; /* in signal */
    int rate; /* in signal */
    int sector_size; /* physical sector size */
    int iso;
    int gap2; /* size of gap 2 */
    int gap3; /* size of gap 3 */
    int head_invert;
    int disk_rate;
    int image_sectors;
    int index_count;
    drive_t *drive;
    struct disk_image_s *image;
    struct {
        int head;
        int size;
        int track_head;
        int dirty;
        BYTE *data;
        BYTE *sync;
    } raw;
};

fd_drive_t *fdd_init(int num, drive_t *drive)
{
    fd_drive_t *drv = lib_malloc(sizeof(fd_drive_t));
    drv->image = NULL;
    drv->number = num;
    drv->motor = 0;
    drv->track = 0;
    drv->tracks = 80;
    drv->sectors = 10;
    drv->sector_size = 2;
    drv->head_invert = 1;
    drv->disk_change = 1;
    drv->write_protect = 1;
    drv->rate = 2;
    drv->image_sectors = 40;
    drv->drive = drive;
    return drv;
}

static void fdd_init_crc1021(void)
{
    int i, j;
    WORD w;

    crc1021 = lib_malloc(256 * sizeof(WORD));
    for (i = 0; i < 256; i++) {
        w = i << 8;
        for (j = 0; j < 8; j++) {
            if (w & 0x8000) {
                w <<= 1;
                w ^= 0x1021;
            } else {
                w <<= 1;
            }
        }
        crc1021[i] = w;
    }
}

void fdd_shutdown(fd_drive_t *drv)
{
    if (!drv) {
        return;
    }
    lib_free(drv);
}

void fdd_image_attach(fd_drive_t *drv, struct disk_image_s *image)
{
    if (!drv) {
        return;
    }
    drv->image = image;
    switch (image->type) {
    case DISK_IMAGE_TYPE_D1M:
        drv->tracks = 81;
        drv->sectors = 10;
        drv->sector_size = 2;
        drv->head_invert = 1;
        drv->disk_rate = 2;
        drv->iso = 0;
        drv->gap2 = 22;
        drv->gap3 = 35;
        drv->image_sectors = 256;
        break;
    case DISK_IMAGE_TYPE_D2M:
        drv->tracks = 81;
        drv->sectors = 10;
        drv->sector_size = 3;
        drv->head_invert = 1;
        drv->disk_rate = 0;
        drv->iso = 0;
        drv->gap2 = 22;
        drv->gap3 = 100;
        drv->image_sectors = 256;
        break;
    case DISK_IMAGE_TYPE_D4M:
        drv->tracks = 81;
        drv->sectors = 20;
        drv->sector_size = 3;
        drv->head_invert = 1;
        drv->disk_rate = 3;
        drv->iso = 0;
        drv->gap2 = 41;
        drv->gap3 = 100;
        drv->image_sectors = 256;
        break;
    case DISK_IMAGE_TYPE_D81:
    default:
        drv->tracks = 80;
        drv->sectors = 10;
        drv->sector_size = 2;
        drv->head_invert = 1;
        drv->disk_rate = 2;
        drv->iso = 1;
        drv->gap2 = 22;
        drv->gap3 = 35;
        drv->image_sectors = 40;
        break;
    }
    drv->raw.size = 25 * fdd_data_rates[drv->disk_rate];
    drv->raw.data = lib_malloc(drv->raw.size);
    drv->raw.sync = lib_calloc(1, (drv->raw.size + 7) >> 3);
    drv->raw.track_head = -1;
    drv->raw.dirty = 0;
    drv->raw.head = 0;

    drv->disk_change = 1;
    drv->write_protect = image->read_only;
}

void fdd_image_detach(fd_drive_t *drv)
{
    if (!drv) {
        return;
    }
    fdd_flush_raw(drv);
    drv->image = NULL;
    lib_free(drv->raw.data);
    lib_free(drv->raw.sync);
    drv->disk_change = 1;
}

#define fdd_raw_write(b) {\
    drv->raw.data[p] = b; \
    drv->raw.sync[p >> 3] &= 0xff7f >> (p & 7); \
    p++;if (p >= drv->raw.size) p = 0;}

#define fdd_raw_write_sync(b) {\
    drv->raw.data[p] = b; \
    drv->raw.sync[p >> 3] |= 0x80 >> (p & 7); \
    p++;if (p >= drv->raw.size) p = 0;}

inline WORD fdd_crc(WORD crc, BYTE b)
{
    if (!crc1021) {
        fdd_init_crc1021();
    }
    return crc1021[(crc >> 8) ^ b] ^ (crc << 8);
}

static void fdd_flush_raw(fd_drive_t *drv) {
    int i, j, s, p, it, is, step, d;
    BYTE *data;
    WORD w;

    if (!drv->raw.dirty) {
        return;
    }
    drv->raw.dirty = 0;

    if (drv->raw.track_head / 2 < drv->tracks && drv->image) {
#if FDD_DEBUG
        for (i = 0; i < drv->raw.size; i++) {
            if (!(i & 15)) printf("%04x: ", i);
            printf("%02x ", drv->raw.data[i]);
            if ((i & 15)==15) printf("\n");
        }
#endif
        data = lib_malloc(128 << drv->sector_size);
        p = 0;
        for (s = 0; s < drv->sectors; s++) {
            step = 0;
            d = 0;
            for (i = 0; i < drv->raw.size * 2; i++) {
                w = drv->raw.data[p];
                if (drv->raw.sync[p >> 3] & (0x80 >> (p & 7))) {
                    w |= 0x100;
                }
                p++;
                if (p >= drv->raw.size) {
                    p = 0;
                }
                switch (step) {
                case 0:
                    if (w == 0x00) {
                        step++;
                    }
                    continue;
                case 1:
                    if (w == 0x00) {
                        continue;
                    }
                    if (w == 0x1a1) {
                        step++;
                        continue;
                    }
                    break;
                case 2:
                    if (w == 0x1a1) {
                        continue;
                    }
                    if (w == 0xfe) {
                        step++;
                        continue;
                    }
                    break;
                case 3:
                    if (w == drv->raw.track_head / 2) {
                        step++;
                        continue;
                    }
                    break;
                case 4:
                    if (w == ((drv->raw.track_head & 1) ^ drv->head_invert)) {
                        step++;
                        continue;
                    }
                    break;
                case 5:
                    if (w == s + 1) {
                        step++;
                        continue;
                    }
                    break;
                case 6:
                    if (w == drv->sector_size) {
                        step++;
                        continue;
                    }
                    break;
                case 7:
                    step++;
                    continue;
                case 8:
                    step++;
                    continue;
                case 9:
                    if (w == 0x00) {
                        step++;
                    }
                    continue;
                case 10:
                    if (w == 0x00) {
                        continue;
                    }
                    if (w == 0x1a1) {
                        step++;
                        continue;
                    }
                    step = 9;
                    continue;
                case 11:
                    if (w == 0x1a1) {
                        continue;
                    }
                    if (w == 0xfb) {
                        step++;
                        continue;
                    }
                    break;
                case 12:
                    data[d++] = w;
                    if (d >= (128 << drv->sector_size)) {
                        step++;
                    }
                    continue;
                case 13:
                    step++;
                    continue;
                case 14:

                    is = (drv->raw.track_head ^ drv->head_invert) * drv->sectors + s;
                    is <<= drv->sector_size - 1;
                    it = is / drv->image_sectors + 1;
                    is = is % drv->image_sectors;

                    for (j = 0; j < (1 << drv->sector_size); j += 2) {
                        disk_image_write_sector(drv->image, data + j * 128, it, is);
                        is = (is + 1) % drv->image_sectors;
                        if (!is) {
                            it++;
                        }
                    }
                    i = drv->raw.size * 2;
                }
                step = 0;
            }
        }
        lib_free(data);
    }
}

static void fdd_update_raw(fd_drive_t *drv) {
    int i, j, s, p, it, is, res;
    BYTE buffer[256];
    WORD crc;

    if (drv->track * 2 + drv->head == drv->raw.track_head) {
        return;
    }
    if (drv->raw.dirty) {
        fdd_flush_raw(drv);
    }
    drv->raw.track_head = drv->track * 2 + drv->head;

    memset(drv->raw.data, 0x4e, drv->raw.size);
    memset(drv->raw.sync, 0, (drv->raw.size + 7) >> 3);

    if (drv->track < drv->tracks && drv->image) {

        i = (drv->track * 2 + (drv->head ^ drv->head_invert)) * drv->sectors;
        i <<= drv->sector_size - 1;
        it = i / drv->image_sectors + 1;
        is = i % drv->image_sectors;

        if (drv->iso) {
            p = 32; /* GAP 4a */
        } else {
            p = 80;
            for (i = 0; i < 12; i++) { /* Sync */
                fdd_raw_write(0x00);
            }
            for (i = 0; i < 3; i++) {
                fdd_raw_write_sync(0xa1);
            }
            fdd_raw_write(0xfc); /* Index mark */
            for (i = 0; i < 50; i++) {
                fdd_raw_write(0x4e);
            }
        }
        for (s = 0; s < drv->sectors; s++) {
            for (i = 0; i < 12; i++) { /* Sync */
                fdd_raw_write(0x00);
            }
            for (i = 0; i < 3; i++) {
                fdd_raw_write_sync(0xa1);
            }
            fdd_raw_write(0xfe); /* ID mark */
            fdd_raw_write(drv->track);
            crc = fdd_crc(0xb230, drv->track);
            fdd_raw_write(drv->head ^ drv->head_invert);
            crc = fdd_crc(crc, drv->head ^ drv->head_invert);
            fdd_raw_write(s + 1);
            crc = fdd_crc(crc, s + 1);
            fdd_raw_write(drv->sector_size);
            crc = fdd_crc(crc, drv->sector_size);
            fdd_raw_write(crc >> 8);
            fdd_raw_write(crc);
            for (i = 0; i < drv->gap2; i++) {
                fdd_raw_write(0x4e);
            }
            crc = 0xe295;
            for (j = 0; j < (1 << drv->sector_size); j += 2) {
                res = disk_image_read_sector(drv->image, buffer, it, is);
                if (res < 0) {
                    return;
                }
                if (j == 0) {
                    for (i = 0; i < 12; i++) { /* Sync */
                        fdd_raw_write(0x00);
                    }
                    for (i = 0; i < 3; i++) {
                        fdd_raw_write_sync(0xa1);
                    }
                    fdd_raw_write(0xfb); /* Data mark */
                }
                for (i = 0; i < 256; i++) {
                    fdd_raw_write(buffer[i]);
                    crc = fdd_crc(crc, buffer[i]);
                }
                is = (is + 1) % drv->image_sectors;
                if (!is) {
                    it++;
                }
            }
            fdd_raw_write(crc >> 8);
            fdd_raw_write(crc & 0xff);
            for (i = 0; i < drv->gap3; i++) {
                fdd_raw_write(0x4e); /* GAP 3 */
            }
        }
#if FDD_DEBUG
        for (i = 0; i < drv->raw.size; i++) {
            if (!(i & 15)) printf("%04x: ", i);
            printf("%02x ", drv->raw.data[i]);
            if ((i & 15)==15) printf("\n");
        }
#endif
    }
}

int fdd_rotate(fd_drive_t *drv, int bytes)
{
    if (!drv || !drv->motor || !drv->image) {
        return bytes;
    }
    drv->index_count += (drv->raw.head + bytes) / drv->raw.size;
    drv->raw.head = (drv->raw.head + bytes) % drv->raw.size;
    return bytes;
}

inline int fdd_index(fd_drive_t *drv)
{
    if (!drv) {
        return 0;
    }
    return (drv->raw.head < INDEXLEN) ? 1 : 0;
}

inline void fdd_index_count_reset(fd_drive_t *drv)
{
    if (!drv) {
        return;
    }
    drv->index_count = 0;
}

inline int fdd_index_count(fd_drive_t *drv)
{
    if (!drv) {
        return 0;
    }
    return drv->index_count;
}

inline int fdd_track0(fd_drive_t *drv)
{
    if (!drv) {
        return 0;
    }
    return drv->track ? 0 : 1;
}

inline int fdd_write_protect(fd_drive_t *drv)
{
    if (!drv) {
        return 0;
    }
    return drv->write_protect;
}

inline int fdd_disk_change(fd_drive_t *drv)
{
    if (!drv) {
        return 0;
    }
    return drv->disk_change;
}

WORD fdd_read(fd_drive_t *drv)
{
    WORD data;
    int p;

    if (!drv || !drv->motor) {
        return 0;
    }
    p = drv->raw.head;
    if (drv->disk_rate == drv->rate) {
        fdd_update_raw(drv);

        data = (WORD)drv->raw.data[p];
        if (drv->raw.sync[p >> 3] & (0x80 >> (p & 7))) {
            data |= 0x100;
        }
    } else {
        data = 0;
    }
    p++;
    if (p >= drv->raw.size) {
        p = 0;
        drv->index_count++;
    }
    drv->raw.head = p;
    return data;
}

int fdd_write(fd_drive_t *drv, WORD data)
{
    int p;

    if (!drv || !drv->motor) {
        return -1;
    }
    fdd_update_raw(drv);

    p = drv->raw.head;
    if (drv->disk_rate == drv->rate) {
        drv->raw.data[p] = (BYTE)data;
        if (data & 0x100) {
            drv->raw.sync[p >> 3] |= 0x80 >> (p & 7);
        } else {
            drv->raw.sync[p >> 3] &= 0xff7f >> (p & 7);
        }
        drv->raw.dirty = 1;
    }
    p++;
    if (p >= drv->raw.size) {
        p = 0;
        drv->index_count++;
    }
    drv->raw.head = p;
    return 0;
}

void fdd_flush(fd_drive_t *drv)
{
    if (!drv) {
        return;
    }
    fdd_flush_raw(drv);
}

void fdd_seek_pulse(fd_drive_t *drv, int dir)
{
    if (!drv) {
        return;
    }
    if (drv->motor) {
        drv->track += dir ? 1 : -1;
    }
    if (drv->image) {
        drv->disk_change = 0;
    }
    if (drv->track < 0) drv->track = 0;
    if (drv->track > 82) drv->track = 82;
    drv->drive->current_half_track = (drv->track + 1) * 2;
}

void fdd_select_head(fd_drive_t *drv, int head)
{
    if (!drv) {
        return;
    }
    drv->head = head & 1;
}

void fdd_set_motor(fd_drive_t *drv, int motor)
{
    if (!drv) {
        return;
    }
    drv->motor = motor & 1;
}

void fdd_set_rate(fd_drive_t *drv, int rate)
{
    if (!drv) {
        return;
    }
    drv->rate = rate & 3;
}
