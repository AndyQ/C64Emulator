/*
 * drive-snapshot.c - Hardware-level disk drive emulation, snapshot module.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
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
#include <stdlib.h>

#include "archdep.h"
#include "attach.h"
#include "diskconstants.h"
#include "diskimage.h"
#include "drive-snapshot.h"
#include "drive.h"
#include "drivecpu.h"
#include "drivemem.h"
#include "driverom.h"
#include "drivetypes.h"
#include "gcr.h"
#include "iecbus.h"
#include "iecdrive.h"
#include "lib.h"
#include "log.h"
#include "machine-bus.h"
#include "machine-drive.h"
#include "resources.h"
#include "rotation.h"
#include "snapshot.h"
#include "types.h"
#include "vdrive-bam.h"
#include "vdrive-snapshot.h"
#include "zfile.h"


/* Currently the drive snapshot only handles 2 drives.  */

/* Logging.  */
static log_t drive_snapshot_log = LOG_ERR;

static int drive_snapshot_write_image_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_write_gcrimage_module(snapshot_t *s,
                                                unsigned int dnr);
static int drive_snapshot_read_image_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_read_gcrimage_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_write_rom_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_read_rom_module(snapshot_t *s, unsigned int dnr);

/*
This is the format of the DRIVE snapshot module.

Name                 Type   Size   Description

SyncFactor           DWORD  1      sync factor main cpu <-> drive cpu

Accum                DWORD  2
AttachClk            CLOCK  2      write protect handling on attach
BitsMoved            DWORD  2      number of bits moved since last access
ByteReady            BYTE   2      flag: Byte ready
ClockFrequency       BYTE   2      current clock frequency
CurrentHalfTrack     WORD   2      current half track of the r/w head
DetachClk            CLOCK  2      write protect handling on detach
DiskID1              BYTE   2      disk ID1
DiskID2              BYTE   2      disk ID2
ExtendImagePolicy    BYTE   2      Is extending the disk image allowed
FinishByte           BYTE   2      flag: Mode changed, finish byte
GCRHeadOffset        DWORD  2      offset from the begin of the track
GCRRead              BYTE   2      next value to read from disk
GCRWriteValue        BYTE   2      next value to write to disk
IdlingMethod         BYTE   2      What idle methode do we use
LastMode             BYTE   2      flag: Was the last mode read or write
ParallelCableEnabled BYTE   2      flag: Is the parallel cable enabed
ReadOnly             BYTE   2      flag: This disk is read only
RotationLastClk      CLOCK  2
RotationTablePtr     DWORD  2      pointer to the rotation table
                                   (offset to the rotation table is saved)
Type                 DWORD  2      drive type

*/

#define DRIVE_SNAP_MAJOR 1
#define DRIVE_SNAP_MINOR 1

