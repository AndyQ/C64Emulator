/*
 * ide64.c - Cartridge handling, IDE64 cart.
 *
 * Written by
 *  Kajtar Zsolt <soci@c64.rulez.org>
 *
 * Real-Time-Clock patches by
 *  Greg King <greg.king4@verizon.net>
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

/* required for off_t on some platforms */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

/* VAC++ has off_t in sys/stat.h */
#ifdef __IBMC__
#include <sys/stat.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "archdep.h"
#define CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64cartsystem.h"
#undef CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64export.h"
#include "cartio.h"
#include "cartridge.h"
#include "cmdline.h"
#include "ds1202_1302.h"
#include "ide64.h"
#include "log.h"
#include "lib.h"
#include "machine.h"
#include "resources.h"
#include "snapshot.h"
#include "translate.h"
#include "types.h"
#include "util.h"
#include "vicii-phi1.h"
#include "ata.h"
#include "monitor.h"
#include "crt.h"

#ifdef IDE64_DEBUG
#define debug(x) log_debug(x)
#else
#define debug(x)
#endif

/* Current IDE64 bank */
static int current_bank;

/* Current memory config */
static int current_cfg;

/* ds1302 context */
static rtc_ds1202_1302_t *ds1302_context = NULL;

/*  */
static BYTE kill_port;

static struct drive_s {
    ata_drive_t *drv;
    char *filename;
    ata_drive_geometry_t settings;
    int autodetect_size;
    ata_drive_type_t type;
    ata_drive_geometry_t detected;
    int update_needed;
} drives[4];

static int idrive = 0;

/* communication latch */
static WORD out_d030, in_d030, idebus;

/* config ram */
static char ide64_DS1302[65];

static char *ide64_configuration_string = NULL;

static int settings_version4, rtc_offset_res;
static time_t rtc_offset;

/* ---------------------------------------------------------------------*/

/* some prototypes are needed */
static void ide64_idebus_store(WORD addr, BYTE value);
static BYTE ide64_idebus_read(WORD addr);
static BYTE ide64_idebus_peek(WORD addr);
static int ide64_idebus_dump(void);
static void ide64_io_store(WORD addr, BYTE value);
static BYTE ide64_io_read(WORD addr);
static BYTE ide64_io_peek(WORD addr);
static int ide64_io_dump(void);
static void ide64_ft245_store(WORD addr, BYTE value);
static BYTE ide64_ft245_read(WORD addr);
static BYTE ide64_ft245_peek(WORD addr);
static void ide64_ds1302_store(WORD addr, BYTE value);
static BYTE ide64_ds1302_read(WORD addr);
static BYTE ide64_ds1302_peek(WORD addr);
static void ide64_rom_store(WORD addr, BYTE value);
static BYTE ide64_rom_read(WORD addr);
static BYTE ide64_rom_peek(WORD addr);

static io_source_t ide64_idebus_device = {
    CARTRIDGE_NAME_IDE64 " IDE",
    IO_DETACH_CART,
    NULL,
    0xde20, 0xde2f, 0x0f,
    0,
    ide64_idebus_store,
    ide64_idebus_read,
    ide64_idebus_peek,
    ide64_idebus_dump,
    CARTRIDGE_IDE64,
    0,
    0
};

static io_source_t ide64_io_device = {
    CARTRIDGE_NAME_IDE64 " I/O",
    IO_DETACH_CART,
    NULL,
    0xde30, 0xde37, 0x07,
    0,
    ide64_io_store,
    ide64_io_read,
    ide64_io_peek,
    ide64_io_dump,
    CARTRIDGE_IDE64,
    0,
    0
};

static io_source_t ide64_ft245_device = {
    CARTRIDGE_NAME_IDE64 " FT245",
    IO_DETACH_CART,
    NULL,
    0xde5d, 0xde5e, 0x01,
    0,
    ide64_ft245_store,
    ide64_ft245_read,
    ide64_ft245_peek,
    NULL,
    CARTRIDGE_IDE64,
    0,
    0
};

static io_source_t ide64_ds1302_device = {
    CARTRIDGE_NAME_IDE64 " DS1302",
    IO_DETACH_CART,
    NULL,
    0xde5f, 0xde5f, 0x00,
    0,
    ide64_ds1302_store,
    ide64_ds1302_read,
    ide64_ds1302_peek,
    NULL,
    CARTRIDGE_IDE64,
    0,
    0
};

