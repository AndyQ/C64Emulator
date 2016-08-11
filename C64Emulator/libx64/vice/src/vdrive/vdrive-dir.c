/*
 * vdrive-dir.c - Virtual disk-drive implementation.
 *                Directory specific functions.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Ingo Korb <ingo@akana.de>  
 *
 * Based on old code by
 *  Teemu Rantanen <tvr@cs.hut.fi>
 *  Jarkko Sonninen <sonninen@lut.fi>
 *  Jouko Valta <jopi@stekt.oulu.fi>
 *  Olaf Seibert <rhialto@mbfys.kun.nl>
 *  Andr� Fachat <a.fachat@physik.tu-chemnitz.de>
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  pottendo <pottendo@gmx.net>
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

/* #define DEBUG_DRIVE */

#include <stdio.h>
#include <string.h>

#include "cbmdos.h"
#include "diskconstants.h"
#include "diskimage.h"
#include "lib.h"
#include "log.h"
#include "types.h"
#include "vdrive-bam.h"
#include "vdrive-dir.h"
#include "vdrive.h"


static log_t vdrive_dir_log = LOG_ERR;


void vdrive_dir_init(void)
{
    vdrive_dir_log = log_open("VDriveDIR");
}

/* Returns the interleave for directory sectors of a given image type */
static int vdrive_dir_get_interleave(unsigned int type)
{
    /* Note: Values for all drives determined empirically */
    switch (type) {
    case VDRIVE_IMAGE_FORMAT_1541:
    case VDRIVE_IMAGE_FORMAT_2040:
    case VDRIVE_IMAGE_FORMAT_1571:
    case VDRIVE_IMAGE_FORMAT_8050:
    case VDRIVE_IMAGE_FORMAT_8250:
        return 3;
    case VDRIVE_IMAGE_FORMAT_1581:
    case VDRIVE_IMAGE_FORMAT_4000:
        return 1;
    default:
        log_error(LOG_ERR,
                  "Unknown disk type %i.  Using interleave 3.", type);
        return 3;
    }
}

static unsigned int vdrive_dir_name_match(BYTE *slot, BYTE *nslot, int length,
                                          int type)
{
    if (length < 0) {
        if (slot[SLOT_TYPE_OFFSET])
            return 0;
        else
            return 1;
    }

    if (!slot[SLOT_TYPE_OFFSET])
        return 0;

    if (type != CBMDOS_FT_DEL && type != (slot[SLOT_TYPE_OFFSET] & 0x07))
        return 0;

    return cbmdos_parse_wildcard_compare(nslot, &slot[SLOT_NAME_OFFSET]);
}

void vdrive_dir_free_chain(vdrive_t *vdrive, int t, int s)
{
    BYTE buf[256];

    while (t) {
        /* Check for illegal track or sector.  */
        if (disk_image_check_sector(vdrive->image, t, s) < 0)
            break;

        /* Check if this sector is really allocated.  */
        if (!vdrive_bam_free_sector(vdrive->image_format, vdrive->bam, t, s))
            break;

        /* FIXME: This seems to be redundant.  AB19981124  */
        vdrive_bam_free_sector(vdrive->image_format, vdrive->bam, t, s);
        disk_image_read_sector(vdrive->image, buf, t, s);
        t = (int)buf[0];
        s = (int)buf[1];
    }
}

/* Tries to allocate the given track/sector and link it */
/* to the current directory sector of vdrive.           */
/* Returns NULL if the allocation failed.               */
static BYTE *find_next_directory_sector(vdrive_t *vdrive, unsigned int track,
                                        unsigned int sector)
{
    if (vdrive_bam_allocate_sector(vdrive->image_format, vdrive->bam, track,
        sector)) {
        vdrive->Dir_buffer[0] = track;
        vdrive->Dir_buffer[1] = sector;
        disk_image_write_sector(vdrive->image, vdrive->Dir_buffer,
                                vdrive->Curr_track, vdrive->Curr_sector);
#ifdef DEBUG_DRIVE
        log_debug("Found (%d %d) TR = %d SE = %d.",
                  track, sector, vdrive->Curr_track, vdrive->Curr_sector);
#endif
        vdrive->SlotNumber = 0;
        memset(vdrive->Dir_buffer, 0, 256);
        vdrive->Dir_buffer[1] = 0xff;
        vdrive->Curr_sector = sector;
        return vdrive->Dir_buffer;
    }
    return NULL;
}


void vdrive_dir_create_slot(bufferinfo_t *p, char *realname,
                            int reallength, int filetype)
{
    p->slot = lib_calloc(1, 32);
    memset(p->slot + SLOT_NAME_OFFSET, 0xa0, 16);
    memcpy(p->slot + SLOT_NAME_OFFSET, realname, reallength);
#ifdef DEBUG_DRIVE
    log_debug("DIR: Created dir slot. Name (%d) '%s'\n", reallength, realname);
#endif
    p->slot[SLOT_TYPE_OFFSET] = filetype;       /* unclosed */

    vdrive_alloc_buffer(p, BUFFER_SEQUENTIAL);
    p->bufptr = 2;
    return;
}

