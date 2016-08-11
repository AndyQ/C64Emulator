/*
 * cbm5x0-resources.c - CBM-5x0 resources.
 *
 * Written by
 *  Andr� Fachat <fachat@physik.tu-chemnitz.de>
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
#include <string.h>

#include "archdep.h"
#include "cbm2-resources.h"
#include "cbm2.h"
#include "cbm2cia.h"
#include "cbm2mem.h"
#include "cbm2rom.h"
#include "cbm2tpi.h"
#include "cia.h"
#include "kbd.h"
#include "keyboard.h"
#include "lib.h"
#include "machine.h"
#include "resources.h"
#include "sid-resources.h"
#include "util.h"
#include "vicii-resources.h"
#include "vicii.h"
#include "vsync.h"


#define KBD_INDEX_CBM2_BUKS 0
#define KBD_INDEX_CBM2_BUKP 1
#define KBD_INDEX_CBM2_BGRS 2
#define KBD_INDEX_CBM2_BGRP 3
#define KBD_INDEX_CBM2_BDES 4
#define KBD_INDEX_CBM2_BDEP 5


static int romset_firmware[7];

static int sync_factor;

static char *kernal_rom_name = NULL;
static char *chargen_name = NULL;
static char *basic_rom_name = NULL;

static const BYTE model_port_mask[] = { 0xc0, 0x40, 0x00 };

int cbm2_model_line = 0;

int cia1_model = CIA_MODEL_6526;

static int set_cbm2_model_line(int val, void *param)
{
    int tmp = val;

    if (tmp >= 0 && tmp < 3) {
        cbm2_model_line = val;
    }

    set_cbm2_model_port_mask(model_port_mask[cbm2_model_line]);

    /* FIXME: VIC-II config */
    return 0;
}

/* ramsize starts counting at 0x10000 if less than 512. If 512 or more,
   it starts counting at 0x00000.
   CBM2MEM module requires(!) that ramsize never gets 512-64 = 448
   (Just don't ask...)
   In a C500, that has RAM in bank 0, the actual size of RAM is 64k larger
   than ramsize when ramsize is less than 512, otherwise the same as ramsize.
*/

int ramsize;

static int set_ramsize(int rs, void *param)
{
    if (rs!=64 && rs!=128 && rs!=256 && rs!=512 && rs!=1024)
        return -1;

    ramsize = rs;
    vsync_suspend_speed_eval();
    mem_initialize_memory();
    machine_trigger_reset(MACHINE_RESET_MODE_HARD);

    return 0;
}

static int set_chargen_rom_name(const char *val, void *param)
{
    if (util_string_set(&chargen_name, val))
        return 0;

    return cbm2rom_load_chargen(chargen_name);
}

static int set_kernal_rom_name(const char *val, void *param)
{
    if (util_string_set(&kernal_rom_name, val))
        return 0;

    return cbm2rom_load_kernal(kernal_rom_name);
}

static int set_basic_rom_name(const char *val, void *param)
{
    if (util_string_set(&basic_rom_name, val))
        return 0;

    return cbm2rom_load_basic(basic_rom_name);
}

static int set_cia1_model(int val, void *param)
{
    int old_cia_model = cia1_model;

    switch (val) {
        case CIA_MODEL_6526:
        case CIA_MODEL_6526A:
            cia1_model = val;
            break;
        default:
            return -1;
    }

    if (old_cia_model != cia1_model) {
        cia1_update_model();
    }

    return 0;
}

static int cbm5x0_set_sync_factor(int val, void *param)
{
    int change_timing = 0;
    int border_mode = VICII_BORDER_MODE(vicii_resources.border_mode);

    if (sync_factor != val)
        change_timing = 1;

    switch (val) {
      case MACHINE_SYNC_PAL:
        sync_factor = val;
        if (change_timing)
            machine_change_timing(MACHINE_SYNC_PAL ^ border_mode);
        break;
      case MACHINE_SYNC_NTSC:
        sync_factor = val;
        if (change_timing)
            machine_change_timing(MACHINE_SYNC_NTSC ^ border_mode);
        break;
      default:
        return -1;
    }
    return 0;
}

static int set_romset_firmware(int val, void *param)
{
    unsigned int num = vice_ptr_to_uint(param);

    romset_firmware[num] = val;

    return 0;
}