int drive_snapshot_write_module(snapshot_t *s, int save_disks, int save_roms)
{
    int i;
    char snap_module_name[] = "DRIVE";
    snapshot_module_t *m;
    DWORD rotation_table_ptr[DRIVE_NUM];
    BYTE GCR_image[2];
    int drive_true_emulation;
    int sync_factor;
    drive_t *drive;

    resources_get_int("DriveTrueEmulation", &drive_true_emulation);

    if (vdrive_snapshot_module_write(s, drive_true_emulation ? 10 : 8) < 0)
        return -1;

    if (!drive_true_emulation)
        return 0;

    drive_gcr_data_writeback_all();

    rotation_table_get(rotation_table_ptr);

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        GCR_image[i] = (drive->GCR_image_loaded == 0
                       || !save_disks) ? 0 : 1;
    }

    m = snapshot_module_create(s, snap_module_name, DRIVE_SNAP_MAJOR,
                               DRIVE_SNAP_MINOR);
    if (m == NULL)
        return -1;

    resources_get_int("MachineVideoStandard", &sync_factor);

    if (SMW_DW(m, (DWORD)sync_factor) < 0) {
        if (m != NULL)
            snapshot_module_close(m);
        return -1;
    }

    /* TODO: NUM_DRIVES drives instead of 2 */
    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (0
            || SMW_DW(m, (DWORD)(drive->attach_clk)) < 0
            || SMW_B(m, (BYTE)(drive->byte_ready_level)) < 0
            || SMW_B(m, (BYTE)(drive->clock_frequency)) < 0
            || SMW_W(m, (WORD)(drive->current_half_track)) < 0
            || SMW_DW(m, (DWORD)(drive->detach_clk)) < 0
            || SMW_B(m, (BYTE)(drive->diskID1)) < 0
            || SMW_B(m, (BYTE)(drive->diskID2)) < 0
            || SMW_B(m, (BYTE)(drive->extend_image_policy)) < 0
            || SMW_DW(m, (DWORD)(drive->GCR_head_offset)) < 0
            || SMW_B(m, (BYTE)(drive->GCR_read)) < 0
            || SMW_B(m, (BYTE)(drive->GCR_write_value)) < 0
            || SMW_B(m, (BYTE)(drive->idling_method)) < 0
            || SMW_B(m, (BYTE)(drive->parallel_cable)) < 0
            || SMW_B(m, (BYTE)(drive->read_only)) < 0
            || SMW_DW(m, (DWORD)(rotation_table_ptr[i])) < 0
            || SMW_DW(m, (DWORD)(drive->type)) < 0

            /* rotation */
            || SMW_DW(m, (DWORD)(drive->snap_accum)) < 0
            || SMW_DW(m, (DWORD)(drive->snap_rotation_last_clk)) < 0
            || SMW_DW(m, (DWORD)(drive->snap_bit_counter)) < 0
            || SMW_DW(m, (DWORD)(drive->snap_zero_count)) < 0
            || SMW_W(m, (WORD)(drive->snap_last_read_data)) < 0
            || SMW_B(m, (BYTE)(drive->snap_last_write_data)) < 0
            || SMW_DW(m, (BYTE)(drive->snap_seed)) < 0
        ) {
            if (m != NULL)
                snapshot_module_close(m);
            return -1;
          }
    }

    /* new snapshot members */
    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (0
            || SMW_DW(m, (DWORD)(drive->attach_detach_clk)) < 0
        ) {
            if (m != NULL)
                snapshot_module_close(m);
            return -1;
          }
    }

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (0
            || SMW_B(m, (BYTE)(drive->byte_ready_edge)) < 0
            || SMW_B(m, (BYTE)(drive->byte_ready_active)) < 0
        ) {
            if (m != NULL)
                snapshot_module_close(m);
            return -1;
          }
    }

    if (snapshot_module_close(m) < 0)
        return -1;

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (drive->enable) {
            if (drivecpu_snapshot_write_module(drive_context[i], s) < 0)
                return -1;
            if (machine_drive_snapshot_write(drive_context[i], s) < 0)
                return -1;
        }
    }

    if (save_disks) {
        if (GCR_image[0] > 0) {
            if (drive_snapshot_write_gcrimage_module(s, 0) < 0)
                return -1;
        } else {
            if (drive_snapshot_write_image_module(s, 0) < 0)
                return -1;
        }
        if (GCR_image[1] > 0) {
            if (drive_snapshot_write_gcrimage_module(s, 1) < 0)
                return -1;
        } else {
            if (drive_snapshot_write_image_module(s, 1) < 0)
                return -1;
        }
    }

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (save_roms && drive->enable)
            if (drive_snapshot_write_rom_module(s, i) < 0)
                return -1;
    }

    return 0;
}