static io_source_t ide64_rom_device = {
    CARTRIDGE_NAME_IDE64 " ROM",
    IO_DETACH_CART,
    NULL,
    0xde60, 0xdeff, 0xff,
    0,
    ide64_rom_store,
    ide64_rom_read,
    ide64_rom_peek,
    NULL,
    CARTRIDGE_IDE64,
    0,
    0
};

static io_source_list_t *ide64_idebus_list_item = NULL;
static io_source_list_t *ide64_io_list_item = NULL;
static io_source_list_t *ide64_ft245_list_item = NULL;
static io_source_list_t *ide64_ds1302_list_item = NULL;
static io_source_list_t *ide64_rom_list_item = NULL;

static const c64export_resource_t export_res[5] = {
    {CARTRIDGE_NAME_IDE64 " IDE", 1, 1, &ide64_idebus_device, NULL, CARTRIDGE_IDE64},
    {CARTRIDGE_NAME_IDE64 " I/O", 1, 1, &ide64_io_device, NULL, CARTRIDGE_IDE64},
    {CARTRIDGE_NAME_IDE64 " FT245", 1, 1, &ide64_ft245_device, NULL, CARTRIDGE_IDE64},
    {CARTRIDGE_NAME_IDE64 " DS1302", 1, 1, &ide64_ds1302_device, NULL, CARTRIDGE_IDE64},
    {CARTRIDGE_NAME_IDE64 " ROM", 1, 1, &ide64_rom_device, NULL, CARTRIDGE_IDE64},
};

static int ide64_register(void)
{
    int i;

    if (ide64_rom_list_item) {
        return 0;
    }

    for (i = 0; i < 5; i++) {
        if (!settings_version4 && i==2) {
            continue;
        }
        if (c64export_add(&export_res[i]) < 0) {
            return -1;
        }
    }

    ide64_idebus_list_item = io_source_register(&ide64_idebus_device);
    ide64_io_list_item = io_source_register(&ide64_io_device);
    if (settings_version4) {
        ide64_ft245_list_item = io_source_register(&ide64_ft245_device);
    }
    ide64_ds1302_list_item = io_source_register(&ide64_ds1302_device);
    ide64_rom_list_item = io_source_register(&ide64_rom_device);
    return 0;
}

static void ide64_unregister(void)
{
    int i;

    if (!ide64_rom_list_item) {
        return;
    }

    for (i = 0; i < 5; i++) {
        if (!settings_version4 && i==2) {
            continue;
        }
        c64export_remove(&export_res[i]);
    }

    io_source_unregister(ide64_idebus_list_item);
    io_source_unregister(ide64_io_list_item);
    if (ide64_ft245_list_item) {
        io_source_unregister(ide64_ft245_list_item);
    }
    io_source_unregister(ide64_ds1302_list_item);
    io_source_unregister(ide64_rom_list_item);
    ide64_idebus_list_item = NULL;
    ide64_io_list_item = NULL;
    ide64_ft245_list_item = NULL;
    ide64_ds1302_list_item = NULL;
    ide64_rom_list_item = NULL;
}

static int set_ide64_config(const char *cfg, void *param)
{
    int i;

    ide64_DS1302[64] = 0;
    memset(ide64_DS1302, 0x40, 64);

    if (cfg) {
        for (i = 0; cfg[i] && i < 64; i++) {
            ide64_DS1302[i] = cfg[i];
        }
    }
    util_string_set(&ide64_configuration_string, ide64_DS1302);
    return 0;
}

