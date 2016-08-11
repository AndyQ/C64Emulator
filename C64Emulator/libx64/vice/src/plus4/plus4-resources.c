/*
 * plus4-resources.c
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

#include "kbd.h"
#include "keyboard.h"
#include "lib.h"
#include "machine.h"
#include "mem.h"
#include "plus4-resources.h"
#include "plus4mem.h"
#include "plus4memcsory256k.h"
#include "plus4memhannes256k.h"
#include "plus4rom.h"
#include "plus4cart.h"
#include "resources.h"
#include "util.h"
#include "vsync.h"


#define KBD_INDEX_PLUS4_SYM 0
#define KBD_INDEX_PLUS4_POS 1


static int romset_firmware[4];

/* What sync factor between the CPU and the drive?  If equal to
   `MACHINE_SYNC_PAL', the same as PAL machines.  If equal to
   `MACHINE_SYNC_NTSC', the same as NTSC machines.  The sync factor is
   calculated as 65536 * drive_clk / clk_[main machine] */
static int sync_factor;

/* Name of the BASIC ROM.  */
static char *basic_rom_name = NULL;

/* Name of the Kernal ROM.  */
static char *kernal_rom_name = NULL;

/* Name of the Function (3plus1) ROMs.  */
static char *func_lo_rom_name = NULL;
static char *func_hi_rom_name = NULL;

/* Name of the external cartridge ROMs.  */
static char *c1lo_rom_name = NULL;
static char *c1hi_rom_name = NULL;
static char *c2lo_rom_name = NULL;
static char *c2hi_rom_name = NULL;

/* Size of RAM installed in kbytes */
static int ram_size_plus4 = 64;

static int set_kernal_rom_name(const char *val, void *param)
{
    if (util_string_set(&kernal_rom_name, val))
        return 0;

    return plus4rom_load_kernal(kernal_rom_name);
}

static int set_basic_rom_name(const char *val, void *param)
{
    if (util_string_set(&basic_rom_name, val))
        return 0;

    return plus4rom_load_basic(basic_rom_name);
}

static int set_func_lo_rom_name(const char *val, void *param)
{
    if (util_string_set(&func_lo_rom_name, val))
        return 0;

    return plus4cart_load_func_lo(func_lo_rom_name);
}

static int set_func_hi_rom_name(const char *val, void *param)
{
    if (util_string_set(&func_hi_rom_name, val))
        return 0;

    return plus4cart_load_func_hi(func_hi_rom_name);
}

static int set_c1lo_rom_name(const char *val, void *param)
{
    if (util_string_set(&c1lo_rom_name, val))
        return 0;

    return plus4cart_load_c1lo(c1lo_rom_name);
}

static int set_c1hi_rom_name(const char *val, void *param)
{
    if (util_string_set(&c1hi_rom_name, val))
        return 0;

    return plus4cart_load_c1hi(c1hi_rom_name);
}

static int set_c2lo_rom_name(const char *val, void *param)
{
    if (util_string_set(&c2lo_rom_name, val))
        return 0;

    return plus4cart_load_c2lo(c2lo_rom_name);
}

static int set_c2hi_rom_name(const char *val, void *param)
{
    if (util_string_set(&c2hi_rom_name, val))
        return 0;

    return plus4cart_load_c2hi(c2hi_rom_name);
}

static int set_ram_size_plus4(int rs, void *param)
{
    if ((rs != 64) && (rs != 32) && (rs != 16) && (rs != 256) && (rs != 1024)
        && (rs != 4096)) {
        return -1;
    }

    ram_size_plus4 = rs;

    if (ram_size_plus4 < 256) {
        resources_set_int("H256K", 0);
        resources_set_int("CS256K", 0);
    }

    vsync_suspend_speed_eval();
    mem_initialize_memory();
    machine_trigger_reset(MACHINE_RESET_MODE_HARD);

    return 0;
}

static int set_sync_factor(int val, void *param)
{
    int change_timing = 0;

    if (sync_factor != val)
        change_timing = 1;

    switch (val) {
      case MACHINE_SYNC_PAL:
        sync_factor = val;
        if (change_timing)
            machine_change_timing(MACHINE_SYNC_PAL);
        break;
      case MACHINE_SYNC_NTSC:
        sync_factor = val;
        if (change_timing)
            machine_change_timing(MACHINE_SYNC_NTSC);
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
    { "KernalName", "kernal", RES_EVENT_NO, NULL,
      &kernal_rom_name, set_kernal_rom_name, NULL },
    { "BasicName", "basic", RES_EVENT_NO, NULL,
      &basic_rom_name, set_basic_rom_name, NULL },
    { "FunctionLowName", "3plus1lo", RES_EVENT_NO, NULL,
      &func_lo_rom_name, set_func_lo_rom_name, NULL },
    { "FunctionHighName", "3plus1hi", RES_EVENT_NO, NULL,
      &func_hi_rom_name, set_func_hi_rom_name, NULL },
    { "c1loName", "", RES_EVENT_NO, NULL,
      &c1lo_rom_name, set_c1lo_rom_name, NULL },
    { "c1hiName", "", RES_EVENT_NO, NULL,
      &c1hi_rom_name, set_c1hi_rom_name, NULL },
    { "c2loName", "", RES_EVENT_NO, NULL,
      &c2lo_rom_name, set_c2lo_rom_name, NULL },
    { "c2hiName", "", RES_EVENT_NO, NULL,
      &c2hi_rom_name, set_c2hi_rom_name, NULL },
#ifdef COMMON_KBD
    { "KeymapSymFile", KBD_PLUS4_SYM, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[0],
      keyboard_set_keymap_file, (void *)0 },
    { "KeymapPosFile", KBD_PLUS4_POS, RES_EVENT_NO, NULL,
      &machine_keymap_file_list[1],
      keyboard_set_keymap_file, (void *)1 },
#endif
    { NULL }
};

static const resource_int_t resources_int[] = {
    { "MachineVideoStandard", MACHINE_SYNC_PAL, RES_EVENT_SAME, NULL,
      &sync_factor, set_sync_factor, NULL },
    { "RomsetKernalName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[0], set_romset_firmware, (void *)0 },
    { "RomsetBasicName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[1], set_romset_firmware, (void *)1 },
    { "RomsetFunctionLowName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[2], set_romset_firmware, (void *)2 },
    { "RomsetFunctionHighName", 0, RES_EVENT_NO, NULL,
      &romset_firmware[3], set_romset_firmware, (void *)3 },
    { "RamSize", 64, RES_EVENT_SAME, NULL,
      &ram_size_plus4, set_ram_size_plus4, NULL },
#ifdef COMMON_KBD
    { "KeymapIndex", KBD_INDEX_PLUS4_DEFAULT, RES_EVENT_NO, NULL,
      &machine_keymap_index, keyboard_set_keymap_index, NULL },
#endif
    { NULL }
};

int plus4_resources_init(void)
{
    if (h256k_resources_init() < 0)
        return -1;
    if (cs256k_resources_init() < 0)
        return -1;

    if (resources_register_string(resources_string) < 0)
        return -1;

    return resources_register_int(resources_int);
}

void plus4_resources_shutdown(void)
{
    lib_free(basic_rom_name);
    lib_free(kernal_rom_name);
    lib_free(func_lo_rom_name);
    lib_free(func_hi_rom_name);
    lib_free(c1lo_rom_name);
    lib_free(c1hi_rom_name);
    lib_free(c2lo_rom_name);
    lib_free(c2hi_rom_name);
    lib_free(machine_keymap_file_list[0]);
    lib_free(machine_keymap_file_list[1]);
}