static const resource_string_t cbm5x0_resources_string[] = {
    { "ChargenName", CBM2_CHARGEN500, RES_EVENT_NO, NULL,
      &chargen_name, set_chargen_rom_name, NULL },
    { "KernalName", CBM2_KERNAL500, RES_EVENT_NO, NULL,
      &kernal_rom_name, set_kernal_rom_name, NULL },
    { "BasicName", CBM2_BASIC500, RES_EVENT_NO, NULL,
      &basic_rom_name, set_basic_rom_name, NULL },
    { NULL }
};

static const resource_string_t resources_string[] = {
#ifdef COMMON_KBD
    { "KeymapBusinessUKSymFile", KBD_CBM2_SYM_UK, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[0], keyboard_set_keymap_file, (void *)0 },
    { "KeymapBusinessUKPosFile", KBD_CBM2_POS_UK, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[1], keyboard_set_keymap_file, (void *)1 },
    { "KeymapGraphicsSymFile", KBD_CBM2_SYM_GR, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[2], keyboard_set_keymap_file, (void *)2 },
    { "KeymapGraphicsPosFile", KBD_CBM2_POS_GR, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[3], keyboard_set_keymap_file, (void *)3 },
    { "KeymapBusinessDESymFile", KBD_CBM2_SYM_DE, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[4], keyboard_set_keymap_file, (void *)4 },
    { "KeymapBusinessDEPosFile", KBD_CBM2_POS_DE, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[5], keyboard_set_keymap_file, (void *)5 },
#endif
    { NULL }
};

static const resource_int_t cbm5x0_resources_int[] = {
    { "MachineVideoStandard", MACHINE_SYNC_PAL, RES_EVENT_SAME, NULL,
      &sync_factor, cbm5x0_set_sync_factor, NULL },
    { "RamSize", 64, RES_EVENT_SAME, NULL,
      &ramsize, set_ramsize, NULL },
    { "ModelLine", 2, RES_EVENT_SAME, NULL,
      &cbm2_model_line, set_cbm2_model_line, NULL },
    { NULL }
};

static const resource_int_t resources_int[] = {
    { "RomsetChargenName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[0], set_romset_firmware, (void *)0 },
    { "RomsetKernalName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[1], set_romset_firmware, (void *)1 },
    { "RomsetBasicName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[2], set_romset_firmware, (void *)2 },
    { "RomsetCart1Name", 0, RES_EVENT_NO, NULL,
      &romset_firmware[3], set_romset_firmware, (void *)3 },
    { "RomsetCart2Name", 0, RES_EVENT_NO, NULL,
      &romset_firmware[4], set_romset_firmware, (void *)4 },
    { "RomsetCart4Name", 0, RES_EVENT_NO, NULL,
      &romset_firmware[5], set_romset_firmware, (void *)5 },
    { "RomsetCart6Name", 0, RES_EVENT_NO, NULL,
      &romset_firmware[6], set_romset_firmware, (void *)6 },
    { "CIA1Model", CIA_MODEL_6526, RES_EVENT_SAME, NULL,
      &cia1_model, set_cia1_model, NULL },
#ifdef COMMON_KBD
    { "KeymapIndex", KBD_INDEX_CBM2_DEFAULT, RES_EVENT_NO, NULL,
      &machine_keymap_index, keyboard_set_keymap_index, NULL },
#endif
    { NULL }
};

int cbm2_resources_init(void)
{
    if (resources_register_string(resources_string) < 0) {
        return -1;
    }

    if (resources_register_int(resources_int) < 0) {
        return -1;
    }

    if (resources_register_string(cbm5x0_resources_string) < 0) {
        return -1;
    }
    return resources_register_int(cbm5x0_resources_int);
}

void cbm2_resources_shutdown(void)
{
    lib_free(kernal_rom_name);
    lib_free(chargen_name);
    lib_free(basic_rom_name);
    lib_free(machine_keymap_file_list[0]);
    lib_free(machine_keymap_file_list[1]);
    lib_free(machine_keymap_file_list[2]);
    lib_free(machine_keymap_file_list[3]);
    lib_free(machine_keymap_file_list[4]);
    lib_free(machine_keymap_file_list[5]);
}