static void detect_ide64_image(struct drive_s *drive)
{
    FILE *file;
    unsigned char header[24];
    int res;
    char *ext;
    ata_drive_geometry_t *geometry = &drive->detected;

    if (!ide64_rom_list_item) {
        drive->type = ATA_DRIVE_NONE;
        return;
    }
    debug("IDE64 detect");
    geometry->cylinders = drive->settings.cylinders;
    geometry->heads = drive->settings.heads;
    geometry->sectors = drive->settings.sectors;
    geometry->size = geometry->cylinders * geometry->heads * geometry->sectors;

    if (!drive->filename) {
        drive->type = ATA_DRIVE_NONE;
        return;
    }

    res = strlen(drive->filename);

    if (!res) {
        drive->type = ATA_DRIVE_NONE;
        return;
    }

    drive->type = ATA_DRIVE_CF;
    ext = util_get_extension(drive->filename);
    if (ext) {
        if (!strcasecmp(ext, "cfa")) {
            drive->type = ATA_DRIVE_CF;
        } else if (!strcasecmp(ext, "hdd")) {
            drive->type = ATA_DRIVE_HDD;
        } else if (!strcasecmp(ext, "fdd")) {
            drive->type = ATA_DRIVE_FDD;
        } else if (!strcasecmp(ext, "iso")) {
            drive->type = ATA_DRIVE_CD;
        }
    }

    file = fopen(drive->filename, MODE_READ);

    if (!file) {
        return;
    }

    if (drive->autodetect_size) {

        if (fread(header, 1, 24, file) < 24) {
            memset(&header, 0, sizeof(header));
        }

        if (memcmp(header, "C64-IDE V", 9) == 0) { /* old filesystem always CHS */
            geometry->cylinders = util_be_buf_to_word(&header[0x10]) + 1;
            geometry->heads = (header[0x12] & 0x0f) + 1;
            geometry->sectors = header[0x13];
            geometry->size = geometry->cylinders * geometry->heads * geometry->sectors;
        } else if (memcmp(header + 8, "C64 CFS V", 9) == 0) {
            if (header[0x04] & 0x40) { /* LBA */
                geometry->cylinders = 0;
                geometry->heads = 0;
                geometry->sectors = 0;
                geometry->size = util_be_buf_to_dword(&header[0x04]) & 0x0fffffff;
            } else { /* CHS */
                geometry->cylinders = util_be_buf_to_word(&header[0x05]) + 1;
                geometry->heads = (header[0x04] & 0x0f) + 1;
                geometry->sectors = header[0x07];
                geometry->size = geometry->cylinders * geometry->heads * geometry->sectors;
            }
        } else {
            off_t size = 0;
            if (fseek(file, 0, SEEK_END) == 0) {
                size = ftell(file);
                if (size < 0) size = 0;
            }
            geometry->cylinders = 0;
            geometry->heads = 0;
            geometry->sectors = 0;
            geometry->size = size / ((drive->type == ATA_DRIVE_CD) ? 2048 : 512);
        }
    }

    fclose(file);
    return;
}

static int set_ide64_image_file(const char *name, void *param)
{
    struct drive_s *drive = &drives[vice_ptr_to_int(param)];

    util_string_set(&drive->filename, name);

    if (drive->drv) {
        detect_ide64_image(drive);
        drive->update_needed = ata_image_change(drive->drv, drive->filename, drive->type, drive->detected);
    }
    return 0;
}

static int set_cylinders(int cylinders, void *param)
{
    struct drive_s *drive = &drives[vice_ptr_to_int(param)];

    if (cylinders > 65535 || cylinders < 1) {
        return -1;
    }

    drive->settings.cylinders = cylinders;
    if (drive->drv) {
        drive->update_needed = ata_image_change(drive->drv, drive->filename, drive->type, drive->detected);
    }
    return 0;
}

static int set_heads(int heads, void *param)
{
    struct drive_s *drive = &drives[vice_ptr_to_int(param)];

    if (heads > 16 || heads < 1) {
        return -1;
    }
    drive->settings.heads = heads;
    if (drive->drv) {
        drive->update_needed = ata_image_change(drive->drv, drive->filename, drive->type, drive->detected);
    }
    return 0;
}

static int set_sectors(int sectors, void *param)
{
    struct drive_s *drive = &drives[vice_ptr_to_int(param)];

    if (sectors > 63 || sectors < 1) {
        return -1;
    }
    drive->settings.sectors = sectors;
    if (drive->drv) {
        drive->update_needed = ata_image_change(drive->drv, drive->filename, drive->type, drive->detected);
    }
    return 0;
}

static int set_autodetect_size(int autodetect_size, void *param)
{
    struct drive_s *drive = &drives[vice_ptr_to_int(param)];

    drive->autodetect_size = autodetect_size;
    if (drive->drv) {
        detect_ide64_image(drive);
        drive->update_needed = ata_image_change(drive->drv, drive->filename, drive->type, drive->detected);
    }
    return 0;
}

static int set_version4(int val, void *param)
{
    if (settings_version4 != val) {
        ide64_unregister();
        settings_version4 = val;
        if (ide64_register() < 0) {
            return -1;
        }
        machine_trigger_reset(MACHINE_RESET_MODE_HARD);
    }
    return 0;
}

