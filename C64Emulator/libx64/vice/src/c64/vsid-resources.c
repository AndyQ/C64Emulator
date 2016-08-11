/*
 * vsid-resources.c
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
#include <string.h>

#include "c64-resources.h"
#include "c64acia.h"
#include "c64cart.h"
#include "c64cia.h"
#include "c64rom.h"
#include "cartio.h"
#include "cartridge.h"
#include "cia.h"
#include "kbd.h"
#include "keyboard.h"
#include "lib.h"
#include "machine.h"
#include "resources.h"
#include "reu.h"
#include "georam.h"
#include "sid-resources.h"
#include "util.h"
#include "vicii-resources.h"
#include "vicii.h"
#include "c64fastiec.h"

#define KBD_INDEX_C64_SYM   0
#define KBD_INDEX_C64_POS   1
#define KBD_INDEX_C64_SYMDE 2

static int romset_firmware[3];

/* What sync factor between the CPU and the drive?  If equal to
   `MACHINE_SYNC_PAL', the same as PAL machines.  If equal to
   `MACHINE_SYNC_NTSC', the same as NTSC machines.  The sync factor is
   calculated as 65536 * drive_clk / clk_[main machine] */
static int sync_factor;

/* Name of the character ROM.  */
static char *chargen_rom_name = NULL;

/* Name of the BASIC ROM.  */
static char *basic_rom_name = NULL;

/* Name of the Kernal ROM.  */
static char *kernal_rom_name = NULL;

/* Kernal revision for ROM patcher.  */
char *kernal_revision = NULL;

int cia1_model;
int cia2_model;

static int set_chargen_rom_name(const char *val, void *param)
{
    if (util_string_set(&chargen_rom_name, val)) {
        return 0;
    }

    return c64rom_load_chargen(chargen_rom_name);
}

static int set_kernal_rom_name(const char *val, void *param)
{
    if (util_string_set(&kernal_rom_name, val)) {
        return 0;
    }

    /* load kernal without a kernal overriding buffer */
    return c64rom_load_kernal(kernal_rom_name, NULL);
}

static int set_basic_rom_name(const char *val, void *param)
{
    if (util_string_set(&basic_rom_name, val)) {
        return 0;
    }

    return c64rom_load_basic(basic_rom_name);
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

static int set_cia2_model(int val, void *param)
{
    int old_cia_model = cia2_model;

    switch (val) {
        case CIA_MODEL_6526:
        case CIA_MODEL_6526A:
            cia2_model = val;
            break;
        default:
            return -1;
    }

    if (old_cia_model != cia2_model) {
        cia2_update_model();
    }

    return 0;
}

/* FIXME: Should patch the ROM on-the-fly.  */
static int set_kernal_revision(const char *val, void *param)
{
    util_string_set(&kernal_revision, val);
    return 0;
}

static int set_sync_factor(int val, void *param)
{
    int change_timing = 0;
    int border_mode = VICII_BORDER_MODE(vicii_resources.border_mode);

    if (sync_factor != val) {
        change_timing = 1;
    }

    switch (val) {
        case MACHINE_SYNC_PAL:
            sync_factor = val;
            if (change_timing) {
                machine_change_timing(MACHINE_SYNC_PAL ^ border_mode);
            }
            break;
        case MACHINE_SYNC_NTSC:
            sync_factor = val;
            if (change_timing) {
                machine_change_timing(MACHINE_SYNC_NTSC ^ border_mode);
            }
            break;
        case MACHINE_SYNC_NTSCOLD:
            sync_factor = val;
            if (change_timing) {
                machine_change_timing(MACHINE_SYNC_NTSCOLD ^ border_mode);
            }
            break;
        case MACHINE_SYNC_PALN:
            sync_factor = val;
            if (change_timing) {
                machine_change_timing(MACHINE_SYNC_PALN ^ border_mode);
            }
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

static const resource_string_t resources_string[] = {
    { "ChargenName", "chargen", RES_EVENT_NO, NULL,
       /* FIXME: should be same but names may differ */
      &chargen_rom_name, set_chargen_rom_name, NULL },
    { "KernalName", "kernal", RES_EVENT_NO, NULL,
      /* FIXME: should be same but names may differ */
      &kernal_rom_name, set_kernal_rom_name, NULL },
    { "BasicName", "basic", RES_EVENT_NO, NULL,
      /* FIXME: should be same but names may differ */
      &basic_rom_name, set_basic_rom_name, NULL },
    { "KernalRev", "", RES_EVENT_SAME, NULL,
      &kernal_revision, set_kernal_revision, NULL },
#ifdef COMMON_KBD
    { "KeymapSymFile", KBD_C64_SYM_US, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[0],
      keyboard_set_keymap_file, (void *)0 },
    { "KeymapPosFile", KBD_C64_POS, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[1],
      keyboard_set_keymap_file, (void *)1 },
    { "KeymapSymDeFile", KBD_C64_SYM_DE, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[2],
      keyboard_set_keymap_file, (void *)2 },
#endif
    { NULL }
};

static const resource_int_t resources_int[] = {
    { "MachineVideoStandard", MACHINE_SYNC_PAL, RES_EVENT_SAME, NULL,
      &sync_factor, set_sync_factor, NULL },
    { "RomsetChargenName", 0, RES_EVENT_NO, NULL,
      /* FIXME: should be same but names may differ */
      &romset_firmware[0], set_romset_firmware, (void *)0 },
    { "RomsetKernalName", 1, RES_EVENT_NO, NULL,
      /* FIXME: should be same but names may differ */
      &romset_firmware[1], set_romset_firmware, (void *)1 },
    { "RomsetBasicName", 0, RES_EVENT_NO, NULL,
      /* FIXME: should be same but names may differ */
      &romset_firmware[2], set_romset_firmware, (void *)2 },
    { "CIA1Model", CIA_MODEL_6526, RES_EVENT_SAME, NULL,
      &cia1_model, set_cia1_model, NULL },
    { "CIA2Model", CIA_MODEL_6526, RES_EVENT_SAME, NULL,
      &cia2_model, set_cia2_model, NULL },
#ifdef COMMON_KBD
    { "KeymapIndex", KBD_INDEX_C64_DEFAULT, RES_EVENT_NO, NULL,
      &machine_keymap_index, keyboard_set_keymap_index, NULL },
#endif
    { "SidStereoAddressStart", 0xde00, RES_EVENT_SAME, NULL,
      (int *)&sid_stereo_address_start, sid_set_sid_stereo_address, NULL },
    { "SidTripleAddressStart", 0xdf00, RES_EVENT_SAME, NULL,
      (int *)&sid_triple_address_start, sid_set_sid_triple_address, NULL },
    { NULL }
};

void c64_resources_update_cia_models(int model)
{
    set_cia1_model(model, NULL);
    set_cia2_model(model, NULL);
}

int c64_resources_init(void)
{
    if (resources_register_string(resources_string) < 0) {
        return -1;
    }

    return resources_register_int(resources_int);
}

void c64_resources_shutdown(void)
{
    lib_free(chargen_rom_name);
    lib_free(basic_rom_name);
    lib_free(kernal_rom_name);
    lib_free(kernal_revision);
    lib_free(machine_keymap_file_list[0]);
    lib_free(machine_keymap_file_list[1]);
    lib_free(machine_keymap_file_list[2]);
}