/*
 * vdrive_dir_remove_slot() is called from vdrive_open() (in 'save and
 * replace') and from ip_execute() for 'SCRATCH'.
 */

void vdrive_dir_remove_slot(vdrive_t *vdrive, BYTE *slot)
{
    unsigned int tmp;
    int t, s;

    /* Find slot.  */
    for (tmp = 0; (tmp < 16) && slot[SLOT_NAME_OFFSET + tmp] != 0xa0; tmp++);

    vdrive_dir_find_first_slot(vdrive,
                               (char *)&slot[SLOT_NAME_OFFSET], tmp,
                               slot[SLOT_TYPE_OFFSET] & 0x07);

    /* If slot found, remove.  */
    if (vdrive_dir_find_next_slot(vdrive)) {

        /* Free all sector this file is using.  */
        t = (int) vdrive->Dir_buffer[vdrive->SlotNumber * 32
            + SLOT_FIRST_TRACK];
        s = (int) vdrive->Dir_buffer[vdrive->SlotNumber * 32
            + SLOT_FIRST_SECTOR];

        vdrive_dir_free_chain(vdrive, t, s);

        /* Free side sectors.  */
        t = (int) vdrive->Dir_buffer[vdrive->SlotNumber * 32
            + SLOT_SIDE_TRACK];
        s = (int) vdrive->Dir_buffer[vdrive->SlotNumber * 32
            + SLOT_SIDE_SECTOR];

        vdrive_dir_free_chain(vdrive, t, s);

        /* Update bam */
        vdrive_bam_write_bam(vdrive);

        /* Update directory entry */
        vdrive->Dir_buffer[vdrive->SlotNumber * 32 + SLOT_TYPE_OFFSET] = 0;
        disk_image_write_sector(vdrive->image, vdrive->Dir_buffer,
                                vdrive->Curr_track, vdrive->Curr_sector);
    }
}

/*
   read first dir buffer into Dir_buffer
*/
void vdrive_dir_find_first_slot(vdrive_t *vdrive, const char *name,
                                int length, unsigned int type)
{
    if (length > 0) {
        BYTE *nslot;

        nslot = cbmdos_dir_slot_create(name, length);
        memcpy(vdrive->find_nslot, nslot, CBMDOS_SLOT_NAME_LENGTH);
        lib_free(nslot);
    }

    vdrive->find_length = length;
    vdrive->find_type = type;

    vdrive->Curr_track = vdrive->Header_Track;
    vdrive->Curr_sector = vdrive->Header_Sector;
    vdrive->SlotNumber = 7;

    disk_image_read_sector(vdrive->image, vdrive->Dir_buffer,
                           vdrive->Curr_track, vdrive->Curr_sector);

    vdrive->Dir_buffer[0] = vdrive->Dir_Track;
    vdrive->Dir_buffer[1] = vdrive->Dir_Sector;

#ifdef DEBUG_DRIVE
    log_debug("DIR: vdrive_dir_find_first_slot (curr t:%d/s:%d dir t:%d/s:%d)", 
              vdrive->Curr_track, vdrive->Curr_sector,
              vdrive->Dir_Track, vdrive->Dir_Sector
             );
#endif
}

BYTE *vdrive_dir_find_next_slot(vdrive_t *vdrive)
{
    static BYTE return_slot[32];

#ifdef DEBUG_DRIVE
    log_debug("DIR: vdrive_dir_find_next_slot start (t:%d/s:%d) #%d", vdrive->Curr_track, vdrive->Curr_sector, vdrive->SlotNumber);
#endif
    /*
     * Loop all directory blocks starting from track 18, sector 1 (1541).
     */

    do {
        /*
         * Load next(first) directory block ?
         */

        vdrive->SlotNumber++;

        if (vdrive->SlotNumber >= 8) {
            int status;

            /* end of current directory? */
            if (vdrive->Dir_buffer[0] == 0) {
                break;
            }

            vdrive->SlotNumber = 0;
            vdrive->Curr_track  = (int)vdrive->Dir_buffer[0];
            vdrive->Curr_sector = (int)vdrive->Dir_buffer[1];

            status = disk_image_read_sector(vdrive->image, vdrive->Dir_buffer,
                                            vdrive->Curr_track,
                                            vdrive->Curr_sector);
            if (status != 0) {
                return NULL; /* error */
            }
        }
        if (vdrive_dir_name_match(&vdrive->Dir_buffer[vdrive->SlotNumber * 32],
                                  vdrive->find_nslot, vdrive->find_length,
                                  vdrive->find_type)) {
            memcpy(return_slot, &vdrive->Dir_buffer[vdrive->SlotNumber * 32], 32);
            return return_slot;
        }
    } while (1);

#ifdef DEBUG_DRIVE
    log_debug("DIR: vdrive_dir_find_next_slot (t:%d/s:%d) #%d", vdrive->Curr_track, vdrive->Curr_sector, vdrive->SlotNumber);
#endif

    /*
     * If length < 0, create new directory-entry if possible
     */
    if (vdrive->find_length < 0) {
        int i, sector;
        BYTE *dirbuf;

        sector = vdrive->Curr_sector + vdrive_dir_get_interleave(vdrive->image_format);

        for (i = 0; i < vdrive_get_max_sectors(vdrive, vdrive->Curr_track); i++) {

            dirbuf = find_next_directory_sector(vdrive, vdrive->Curr_track, sector);
            if (dirbuf != NULL) {
                return dirbuf;
            }

            sector++;
            if (sector >= vdrive_get_max_sectors(vdrive, vdrive->Curr_track)) {
                sector = 0;
            }
        }
    }
    return NULL;
}