static int set_rtc_offset(int val, void *param)
{
    rtc_offset_res = val;
    rtc_offset = (time_t)val;

    return 0;
}

static const resource_string_t resources_string[] = {
    { "IDE64Image1", "ide.cfa", RES_EVENT_NO, NULL,
      &drives[0].filename, set_ide64_image_file, (void *)0 },
    { "IDE64Image2", "", RES_EVENT_NO, NULL,
      &drives[1].filename, set_ide64_image_file, (void *)1 },
    { "IDE64Image3", "", RES_EVENT_NO, NULL,
      &drives[2].filename, set_ide64_image_file, (void *)2 },
    { "IDE64Image4", "", RES_EVENT_NO, NULL,
      &drives[3].filename, set_ide64_image_file, (void *)3 },
    { "IDE64Config", "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@", RES_EVENT_NO, NULL,
      &ide64_configuration_string, set_ide64_config, NULL },
    { NULL }
};

static const resource_int_t resources_int[] = {
    { "IDE64Cylinders1", 256,
      RES_EVENT_NO, NULL,
      (int *)&drives[0].settings.cylinders, set_cylinders, (void *)0 },
    { "IDE64Cylinders2", 256,
      RES_EVENT_NO, NULL,
      (int *)&drives[1].settings.cylinders, set_cylinders, (void *)1 },
    { "IDE64Cylinders3", 256,
      RES_EVENT_NO, NULL,
      (int *)&drives[2].settings.cylinders, set_cylinders, (void *)2 },
    { "IDE64Cylinders4", 256,
      RES_EVENT_NO, NULL,
      (int *)&drives[3].settings.cylinders, set_cylinders, (void *)3 },
    { "IDE64Heads1", 4,
      RES_EVENT_NO, NULL,
      (int *)&drives[0].settings.heads, set_heads, (void *)0 },
    { "IDE64Heads2", 4,
      RES_EVENT_NO, NULL,
      (int *)&drives[1].settings.heads, set_heads, (void *)1 },
    { "IDE64Heads3", 4,
      RES_EVENT_NO, NULL,
      (int *)&drives[2].settings.heads, set_heads, (void *)2 },
    { "IDE64Heads4", 4,
      RES_EVENT_NO, NULL,
      (int *)&drives[3].settings.heads, set_heads, (void *)3 },
    { "IDE64Sectors1", 16,
      RES_EVENT_NO, NULL,
      (int *)&drives[0].settings.sectors, set_sectors, (void *)0 },
    { "IDE64Sectors2", 16,
      RES_EVENT_NO, NULL,
      (int *)&drives[1].settings.sectors, set_sectors, (void *)1 },
    { "IDE64Sectors3", 16,
      RES_EVENT_NO, NULL,
      (int *)&drives[2].settings.sectors, set_sectors, (void *)2 },
    { "IDE64Sectors4", 16,
      RES_EVENT_NO, NULL,
      (int *)&drives[3].settings.sectors, set_sectors, (void *)3 },
    { "IDE64AutodetectSize1", 1,
      RES_EVENT_NO, NULL,
      (int *)&drives[0].autodetect_size, set_autodetect_size, (void *)0 },
    { "IDE64AutodetectSize2", 1,
      RES_EVENT_NO, NULL,
      (int *)&drives[1].autodetect_size, set_autodetect_size, (void *)1 },
    { "IDE64AutodetectSize3", 1,
      RES_EVENT_NO, NULL,
      (int *)&drives[2].autodetect_size, set_autodetect_size, (void *)2 },
    { "IDE64AutodetectSize4", 1,
      RES_EVENT_NO, NULL,
      (int *)&drives[3].autodetect_size, set_autodetect_size, (void *)3 },
    { "IDE64version4", 0,
      RES_EVENT_NO, NULL,
      &settings_version4, set_version4, NULL },
    { "IDE64RTCOffset", 0,
      RES_EVENT_NO, NULL,
      (int *)&rtc_offset_res, set_rtc_offset, NULL },
    { NULL }
};

int ide64_resources_init(void)
{
    int i;

    debug("IDE64 resource init");
    for (i = 0; i < 4; i++) {
        drives[i].drv = NULL;
        drives[i].filename = NULL;
    }
    if (resources_register_string(resources_string) < 0) {
        return -1;
    }
    if (resources_register_int(resources_int) < 0) {
        return -1;
    }
    
    return 0;
}