int drive_snapshot_read_module(snapshot_t *s)
{
    BYTE major_version, minor_version;
    int i;
    snapshot_module_t *m;
    char snap_module_name[] = "DRIVE";
    DWORD rotation_table_ptr[DRIVE_NUM];
    CLOCK attach_clk[DRIVE_NUM];
    CLOCK detach_clk[DRIVE_NUM];
    CLOCK attach_detach_clk[DRIVE_NUM];
    int drive_true_emulation;
    int sync_factor;
    drive_t *drive;
    int dummy;

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL) {
        /* If this module is not found true emulation is off.  */
        resources_set_int("DriveTrueEmulation", 0);
        return 0;
    }

    drive_gcr_data_writeback_all();

    if (major_version > DRIVE_SNAP_MAJOR || minor_version > DRIVE_SNAP_MINOR) {
        log_error(drive_snapshot_log,
                  "Snapshot module version (%d.%d) newer than %d.%d.",
                  major_version, minor_version,
                  DRIVE_SNAP_MAJOR, DRIVE_SNAP_MINOR);
    }

    /* If this module exists true emulation is enabled.  */
    /* XXX drive_true_emulation = 1 */
    resources_set_int("DriveTrueEmulation", 1);

    if (SMR_DW_INT(m, &sync_factor) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    /* TODO: NUM_DRIVES drives instead of 2 */
    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;

        /* Partially read 1.0 snapshots */
        if (major_version == 1 && minor_version == 0) {
            if (0
                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_DW_INT(m, &dummy) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(drive->clock_frequency)) < 0
                || SMR_W_INT(m, &(drive->current_half_track)) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, &(drive->diskID1)) < 0
                || SMR_B(m, &(drive->diskID2)) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_B_INT(m, &dummy) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(drive->idling_method)) < 0
                || SMR_B_INT(m, &dummy) < 0
                || SMR_B_INT(m, &(drive->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(drive->type)) < 0
            ) {
                snapshot_module_close(m);
                return -1;
            }
        } else {

            if (0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(drive->clock_frequency)) < 0
                || SMR_W_INT(m, &(drive->current_half_track)) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, &(drive->diskID1)) < 0
                || SMR_B(m, &(drive->diskID2)) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(drive->idling_method)) < 0
                || SMR_B_INT(m, &(drive->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(drive->type)) < 0

                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW_INT(m, &(drive->snap_bit_counter)) < 0
                || SMR_DW_INT(m, &(drive->snap_zero_count)) < 0
                || SMR_W_INT(m, &(drive->snap_last_read_data)) < 0
                || SMR_B(m, &(drive->snap_last_write_data)) < 0
                || SMR_DW_INT(m, &(drive->snap_seed)) < 0
            ) {
                snapshot_module_close(m);
                return -1;
            }
        }
    }
    
    /* this one is new, so don't test so stay compatible with old snapshots */
    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        SMR_DW(m, &(attach_detach_clk[i]));
    }
    
    /* these are even newer */
    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        SMR_B_INT(m, (int *)&(drive->byte_ready_edge));
        SMR_B_INT(m, (int *)&(drive->byte_ready_active));
    }
    
    snapshot_module_close(m);
    m = NULL;

    rotation_table_set(rotation_table_ptr);

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        drive->GCR_track_start_ptr = (drive->gcr->data
                                     + ((drive->current_half_track / 2 - 1)
                                     * NUM_MAX_BYTES_TRACK));
        if (drive->type != DRIVE_TYPE_1570
            && drive->type != DRIVE_TYPE_1571
            && drive->type != DRIVE_TYPE_1571CR) {
            if (drive->type == DRIVE_TYPE_1581
                || drive->type == DRIVE_TYPE_2000
                || drive->type == DRIVE_TYPE_4000) {
                resources_set_int("MachineVideoStandard", sync_factor);
            } else {
                drive->side = 0;
                resources_set_int("MachineVideoStandard", sync_factor);
            }
        }
    }

    drive = drive_context[0]->drive;
    switch (drive->type) {
      case DRIVE_TYPE_1541:
      case DRIVE_TYPE_1541II:
      case DRIVE_TYPE_1551:
      case DRIVE_TYPE_1570:
      case DRIVE_TYPE_1571:
      case DRIVE_TYPE_1571CR:
      case DRIVE_TYPE_1581:
      case DRIVE_TYPE_2000:
      case DRIVE_TYPE_4000:
      case DRIVE_TYPE_2031:
      case DRIVE_TYPE_1001:
      case DRIVE_TYPE_2040:
      case DRIVE_TYPE_3040:
      case DRIVE_TYPE_4040:
      case DRIVE_TYPE_8050:
      case DRIVE_TYPE_8250:
        drive->enable = 1;
        machine_drive_rom_setup_image(0);
        drivemem_init(drive_context[0], drive->type);
        resources_set_int("Drive8IdleMethod", drive->idling_method);
        driverom_initialize_traps(drive, 1);
        drive_set_active_led_color(drive->type, 0);
        machine_bus_status_drivetype_set(8, 1);
        break;
      case DRIVE_TYPE_NONE:
        drive_disable(drive_context[0]);
        machine_bus_status_drivetype_set(8, 0);
        break;
      default:
        return -1;
    }

    drive = drive_context[1]->drive;
    switch (drive->type) {
      case DRIVE_TYPE_1541:
      case DRIVE_TYPE_1541II:
      case DRIVE_TYPE_1551:
      case DRIVE_TYPE_1570:
      case DRIVE_TYPE_1571:
      case DRIVE_TYPE_1581:
      case DRIVE_TYPE_2000:
      case DRIVE_TYPE_4000:
      case DRIVE_TYPE_2031:
      case DRIVE_TYPE_1001:
        /* drive 1 does not allow dual disk drive */
        drive->enable = 1;
        machine_drive_rom_setup_image(1);
        drivemem_init(drive_context[1], drive->type);
        resources_set_int("Drive9IdleMethod", drive->idling_method);
        driverom_initialize_traps(drive, 1);
        drive_set_active_led_color(drive->type, 1);
        machine_bus_status_drivetype_set(9, 1);
        break;
      case DRIVE_TYPE_NONE:
      case DRIVE_TYPE_8050:
      case DRIVE_TYPE_8250:
        drive_disable(drive_context[1]);
        machine_bus_status_drivetype_set(9, 0);
        break;
      default:
        return -1;
    }

    /* Clear parallel cable before undumping parallel port values.  */
    for (i = 0; i < DRIVE_PC_NUM; i++) {
        parallel_cable_drive_write(i, 0xff, PARALLEL_WRITE, 0);
        parallel_cable_drive_write(i, 0xff, PARALLEL_WRITE, 1);
    }

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (drive->enable) {
            if (drivecpu_snapshot_read_module(drive_context[i], s) < 0)
                return -1;
            if (machine_drive_snapshot_read(drive_context[i], s) < 0)
                return -1;
        }
    }

    if (drive_snapshot_read_image_module(s, 0) < 0
        || drive_snapshot_read_gcrimage_module(s, 0) < 0)
        return -1;
    if (drive_snapshot_read_image_module(s, 1) < 0
        || drive_snapshot_read_gcrimage_module(s, 1) < 0)
        return -1;
    if (drive_snapshot_read_rom_module(s, 0) < 0)
        return -1;
    if (drive_snapshot_read_rom_module(s, 1) < 0)
        return -1;

    for (i = 0; i < 2; i++)
        drive_current_track_size_set(drive_context[i]->drive);

    for (i = 0; i < 2; i++) {
        drive = drive_context[i]->drive;
        if (drive->type != DRIVE_TYPE_NONE) {
            drive_enable(drive_context[i]);
            drive->attach_clk = attach_clk[i];
            drive->detach_clk = detach_clk[i];
            drive->attach_detach_clk = attach_detach_clk[i];
        }
    }

    iec_update_ports_embedded();
    drive_update_ui_status();

    resources_get_int("DriveTrueEmulation", &drive_true_emulation);

    if (vdrive_snapshot_module_read(s, drive_true_emulation ? 10 : 8) < 0)
        return -1;

    return 0;
}