void vdrive_dir_no_a0_pads(BYTE *ptr, int l)
{
    while (l--) {
        if (*ptr == 0xa0)
            *ptr = 0x20;
        ptr++;
    }
}

int vdrive_dir_next_directory(vdrive_t *vdrive, bufferinfo_t *b);

int vdrive_dir_first_directory(vdrive_t *vdrive, const char *name,
                               int length, int filetype, bufferinfo_t *p)
{
    BYTE *l;

    if (length) {
        if (*name == '$') {
            ++name;
            --length;
        }
        if (*name == ':') {
            ++name;
            --length;
        }
    }
    if (!*name || length < 1) {
        name = "*\0";
        length = 1;
    }

    vdrive_dir_find_first_slot(vdrive, name, length, filetype);

    /*
     * Start Address, Line Link and Line number 0
     */

    l = p->buffer;

    *l++ = 1;
    *l++ = 4;

    *l++ = 1;
    *l++ = 1;

    *l++ = 0;
    *l++ = 0;

    l[25] = 0;

    *l++ = (BYTE)0x12;          /* Reverse on */
    *l++ = '"';

    memcpy(l, vdrive->Dir_buffer + vdrive->bam_name, 16);
    vdrive_dir_no_a0_pads(l, 16);
    l += 16;
    *l++ = '"';
    *l++ = ' ';
    memcpy(l, vdrive->Dir_buffer + vdrive->bam_id, 5);
    vdrive_dir_no_a0_pads(l, 5);

    p->bufptr = 32;

    return vdrive_dir_next_directory(vdrive, p);
}

int vdrive_dir_next_directory(vdrive_t *vdrive, bufferinfo_t *b)
{
    BYTE *l, *p;
    int blocks, i;

    while ((p = vdrive_dir_find_next_slot(vdrive))) {

        if (p[SLOT_TYPE_OFFSET]) {

            l = b->buffer + b->bufptr;

            *l++ = 1;
            *l++ = 1;

            /*
             * Length and spaces
             */
            *l++ = p[SLOT_NR_BLOCKS];
            *l++ = p[SLOT_NR_BLOCKS + 1];

            memset(l, 32, 27);
            l[27] = 0;

            blocks = p[SLOT_NR_BLOCKS] + p[SLOT_NR_BLOCKS + 1] * 256;

            if (blocks < 10)
                l++;
            if (blocks < 100)
                l++;
            l++;

            *l++ = '"';

            memcpy(l, &p[SLOT_NAME_OFFSET], 16);

            for (i = 0; (i < 16) && (p[SLOT_NAME_OFFSET + i] != 0xa0);)
                i++;

            vdrive_dir_no_a0_pads(l, 16);

            l[i] = '"';

            /*
             * Type + End
             * There are 3 spaces or < and 2 spaces after the filetype.
             * Well, not exactly - the whole directory entry is 32 byte long
             * (including nullbyte).
             * Depending on the file size, there are more or less spaces
             */

            l[17] = (p[SLOT_TYPE_OFFSET] & CBMDOS_FT_CLOSED) ? ' ' : '*';
            memcpy(l + 18, cbmdos_filetype_get(p[SLOT_TYPE_OFFSET] & 0x07), 3);
            l[21] = (p[SLOT_TYPE_OFFSET] & CBMDOS_FT_LOCKED) ? '<' : ' ';

            b->bufptr = (b->bufptr + 32) & 255;

            if (b->bufptr == 0)
                return 0;
        }
    }

    blocks = vdrive_bam_free_block_count(vdrive);

    l = b->buffer + b->bufptr;
    *l++ = 1;
    *l++ = 1;
    *l++ = blocks;
    *l++ = blocks >> 8;
    memcpy(l, "BLOCKS FREE.", 12);
    memset(l + 12, 32, 13);
    l[25] = 0;
    l[26] = 0;
    l[27] = 0;
    return b->bufptr + 31;
}