int ide64_resources_shutdown(void)
{
    int i;

    debug("IDE64 resource shutdown");

    for (i = 0; i < 4; i++) {
        if (drives[i].filename) {
            lib_free(drives[i].filename);
        }
        drives[i].filename = NULL;
    }

    lib_free(ide64_configuration_string);
    ide64_configuration_string = NULL;
    return 0;
}

static const cmdline_option_t cmdline_options[] = {
    { "-IDE64image1", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Image1", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_NAME, IDCLS_SPECIFY_IDE64_NAME,
      NULL, NULL },
    { "-IDE64image2", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Image2", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_NAME, IDCLS_SPECIFY_IDE64_NAME,
      NULL, NULL },
    { "-IDE64image3", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Image3", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_NAME, IDCLS_SPECIFY_IDE64_NAME,
      NULL, NULL },
    { "-IDE64image4", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Image4", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_NAME, IDCLS_SPECIFY_IDE64_NAME,
      NULL, NULL },
    { "-IDE64cyl1", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Cylinders1", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_CYLINDERS_IDE64,
      NULL, NULL },
    { "-IDE64cyl2", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Cylinders2", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_CYLINDERS_IDE64,
      NULL, NULL },
    { "-IDE64cyl3", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Cylinders3", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_CYLINDERS_IDE64,
      NULL, NULL },
    { "-IDE64cyl4", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Cylinders4", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_CYLINDERS_IDE64,
      NULL, NULL },
    { "-IDE64hds1", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Heads1", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_HEADS_IDE64,
      NULL, NULL },
    { "-IDE64hds2", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Heads2", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_HEADS_IDE64,
      NULL, NULL },
    { "-IDE64hds3", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Heads3", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_HEADS_IDE64,
      NULL, NULL },
    { "-IDE64hds4", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Heads4", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_HEADS_IDE64,
      NULL, NULL },
    { "-IDE64sec1", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Sectors1", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_SECTORS_IDE64,
      NULL, NULL },
    { "-IDE64sec2", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Sectors2", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_SECTORS_IDE64,
      NULL, NULL },
    { "-IDE64sec3", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Sectors3", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_SECTORS_IDE64,
      NULL, NULL },
    { "-IDE64sec4", SET_RESOURCE, 1,
      NULL, NULL, "IDE64Sectors4", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SET_AMOUNT_SECTORS_IDE64,
      NULL, NULL },
    { "-IDE64autosize1", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize1", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "+IDE64autosize1", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize1", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_NO_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "-IDE64autosize2", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize2", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "+IDE64autosize2", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize2", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_NO_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "-IDE64autosize3", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize3", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "+IDE64autosize3", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize3", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_NO_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "-IDE64autosize4", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize4", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "+IDE64autosize4", SET_RESOURCE, 0,
      NULL, NULL, "IDE64AutodetectSize4", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_NO_AUTODETECT_IDE64_GEOMETRY,
      NULL, NULL },
    { "-IDE64version4", SET_RESOURCE, 0,
      NULL, NULL, "IDE64version4", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_IDE64_V4,
      NULL, NULL },
    { "+IDE64version4", SET_RESOURCE, 0,
      NULL, NULL, "IDE64version4", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_IDE64_PRE_V4,
      NULL, NULL },
    { NULL }
};

int ide64_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}

static BYTE ide64_idebus_read(WORD addr)
{
    in_d030 = ata_register_read(drives[idrive ^ 1].drv, addr, idebus);
    in_d030 = ata_register_read(drives[idrive].drv, addr, in_d030);
    if (settings_version4) {
        idebus = (in_d030 & 0xff00) | vicii_read_phi1();
        ide64_idebus_device.io_source_valid = 1;
        return in_d030 & 0xff;
    } else {
        idebus = in_d030;
    }
    ide64_idebus_device.io_source_valid = 0;
    return 0;
}

static BYTE ide64_idebus_peek(WORD addr)
{
    if (settings_version4) {
        return ata_register_peek(drives[idrive].drv, addr) | ata_register_peek(drives[idrive ^ 1].drv, addr);
    }
    return 0;
}

static void ide64_idebus_store(WORD addr, BYTE value)
{
    switch (addr) {
        case 8:
        case 9:
            idrive = (addr & 1) << 1;
            /* fall through */
        default:
            if (settings_version4) {
                out_d030 = value | (out_d030 & 0xff00);
            }
            ata_register_store(drives[idrive].drv, addr, out_d030);
            ata_register_store(drives[idrive ^ 1].drv, addr, out_d030);
            idebus = out_d030;
            return;
    }
}