/* -------------------------------------------------------------------- */
/* read/write "normal" disk image snapshot module */

#define IMAGE_SNAP_MAJOR 1
#define IMAGE_SNAP_MINOR 0

/*
 * This image format is pretty simple:
 *
 * WORD Type            Disk image type (1581, 8050, 8250)
 * 256 * blocks(disk image type) BYTE
 *                      disk image
 *
 */

static int drive_snapshot_write_image_module(snapshot_t *s, unsigned int dnr)
{
    char snap_module_name[10];
    snapshot_module_t *m;
    BYTE sector_data[0x100];
    WORD word;
    int track, sector;
    int rc;
    drive_t *drive;

    drive = drive_context[dnr]->drive;

    if (drive->image == NULL)
	    sprintf(snap_module_name, "NOIMAGE%i", dnr);
    else
	    sprintf(snap_module_name, "IMAGE%i", dnr);
    
    m = snapshot_module_create(s, snap_module_name, IMAGE_SNAP_MAJOR,
                               IMAGE_SNAP_MINOR);
    if (m == NULL)
       return -1;

    if (drive->image == NULL) {
        if (snapshot_module_close(m) < 0)
            return -1;

        return 0;
    }

    word = drive->image->type;
    SMW_W(m, word);

    /* we use the return code to step through the tracks. So we do not
       need any geometry info. */
    for (track = 1; ; track++) {
        rc = 0;
        for (sector = 0; ; sector++) {
            rc = disk_image_read_sector(drive->image, sector_data, track,
                                        sector);
            if (rc == 0) {
                SMW_BA(m, sector_data, 0x100);
            } else {
                break;
            }
        }
        if (sector == 0) {
            break;
        }
    }

    if (snapshot_module_close(m) < 0)
        return -1;
    return 0;
}

static int drive_snapshot_read_image_module(snapshot_t *s, unsigned int dnr)
{
    BYTE major_version, minor_version;
    snapshot_module_t *m;
    char snap_module_name[10];
    WORD word;
    char *filename = NULL;
    char *request_str;
    int len = 0;
    FILE *fp;
    BYTE sector_data[0x100];
    int track, sector;
    int rc;
    drive_t *drive;

    drive = drive_context[dnr]->drive;

    sprintf(snap_module_name, "NOIMAGE%i", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m != NULL) {
        file_system_detach_disk(dnr + 8);
        snapshot_module_close(m);
        return 0;
    }

    sprintf(snap_module_name, "IMAGE%i", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL)
        return 0;

    if (major_version > IMAGE_SNAP_MAJOR || minor_version > IMAGE_SNAP_MINOR) {
        log_error(drive_snapshot_log,
                  "Snapshot module version (%d.%d) newer than %d.%d.",
                  major_version, minor_version,
                  IMAGE_SNAP_MAJOR, IMAGE_SNAP_MINOR);
    }

    if (SMR_W(m, &word) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    switch(word) {
      case 1581:
        len = D81_FILE_SIZE;
        break;
      case 8050:
        len = D80_FILE_SIZE;
        break;
      case 8250:
        len = D82_FILE_SIZE;
        break;
      default:
        log_error(drive_snapshot_log,
                  "Snapshot of disk image unknown (type %d)",
                  (int)word);
        snapshot_module_close(m);
        return -1;
    }

    /* create temporary file of the right size */
    fp = archdep_mkstemp_fd(&filename, MODE_WRITE);

    if (fp == NULL) {
        log_error(drive_snapshot_log, "Could not create temporary file!");
        snapshot_module_close(m);
        return -1;
    }

    /* blow up the file to needed size */
    if (fseek(fp, len - 1, SEEK_SET) < 0
        || (fputc(0, fp) == EOF)) {
        log_error(drive_snapshot_log, "Could not create large temporary file");
        fclose(fp);
        lib_free(filename);
        snapshot_module_close(m);
        return -1;
    }

    fclose(fp);
    lib_free(filename);

    if (file_system_attach_disk(dnr + 8, filename) < 0) {
        log_error(drive_snapshot_log, "Invalid Disk Image");
        lib_free(filename);
        snapshot_module_close(m);
        return -1;
    }

    request_str = lib_msprintf("Disk image unit #%d imported from snapshot",
                               dnr + 8);
    zfile_close_action(filename, ZFILE_REQUEST, request_str);
    lib_free(request_str);

    /* we use the return code to step through the tracks. So we do not
       need any geometry info. */
    SMR_BA(m, sector_data, 0x100);
    for (track = 1; ; track++) {
        rc = 0;
        for (sector = 0; ; sector++) {
            rc = disk_image_write_sector(drive->image, sector_data, track,
                                         sector);
            if (rc == 0) {
                SMR_BA(m, sector_data, 0x100);
            } else {
                break;
            }
        }
        if (sector == 0) {
            break;
        }
    }

    vdrive_bam_reread_bam(dnr + 8);

    snapshot_module_close(m);
    m = NULL;

    return 0;
}

/* -------------------------------------------------------------------- */
/* read/write GCR disk image snapshot module */

#define GCRIMAGE_SNAP_MAJOR 1
#define GCRIMAGE_SNAP_MINOR 0

static int drive_snapshot_write_gcrimage_module(snapshot_t *s, unsigned int dnr)
{
    char snap_module_name[10];
    snapshot_module_t *m;
    BYTE *tmpbuf;
    int i;
    drive_t *drive;

    drive = drive_context[dnr]->drive;
    sprintf(snap_module_name, "GCRIMAGE%i", dnr);

    m = snapshot_module_create(s, snap_module_name, GCRIMAGE_SNAP_MAJOR,
                               GCRIMAGE_SNAP_MINOR);
    if (m == NULL)
        return -1;

    tmpbuf = lib_malloc(MAX_TRACKS_1571 * 4);

    for (i = 0; i < MAX_TRACKS_1571; i++) {
        tmpbuf[i * 4] = drive->gcr->track_size[i] & 0xff;
        tmpbuf[i * 4 + 1] = (drive->gcr->track_size[i] >> 8) & 0xff;
        tmpbuf[i * 4 + 2] = (drive->gcr->track_size[i] >> 16) & 0xff;
        tmpbuf[i * 4 + 3] = (drive->gcr->track_size[i] >> 24) & 0xff;
    }

    if (0
        || SMW_BA(m, drive->gcr->data,
            sizeof(drive->gcr->data)) < 0
        || SMW_BA(m, drive->gcr->speed_zone,
            sizeof(drive->gcr->speed_zone)) < 0
        || SMW_BA(m, tmpbuf, MAX_TRACKS_1571 * 4) < 0) {
        if (m != NULL)
            snapshot_module_close(m);
        lib_free(tmpbuf);
        return -1;
    }

    lib_free(tmpbuf);

    if (snapshot_module_close(m) < 0)
        return -1;

    return 0;
}

static int drive_snapshot_read_gcrimage_module(snapshot_t *s, unsigned int dnr)
{
    BYTE major_version, minor_version;
    snapshot_module_t *m;
    char snap_module_name[10];
    BYTE *tmpbuf;
    int i;
    drive_t *drive;

    drive = drive_context[dnr]->drive;
    sprintf(snap_module_name, "GCRIMAGE%i", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL)
        return 0;

    if (major_version > GCRIMAGE_SNAP_MAJOR
        || minor_version > GCRIMAGE_SNAP_MINOR) {
        log_error(drive_snapshot_log,
                  "Snapshot module version (%d.%d) newer than %d.%d.",
                  major_version, minor_version,
                  GCRIMAGE_SNAP_MAJOR, GCRIMAGE_SNAP_MINOR);
    }

    tmpbuf = lib_malloc(MAX_TRACKS_1571 * 4);

    if (0
        || SMR_BA(m, drive->gcr->data,
            sizeof(drive->gcr->data)) < 0
        || SMR_BA(m, drive->gcr->speed_zone,
            sizeof(drive->gcr->speed_zone)) < 0
        || SMR_BA(m, tmpbuf, MAX_TRACKS_1571 * 4) < 0) {

        if (m != NULL)
            snapshot_module_close(m);
        lib_free(tmpbuf);
        return -1;
    }

    snapshot_module_close(m);
    m = NULL;

    for (i = 0; i < MAX_TRACKS_1571; i++)
        drive->gcr->track_size[i] = tmpbuf[i * 4] + (tmpbuf[i * 4 + 1] << 8)
                                    + (tmpbuf[i * 4 + 2] << 16)
                                    + (tmpbuf[i * 4 + 3] << 24);

    lib_free(tmpbuf);

    drive->GCR_image_loaded = 1;
    drive->image = NULL;

    return 0;
}

/* -------------------------------------------------------------------- */

#define ROM_SNAP_MAJOR 1
#define ROM_SNAP_MINOR 0

static int drive_snapshot_write_rom_module(snapshot_t *s, unsigned int dnr)
{
    char snap_module_name[10];
    snapshot_module_t *m;
    BYTE *base;
    int len;
    drive_t *drive;

    drive = drive_context[dnr]->drive;

    sprintf(snap_module_name, "DRIVEROM%i", dnr);

    m = snapshot_module_create(s, snap_module_name, ROM_SNAP_MAJOR,
                               ROM_SNAP_MINOR);
    if (m == NULL)
       return -1;

    switch (drive->type) {
      case DRIVE_TYPE_1541:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM1541_SIZE;
        break;
      case DRIVE_TYPE_1541II:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM1541II_SIZE;
        break;
      case DRIVE_TYPE_1551:
        base = drive->rom;
        len = DRIVE_ROM1551_SIZE;
        break;
      case DRIVE_TYPE_1570:
        base = drive->rom;
        len = DRIVE_ROM1571_SIZE;
        break;
      case DRIVE_TYPE_1571:
        base = drive->rom;
        len = DRIVE_ROM1571_SIZE;
        break;
      case DRIVE_TYPE_1571CR:
        base = drive->rom;
        len = DRIVE_ROM1571_SIZE;
        break;
      case DRIVE_TYPE_1581:
        base = drive->rom;
        len = DRIVE_ROM1581_SIZE;
        break;
      case DRIVE_TYPE_2000:
        base = drive->rom;
        len = DRIVE_ROM2000_SIZE;
        break;
      case DRIVE_TYPE_4000:
        base = drive->rom;
        len = DRIVE_ROM4000_SIZE;
        break;
      case DRIVE_TYPE_2031:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM2031_SIZE;
        break;
      case DRIVE_TYPE_2040:
        base = &(drive->rom[DRIVE_ROM_SIZE - DRIVE_ROM2040_SIZE]);
        len = DRIVE_ROM2040_SIZE;
        break;
      case DRIVE_TYPE_3040:
        base = &(drive->rom[DRIVE_ROM_SIZE - DRIVE_ROM3040_SIZE]);
        len = DRIVE_ROM3040_SIZE;
        break;
      case DRIVE_TYPE_4040:
        base = &(drive->rom[DRIVE_ROM_SIZE - DRIVE_ROM4040_SIZE]);
        len = DRIVE_ROM4040_SIZE;
        break;
      case DRIVE_TYPE_1001:
      case DRIVE_TYPE_8050:
      case DRIVE_TYPE_8250:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM1001_SIZE;
        break;
      default:
        return -1;
    }

    if (SMW_BA(m, base, len) < 0) {
        if (m != NULL)
            snapshot_module_close(m);
        return -1;
    }
    if (snapshot_module_close(m) < 0)
        return -1;
    return 0;
}

static int drive_snapshot_read_rom_module(snapshot_t *s, unsigned int dnr)
{
    BYTE major_version, minor_version;
    snapshot_module_t *m;
    char snap_module_name[10];
    BYTE *base;
    int len;
    drive_t *drive;

    drive = drive_context[dnr]->drive;

    sprintf(snap_module_name, "DRIVEROM%i", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL)
        return 0;

    if (major_version > ROM_SNAP_MAJOR || minor_version > ROM_SNAP_MINOR) {
        log_error(drive_snapshot_log,
                  "Snapshot module version (%d.%d) newer than %d.%d.",
                  major_version, minor_version,
                  ROM_SNAP_MAJOR, ROM_SNAP_MINOR);
    }

    switch (drive->type) {
      case DRIVE_TYPE_1541:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM1541_SIZE;
        break;
      case DRIVE_TYPE_1541II:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM1541II_SIZE;
        break;
      case DRIVE_TYPE_1551:
        base = drive->rom;
        len = DRIVE_ROM1551_SIZE;
        break;
      case DRIVE_TYPE_1570:
        base = drive->rom;
        len = DRIVE_ROM1571_SIZE;
        break;
      case DRIVE_TYPE_1571:
        base = drive->rom;
        len = DRIVE_ROM1571_SIZE;
        break;
      case DRIVE_TYPE_1571CR:
        base = drive->rom;
        len = DRIVE_ROM1571_SIZE;
        break;
      case DRIVE_TYPE_1581:
        base = drive->rom;
        len = DRIVE_ROM1581_SIZE;
        break;
      case DRIVE_TYPE_2000:
        base = drive->rom;
        len = DRIVE_ROM2000_SIZE;
        break;
      case DRIVE_TYPE_4000:
        base = drive->rom;
        len = DRIVE_ROM4000_SIZE;
        break;
      case DRIVE_TYPE_2031:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM2031_SIZE;
        break;
      case DRIVE_TYPE_2040:
        base = &(drive->rom[DRIVE_ROM_SIZE - DRIVE_ROM2040_SIZE]);
        len = DRIVE_ROM2040_SIZE;
        break;
      case DRIVE_TYPE_3040:
        base = &(drive->rom[DRIVE_ROM_SIZE - DRIVE_ROM3040_SIZE]);
        len = DRIVE_ROM3040_SIZE;
        break;
      case DRIVE_TYPE_4040:
        base = &(drive->rom[DRIVE_ROM_SIZE - DRIVE_ROM4040_SIZE]);
        len = DRIVE_ROM4040_SIZE;
        break;
      case DRIVE_TYPE_1001:
      case DRIVE_TYPE_8050:
      case DRIVE_TYPE_8250:
        base = &(drive->rom[0x4000]);
        len = DRIVE_ROM1001_SIZE;
        break;
      default:
        return -1;
    }

    if (SMR_BA(m, base, len) < 0) {
        if (m != NULL)
            snapshot_module_close(m);
        return -1;
    }

    machine_drive_rom_do_checksum(dnr);

    snapshot_module_close(m);

    return 0;
}