static BYTE ide64_io_read(WORD addr)
{
    ide64_io_device.io_source_valid = 1;

    switch (addr) {
        case 0:
            if (settings_version4) {
                break;
            }
            return (BYTE)in_d030;
        case 1:
            return in_d030 >> 8;
        case 2:
            return (settings_version4 ? 0x20 : 0x10) | (current_bank << 2) | (((current_cfg & 1) ^ 1) << 1) | (current_cfg >> 1);
    }
    ide64_io_device.io_source_valid = 0;
    return 0;
}

static BYTE ide64_io_peek(WORD addr)
{
    switch (addr) {
        case 0:
            if (settings_version4) {
                break;
            }
            return (BYTE)in_d030;
        case 1:
            return in_d030 >> 8;
        case 2:
            return (settings_version4 ? 0x20 : 0x10) | (current_bank << 2) | (((current_cfg & 1) ^ 1) << 1) | (current_cfg >> 1);
    }
    return 0;
}

static void ide64_io_store(WORD addr, BYTE value)
{
    switch (addr) {
        case 0:
            if (!settings_version4) {
                out_d030 = (out_d030 & 0xff00) | value;
            }
            return;
        case 1:
            out_d030 = (out_d030 & 0x00ff) | (value << 8);
            return;
        case 2:
        case 3:
        case 4:
        case 5:
            if (!settings_version4 && current_bank != ((addr ^ 2) & 3)) {
                current_bank = (addr ^ 2) & 3;
                cart_config_changed_slotmain(0, (BYTE)(current_cfg | (current_bank << CMODE_BANK_SHIFT)), CMODE_READ | CMODE_PHI2_RAM);
            }
            return;
    }
}

static BYTE ide64_ft245_read(WORD addr)
{
    if (settings_version4) {
        switch (addr ^ 1) {
        case 0:
            break;
        case 1:
            ide64_ft245_device.io_source_valid = 1;
            return 0xc0;
        }
    }
    ide64_ft245_device.io_source_valid = 0;
    return 0;
}

static BYTE ide64_ft245_peek(WORD addr)
{
    if (settings_version4) {
        switch (addr ^ 1) {
        case 0:
            return 0xff;
        case 1:
            return 0xc0;
        }
    }
    return 0;
}

static void ide64_ft245_store(WORD addr, BYTE value)
{
    return;
}

static BYTE ide64_ds1302_read(WORD addr)
{
    int i;

    if (kill_port & 1) {
        ide64_ds1302_device.io_source_valid = 0;
        return 0;
    }

    i = vicii_read_phi1() & ~1;
    ds1202_1302_set_lines(ds1302_context, kill_port & 2, 0u, 1u);
    i |= ds1202_1302_read_data_line(ds1302_context);
    ds1202_1302_set_lines(ds1302_context, kill_port & 2, 1u, 1u);

    ide64_ds1302_device.io_source_valid = 1;
    return i;
}

static BYTE ide64_ds1302_peek(WORD addr)
{
    return 0;
}

static void ide64_ds1302_store(WORD addr, BYTE value)
{
    if (kill_port & 1) {
        return;
    }
    ds1202_1302_set_lines(ds1302_context, kill_port & 2u, 0u, 1u);
    ds1202_1302_set_lines(ds1302_context, kill_port & 2u, 1u, value & 1u);
    return;
}


static BYTE ide64_rom_read(WORD addr)
{
    if (kill_port & 1) {
        ide64_rom_device.io_source_valid = 0;
        return 0;
    }

    ide64_rom_device.io_source_valid = 1;
    return roml_banks[addr | 0x1e00 | (current_bank << 14)];
}

static BYTE ide64_rom_peek(WORD addr)
{
    if (kill_port & 1) {
        return 0;
    }
    return roml_banks[addr | 0x1e00 | (current_bank << 14)];
}

static void ide64_rom_store(WORD addr, BYTE value)
{
    if (kill_port & 1) {
        return;
    }

    switch (addr) {
        case 0x60:
        case 0x61:
        case 0x62:
        case 0x63:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
            if (settings_version4 && current_bank != (addr & 7)) {
                current_bank = addr & 7;
                break;
            }
            return;
        case 0xfb:
            kill_port = value;
            ds1202_1302_set_lines(ds1302_context, kill_port & 2u, 1u, 1u);
            if ((kill_port & 1) == 0) {
                return;
            }
            /* fall through */
        case 0xfc:
        case 0xfd:
        case 0xfe:
        case 0xff:
            if (current_cfg != ((addr ^ 1) & 3)) {
                current_cfg = (addr ^ 1) & 3;
                break;
            }
            return;
        default:
            return;
    }
    cart_config_changed_slotmain(0, (BYTE)(current_cfg | (current_bank << CMODE_BANK_SHIFT)), CMODE_READ | CMODE_PHI2_RAM);
}

BYTE ide64_roml_read(WORD addr)
{
    return roml_banks[(addr & 0x3fff) | (roml_bank << 14)];
}

BYTE ide64_romh_read(WORD addr)
{
    return romh_banks[(addr & 0x3fff) | (romh_bank << 14)];
}

BYTE ide64_1000_7fff_read(WORD addr)
{
    return export_ram0[addr & 0x7fff];
}

void ide64_1000_7fff_store(WORD addr, BYTE value)
{
    export_ram0[addr & 0x7fff] = value;
}

BYTE ide64_a000_bfff_read(WORD addr)
{
    return romh_banks[(addr & 0x3fff) | (romh_bank << 14)];
}

BYTE ide64_c000_cfff_read(WORD addr)
{
    return export_ram0[addr & 0x7fff];
}

void ide64_c000_cfff_store(WORD addr, BYTE value)
{
    export_ram0[addr & 0x7fff] = value;
}

void ide64_config_init(void)
{
    int i;
    struct drive_s *drive;

    debug("IDE64 init");
    cart_config_changed_slotmain(0, 0, CMODE_READ | CMODE_PHI2_RAM);
    current_bank = 0;
    current_cfg = 0;
    kill_port = 0;
    idrive = 0;

    for (i = 0; i < 4; i++) {
        drive = &drives[i];
        ata_update_timing(drive->drv, machine_get_cycles_per_second());
        if (drive->update_needed) {
            drive->update_needed = 0;
            detect_ide64_image(drive);
            ata_image_attach(drive->drv, drive->filename, drive->type, drive->detected);
            memset(export_ram0, 0, 0x8000);
        }
    }
}

void ide64_config_setup(BYTE *rawcart)
{
    debug("IDE64 setup");
    memcpy(roml_banks, rawcart, 0x20000);
    memcpy(romh_banks, rawcart, 0x20000);
    memset(export_ram0, 0, 0x8000);
}

void ide64_detach(void)
{
    int i;

    ds1202_1302_destroy(ds1302_context);

    for (i = 0; i < 4; i++) {
        if (drives[i].drv) {
            ata_image_detach(drives[i].drv);
            ata_shutdown(drives[i].drv);
            drives[i].drv = NULL;
        }
    }

    ide64_unregister();
    debug("IDE64 detached");
}

static int ide64_common_attach(BYTE *rawcart, int detect)
{
    int i;

    idebus = 0;
    ds1302_context = ds1202_1302_init((BYTE *)ide64_DS1302, &rtc_offset, 1302);

    if (detect) {
        for (i = 0x1e60; i < 0x1efd; i++) {
            if (rawcart[i] == 0x8d && ((rawcart[i + 1] - 2) & 0xfc) == 0x30 && rawcart[i + 2] == 0xde) {
                settings_version4 = 0; /* V3 emulation required */
                break;
            }
            if (rawcart[i] == 0x8d && (rawcart[i + 1] & 0xf8) == 0x60 && rawcart[i + 2] == 0xde) {
                settings_version4 = 1; /* V4 emulation required */
                break;
            }
        }
    }
    for (i = 0; i < 4; i++) {
        if (!drives[i].drv) {
            drives[i].drv = ata_init(i);
        }
        drives[i].update_needed = 1;
    }

    debug("IDE64 attached");
    return ide64_register();
}

int ide64_bin_attach(const char *filename, BYTE *rawcart)
{
    if (util_file_load(filename, rawcart, 0x20000, UTIL_FILE_LOAD_SKIP_ADDRESS | UTIL_FILE_LOAD_FILL) < 0) {
        return -1;
    }

    return ide64_common_attach(rawcart, 1);
}

int ide64_crt_attach(FILE *fd, BYTE *rawcart)
{
    crt_chip_header_t chip;
    int i;

    for (i = 0; i <= 7; i++) {
        if (crt_read_chip_header(&chip, fd)) {
            if (i == 4) {
                break;
            }
            return -1;
        }

        if (chip.start != 0x8000 || chip.size != 0x4000) {
            return -1;
        }

        if (chip.bank > 7) {
            return -1;
        }

        if (crt_read_chip(rawcart, chip.bank << 14, &chip, fd)) {
            return -1;
        }
    }

    return ide64_common_attach(rawcart, 1);
}

static int ide64_idebus_dump(void) {
    if (ata_register_dump(drives[idrive].drv) == 0) {
        return 0;
    }
    return ata_register_dump(drives[idrive ^ 1].drv);
}

static int ide64_io_dump(void) {
    const char *configs[4] = {
        "8k", "16k", "stnd", "open"
    };
    mon_out("Version: %d, Mode: %s, ", settings_version4 ? 4:3, (kill_port & 1) ? "Disabled" : "Enabled");
    mon_out("ROM bank: %d, Config: %s, Interface: %d\n", current_bank, configs[current_cfg], idrive >> 1);
    return 0;
}

/* ---------------------------------------------------------------------*/
/*    snapshot support functions                                             */

#define CART_DUMP_VER_MAJOR   0
#define CART_DUMP_VER_MINOR   0
#define SNAP_MODULE_NAME "CARTIDE"

int ide64_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;
    int i;

    for (i = 0; i < 4; i++) {
        if (drives[i].drv) {
            if (ata_snapshot_write_module(drives[i].drv, s)) {
                return -1;
            }
        }
    }

    m = snapshot_module_create(s, SNAP_MODULE_NAME, CART_DUMP_VER_MAJOR, CART_DUMP_VER_MINOR);

    if (m == NULL) {
        return -1;
    }

    SMW_DW(m, settings_version4);
    SMW_BA(m, roml_banks, settings_version4 ? 0x20000 : 0x10000);
    SMW_BA(m, export_ram0, 0x8000);
    SMW_DW(m, current_bank);
    SMW_DW(m, current_cfg);
    SMW_B(m, kill_port);
    SMW_DW(m, idrive);
    SMW_W(m, in_d030);
    SMW_W(m, out_d030);
    SMW_BA(m, (unsigned char *)ide64_DS1302, 64); /* TODO: RTC snapshot! */

    snapshot_module_close(m);

    return 0;
}

int ide64_snapshot_read_module(snapshot_t *s)
{
    BYTE vmajor, vminor;
    snapshot_module_t *m;
    int i;

    for (i = 0; i < 4; i++) {
        if (!drives[i].drv) {
            drives[i].drv = ata_init(i);
            detect_ide64_image(&drives[i]);
            ata_image_attach(drives[i].drv, drives[i].filename, drives[i].type, drives[i].detected);
        }
        if (ata_snapshot_read_module(drives[i].drv, s)) {
            return -1;
        }
    }

    m = snapshot_module_open(s, SNAP_MODULE_NAME, &vmajor, &vminor);

    if (m == NULL) {
        return -1;
    }

    if ((vmajor != CART_DUMP_VER_MAJOR) || (vminor != CART_DUMP_VER_MINOR)) {
        snapshot_module_close(m);
        return -1;
    }

    ide64_unregister();
    SMR_DW_INT(m, &settings_version4);
    if (settings_version4) settings_version4 = 1;
    ide64_register();
    SMR_BA(m, roml_banks, settings_version4 ? 0x20000: 0x10000);
    memcpy(romh_banks, roml_banks, settings_version4 ? 0x20000: 0x10000);
    SMR_BA(m, export_ram0, 0x8000);
    SMR_DW_INT(m, &current_bank);
    current_bank &= settings_version4 ? 7 : 3;
    SMR_DW_INT(m, &current_cfg);
    current_cfg &= 3;
    SMR_B(m, &kill_port);
    SMR_DW_INT(m, &idrive);
    if (idrive) idrive = 2;
    SMR_W(m, &in_d030);
    SMR_W(m, &out_d030);
    SMR_BA(m, (unsigned char *)ide64_DS1302, 64); /* TODO: RTC snapshot! */
    ide64_DS1302[64] = 0;

    snapshot_module_close(m);

    return ide64_common_attach(roml_banks, 0);
}
