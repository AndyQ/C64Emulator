/*
 * c64memsc.c -- C64 memory handling for x64sc.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Ettore Perazzoli <ettore@comm2000.it>
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
#include <string.h>

#include "alarm.h"
#include "c64.h"
#include "c64-resources.h"
#include "c64_256k.h"
#include "c64cart.h"
#include "c64cia.h"
#include "c64mem.h"
#include "c64meminit.h"
#include "c64memlimit.h"
#include "c64memrom.h"
#include "c64pla.h"
#include "c64cartmem.h"
#include "cartio.h"
#include "cartridge.h"
#include "cia.h"
#include "clkguard.h"
#include "machine.h"
#include "mainc64cpu.h"
#include "maincpu.h"
#include "mem.h"
#include "monitor.h"
#include "plus256k.h"
#include "plus60k.h"
#include "ram.h"
#include "reu.h"
#include "sid.h"
#include "tpi.h"
#include "vicii-cycle.h"
#include "vicii-mem.h"
#include "vicii-phi1.h"
#include "vicii.h"

/* Machine class (moved from c64.c to distinguish between x64 and x64sc) */
int machine_class = VICE_MACHINE_C64SC;

/* C64 memory-related resources.  */

/* Adjust this pointer when the MMU changes banks.  */
static BYTE **bank_base;
static int *bank_limit = NULL;
unsigned int mem_old_reg_pc;

/* ------------------------------------------------------------------------- */

/* Number of possible memory configurations.  */
#define NUM_CONFIGS     32

/* Number of possible video banks (16K each).  */
#define NUM_VBANKS      4

/* The C64 memory.  */
BYTE mem_ram[C64_RAM_SIZE];

#ifdef USE_EMBEDDED
#include "c64chargen.h"
#else
BYTE mem_chargen_rom[C64_CHARGEN_ROM_SIZE];
#endif

/* Internal color memory.  */
static BYTE mem_color_ram[0x400];
BYTE *mem_color_ram_cpu, *mem_color_ram_vicii;

/* Pointer to the chargen ROM.  */
BYTE *mem_chargen_rom_ptr;

/* Pointers to the currently used memory read and write tables.  */
read_func_ptr_t *_mem_read_tab_ptr;
store_func_ptr_t *_mem_write_tab_ptr;
BYTE **_mem_read_base_tab_ptr;
int *mem_read_limit_tab_ptr;

/* Memory read and write tables.  */
static store_func_ptr_t mem_write_tab[NUM_CONFIGS][0x101];
static read_func_ptr_t mem_read_tab[NUM_CONFIGS][0x101];
static BYTE *mem_read_base_tab[NUM_CONFIGS][0x101];
static int mem_read_limit_tab[NUM_CONFIGS][0x101];

static store_func_ptr_t mem_write_tab_watch[0x101];
static read_func_ptr_t mem_read_tab_watch[0x101];

/* Current video bank (0, 1, 2 or 3).  */
static int vbank;

/* Current memory configuration.  */
static int mem_config;

/* Tape sense status: 1 = some button pressed, 0 = no buttons pressed.  */
static int tape_sense = 0;

/* Current watchpoint state. 1 = watchpoints active, 0 = no watchpoints */
static int watchpoints_active;

/* ------------------------------------------------------------------------- */

static BYTE read_watch(WORD addr)
{
    monitor_watch_push_load_addr(addr, e_comp_space);
    return mem_read_tab[mem_config][addr >> 8](addr);
}

static void store_watch(WORD addr, BYTE value)
{
    monitor_watch_push_store_addr(addr, e_comp_space);
    mem_write_tab[mem_config][addr >> 8](addr, value);
}

void mem_toggle_watchpoints(int flag, void *context)
{
    if (flag) {
        _mem_read_tab_ptr = mem_read_tab_watch;
        _mem_write_tab_ptr = mem_write_tab_watch;
    } else {
        _mem_read_tab_ptr = mem_read_tab[mem_config];
        _mem_write_tab_ptr = mem_write_tab[mem_config];
    }
    watchpoints_active = flag;
}

/* ------------------------------------------------------------------------- */

static void clk_overflow_callback(CLOCK sub, void *unused_data)
{
    if (pport.data_falloff_bit6) {
        pport.data_falloff_bit6++;
        if (pport.data_falloff_bit6 == 3) {
            pport.data_set_bit6 = 0;
            pport.data_falloff_bit6 = 0;
        }
    }

    if (pport.data_falloff_bit7) {
        pport.data_falloff_bit7++;
        if (pport.data_falloff_bit7 == 3) {
            pport.data_set_bit7 = 0;
            pport.data_falloff_bit7 = 0;
        }
    }

    pport.data_set_clk_bit6 -= sub;
    pport.data_set_clk_bit7 -= sub;
}

static void check_data_set_alarm(void)
{
    if (pport.data_set_clk_bit6 < maincpu_clk) {
        pport.data_falloff_bit6 = 0;
        pport.data_set_bit6 = 0;
    }

    if (pport.data_set_clk_bit7 < maincpu_clk) {
        pport.data_set_bit7 = 0;
        pport.data_falloff_bit7 = 0;
    }
}

void c64_mem_init(void)
{
    clk_guard_add_callback(maincpu_clk_guard, clk_overflow_callback, NULL);

    /* Initialize REU BA low interface (FIXME find a better place for this) */
    reu_ba_register(vicii_cycle, vicii_steal_cycles, &maincpu_ba_low_flags, MAINCPU_BA_LOW_REU);
}

void mem_pla_config_changed(void)
{
    mem_config = (((~pport.dir | pport.data) & 0x7) | (export.exrom << 3) | (export.game << 4));

    c64pla_config_changed(tape_sense, 1, 0x17);

    if (watchpoints_active) {
        _mem_read_tab_ptr = mem_read_tab_watch;
        _mem_write_tab_ptr = mem_write_tab_watch;
    } else {
        _mem_read_tab_ptr = mem_read_tab[mem_config];
        _mem_write_tab_ptr = mem_write_tab[mem_config];
    }

    _mem_read_base_tab_ptr = mem_read_base_tab[mem_config];
    mem_read_limit_tab_ptr = mem_read_limit_tab[mem_config];

    if (bank_limit != NULL) {
        *bank_base = _mem_read_base_tab_ptr[mem_old_reg_pc >> 8];
        if (*bank_base != 0) {
            *bank_base = _mem_read_base_tab_ptr[mem_old_reg_pc >> 8] - (mem_old_reg_pc & 0xff00);
        }
        *bank_limit = mem_read_limit_tab_ptr[mem_old_reg_pc >> 8];
    }
}

BYTE zero_read(WORD addr)
{
    addr &= 0xff;

    switch ((BYTE)addr) {
        case 0:
            return pport.dir_read;
        case 1:
            if (pport.data_falloff_bit6 || pport.data_falloff_bit7) {
                check_data_set_alarm();
            }

            return (pport.data_read & (0xff - (((!pport.data_set_bit6) << 6) + ((!pport.data_set_bit7) << 7))));
    }

    if (c64_256k_enabled) {
        return c64_256k_ram_segment0_read(addr);
    } else {
        if (plus256k_enabled) {
            return plus256k_ram_low_read(addr);
        } else {
            return mem_ram[addr & 0xff];
        }
    }
}

void zero_store(WORD addr, BYTE value)
{
    addr &= 0xff;

    switch ((BYTE)addr) {
        case 0:
            if (vbank == 0) {
                if (c64_256k_enabled) {
                    c64_256k_ram_segment0_store((WORD)0, vicii_read_phi1());
                } else {
                    if (plus256k_enabled) {
                        plus256k_ram_low_store((WORD)0, vicii_read_phi1());
                    } else {
                        mem_ram[0] = vicii_read_phi1();
                    }
                }
            } else {
                mem_ram[0] = vicii_read_phi1();
                machine_handle_pending_alarms(1);
            }
            if (pport.data_set_bit7 && ((value & 0x80) == 0) && pport.data_falloff_bit7 == 0) {
                pport.data_falloff_bit7 = 1;
                pport.data_set_clk_bit7 = maincpu_clk + C64_CPU_DATA_PORT_FALL_OFF_CYCLES;
            }
            if (pport.data_set_bit6 && ((value & 0x40) == 0) && pport.data_falloff_bit6 == 0) {
                pport.data_falloff_bit6 = 1;
                pport.data_set_clk_bit6 = maincpu_clk + C64_CPU_DATA_PORT_FALL_OFF_CYCLES + 20;
            }
            if (pport.data_set_bit7 && (value & 0x80) && pport.data_falloff_bit7) {
                pport.data_falloff_bit7 = 0;
            }
            if (pport.data_set_bit6 && (value & 0x40) && pport.data_falloff_bit6) {
                pport.data_falloff_bit6 = 0;
            }
            if (pport.dir != value) {
                pport.dir = value;
                mem_pla_config_changed();
            }
            break;
        case 1:
            if (vbank == 0) {
                if (c64_256k_enabled) {
                    c64_256k_ram_segment0_store((WORD)1, vicii_read_phi1());
                } else {
                    if (plus256k_enabled) {
                        plus256k_ram_low_store((WORD)1, vicii_read_phi1());
                    } else {
                        mem_ram[1] = vicii_read_phi1();
                    }
                }
            } else {
                mem_ram[1] = vicii_read_phi1();
                machine_handle_pending_alarms(1);
            }
            if ((pport.dir & 0x80) && (value & 0x80)) {
                pport.data_set_bit7 = 1;
            }
            if ((pport.dir & 0x40) && (value & 0x40)) {
                pport.data_set_bit6 = 1;
            }

            if (pport.data != value) {
                pport.data = value;
                mem_pla_config_changed();
            }
            break;
        default:
            if (vbank == 0) {
                if (c64_256k_enabled) {
                    c64_256k_ram_segment0_store(addr, value);
                } else {
                    if (plus256k_enabled) {
                        plus256k_ram_low_store(addr, value);
                    } else {
                        mem_ram[addr] = value;
                    }
                }
            } else {
                mem_ram[addr] = value;
            }
    }
}

/* ------------------------------------------------------------------------- */

BYTE chargen_read(WORD addr)
{
    return mem_chargen_rom[addr & 0xfff];
}

void chargen_store(WORD addr, BYTE value)
{
    mem_chargen_rom[addr & 0xfff] = value;
}

BYTE ram_read(WORD addr)
{
    return mem_ram[addr];
}

void ram_store(WORD addr, BYTE value)
{
    mem_ram[addr] = value;
}

void ram_hi_store(WORD addr, BYTE value)
{
    mem_ram[addr] = value;

    if (addr == 0xff00) {
        reu_dma(-1);
    }
}

/* ------------------------------------------------------------------------- */

/* Generic memory access.  */

void mem_store(WORD addr, BYTE value)
{
    _mem_write_tab_ptr[addr >> 8](addr, value);
}

BYTE mem_read(WORD addr)
{
    return _mem_read_tab_ptr[addr >> 8](addr);
}

void mem_store_without_ultimax(WORD addr, BYTE value)
{
    mem_write_tab[mem_config & 7][addr >> 8](addr, value);
}

BYTE mem_read_without_ultimax(WORD addr)
{
    return mem_read_tab[mem_config & 7][addr >> 8](addr);
}

void mem_store_without_romlh(WORD addr, BYTE value)
{
    mem_write_tab[0][addr >> 8](addr, value);
}


/* ------------------------------------------------------------------------- */

void colorram_store(WORD addr, BYTE value)
{
    mem_color_ram[addr & 0x3ff] = value & 0xf;
}

BYTE colorram_read(WORD addr)
{
    return mem_color_ram[addr & 0x3ff] | (vicii_read_phi1() & 0xf0);
}

/* ------------------------------------------------------------------------- */

/* init 256k memory table changes */

static int check_256k_ram_write(int i, int j)
{
    if (mem_write_tab[i][j] == ram_hi_store || mem_write_tab[i][j] == ram_store) {
        return 1;
    } else {
        return 0;
    }
}

void c64_256k_init_config(void)
{
    int i, j;

    if (c64_256k_enabled) {
        mem_limit_256k_init(mem_read_limit_tab);
        for (i = 0; i < NUM_CONFIGS; i++) {
            for (j = 1; j <= 0xff; j++) {
                if (check_256k_ram_write(i, j) == 1) {
                    if (j < 0x40) {
                        mem_write_tab[i][j] = c64_256k_ram_segment0_store;
                    }
                    if (j > 0x3f && j < 0x80) {
                        mem_write_tab[i][j] = c64_256k_ram_segment1_store;
                    }
                    if (j > 0x7f && j < 0xc0) {
                        mem_write_tab[i][j] = c64_256k_ram_segment2_store;
                    }
                    if (j > 0xbf) {
                        mem_write_tab[i][j] = c64_256k_ram_segment3_store;
                    }
                }
                if (mem_read_tab[i][j] == ram_read) {
                    if (j < 0x40) {
                        mem_read_tab[i][j] = c64_256k_ram_segment0_read;
                    }
                    if (j > 0x3f && j < 0x80) {
                        mem_read_tab[i][j] = c64_256k_ram_segment1_read;
                    }
                    if (j > 0x7f && j < 0xc0) {
                        mem_read_tab[i][j] = c64_256k_ram_segment2_read;
                    }
                    if (j > 0xbf) {
                        mem_read_tab[i][j] = c64_256k_ram_segment3_read;
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------- */

/* init plus256k memory table changes */

void plus256k_init_config(void)
{
    int i, j;

    if (plus256k_enabled) {
        mem_limit_256k_init(mem_read_limit_tab);
        for (i = 0; i < NUM_CONFIGS; i++) {
            for (j = 1; j <= 0xff; j++) {
                if (check_256k_ram_write(i, j) == 1) {
                    if (j < 0x10) {
                        mem_write_tab[i][j] = plus256k_ram_low_store;
                    } else {
                        mem_write_tab[i][j] = plus256k_ram_high_store;
                    }
                }

                if (mem_read_tab[i][j] == ram_read) {
                    if (j < 0x10) {
                        mem_read_tab[i][j] = plus256k_ram_low_read;
                    } else {
                        mem_read_tab[i][j] = plus256k_ram_high_read;
                    }
                }
            }
        }
    }
}

/* init plus60k memory table changes */

static void plus60k_init_config(void)
{
    int i, j;

    if (plus60k_enabled) {
        mem_limit_plus60k_init(mem_read_limit_tab);
        for (i = 0; i < NUM_CONFIGS; i++) {
            for (j = 0x10; j <= 0xff; j++) {
                if (mem_write_tab[i][j] == ram_hi_store) {
                    mem_write_tab[i][j] = plus60k_ram_hi_store;
                }
                if (mem_write_tab[i][j] == ram_store) {
                    mem_write_tab[i][j] = plus60k_ram_store;
                }

                if (mem_read_tab[i][j] == ram_read) {
                    mem_read_tab[i][j] = plus60k_ram_read;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------------- */

void mem_set_write_hook(int config, int page, store_func_t *f)
{
    mem_write_tab[config][page] = f;
}

void mem_read_tab_set(unsigned int base, unsigned int index, read_func_ptr_t read_func)
{
    mem_read_tab[base][index] = read_func;
}

void mem_read_base_set(unsigned int base, unsigned int index, BYTE *mem_ptr)
{
    mem_read_base_tab[base][index] = mem_ptr;
}

void mem_initialize_memory(void)
{
    int i, j;

    mem_chargen_rom_ptr = mem_chargen_rom;
    mem_color_ram_cpu = mem_color_ram;
    mem_color_ram_vicii = mem_color_ram;

    mem_limit_init(mem_read_limit_tab);

    /* Default is RAM.  */
    for (i = 0; i <= 0x100; i++) {
        mem_read_tab_watch[i] = read_watch;
        mem_write_tab_watch[i] = store_watch;
    }

    for (i = 0; i < NUM_CONFIGS; i++) {
        mem_set_write_hook(i, 0, zero_store);
        mem_read_tab[i][0] = zero_read;
        mem_read_base_tab[i][0] = mem_ram;
        for (j = 1; j <= 0xfe; j++) {
            mem_read_tab[i][j] = ram_read;
            mem_read_base_tab[i][j] = mem_ram + (j << 8);
            mem_write_tab[i][j] = ram_store;
        }
        mem_read_tab[i][0xff] = ram_read;
        mem_read_base_tab[i][0xff] = mem_ram + 0xff00;

        /* REU $ff00 trigger is handled within `ram_hi_store()'.  */
        mem_set_write_hook(i, 0xff, ram_hi_store);
    }

    /* Setup character generator ROM at $D000-$DFFF (memory configs 1, 2, 3, 9, 10, 11, 25, 26, 27).  */
    for (i = 0xd0; i <= 0xdf; i++) {
        mem_read_tab[1][i] = chargen_read;
        mem_read_tab[2][i] = chargen_read;
        mem_read_tab[3][i] = chargen_read;
        mem_read_tab[9][i] = chargen_read;
        mem_read_tab[10][i] = chargen_read;
        mem_read_tab[11][i] = chargen_read;
        mem_read_tab[25][i] = chargen_read;
        mem_read_tab[26][i] = chargen_read;
        mem_read_tab[27][i] = chargen_read;
        mem_read_base_tab[1][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[2][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[3][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[9][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[10][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[11][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[25][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[26][i] = mem_chargen_rom + ((i & 0x0f) << 8);
        mem_read_base_tab[27][i] = mem_chargen_rom + ((i & 0x0f) << 8);
    }

    c64meminit(0);

    for (i = 0; i < NUM_CONFIGS; i++) {
        mem_read_tab[i][0x100] = mem_read_tab[i][0];
        mem_write_tab[i][0x100] = mem_write_tab[i][0];
        mem_read_base_tab[i][0x100] = mem_read_base_tab[i][0];
    }

    _mem_read_tab_ptr = mem_read_tab[7];
    _mem_write_tab_ptr = mem_write_tab[7];
    _mem_read_base_tab_ptr = mem_read_base_tab[7];
    mem_read_limit_tab_ptr = mem_read_limit_tab[7];

    vicii_set_chargen_addr_options(0x7000, 0x1000);

    c64pla_pport_reset();
    export.exrom = 0;
    export.game = 0;

    /* Setup initial memory configuration.  */
    mem_pla_config_changed();
    cartridge_init_config();
    /* internal expansions, these may modify the above mappings and must take
       care of hooking up all callbacks correctly.
    */
    plus60k_init_config();
    plus256k_init_config();
    c64_256k_init_config();
}

/* ------------------------------------------------------------------------- */

/* Initialize RAM for power-up.  */
void mem_powerup(void)
{
    ram_init(mem_ram, 0x10000);
    cartridge_ram_init();  /* Clean cartridge ram too */
}

/* ------------------------------------------------------------------------- */

/* Change the current video bank.  Call this routine only when the vbank
   has really changed.  */
void mem_set_vbank(int new_vbank)
{
    vbank = new_vbank;
    vicii_set_vbank(new_vbank);
}

/* Set the tape sense status.  */
void mem_set_tape_sense(int sense)
{
    tape_sense = sense;
    mem_pla_config_changed();
}

void mem_set_bank_pointer(BYTE **base, int *limit)
{
    bank_base = base;
    bank_limit = limit;
}

/* ------------------------------------------------------------------------- */

/* FIXME: this part needs to be checked.  */

void mem_get_basic_text(WORD *start, WORD *end)
{
    if (start != NULL) {
        *start = mem_ram[0x2b] | (mem_ram[0x2c] << 8);
    }
    if (end != NULL) {
        *end = mem_ram[0x2d] | (mem_ram[0x2e] << 8);
    }
}

void mem_set_basic_text(WORD start, WORD end)
{
    mem_ram[0x2b] = mem_ram[0xac] = start & 0xff;
    mem_ram[0x2c] = mem_ram[0xad] = start >> 8;
    mem_ram[0x2d] = mem_ram[0x2f] = mem_ram[0x31] = mem_ram[0xae] = end & 0xff;
    mem_ram[0x2e] = mem_ram[0x30] = mem_ram[0x32] = mem_ram[0xaf] = end >> 8;
}

void mem_inject(DWORD addr, BYTE value)
{
    /* could be made to handle various internal expansions in some sane way */
    mem_ram[addr & 0xffff] = value;
}

/* ------------------------------------------------------------------------- */

int mem_rom_trap_allowed(WORD addr)
{
    if (addr >= 0xe000) {
        switch (mem_config) {
            case 2:
            case 3:
            case 6:
            case 7:
            case 10:
            case 11:
            case 14:
            case 15:
            case 26:
            case 27:
            case 30:
            case 31:
                return 1;
            default: 
                return 0;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------- */

/* Banked memory access functions for the monitor.  */

void store_bank_io(WORD addr, BYTE byte)
{
    switch (addr & 0xff00) {
        case 0xd000:
            c64io_d000_store(addr, byte);
            break;
        case 0xd100:
            c64io_d100_store(addr, byte);
            break;
        case 0xd200:
            c64io_d200_store(addr, byte);
            break;
        case 0xd300:
            c64io_d300_store(addr, byte);
            break;
        case 0xd400:
            c64io_d400_store(addr, byte);
            break;
        case 0xd500:
            c64io_d500_store(addr, byte);
            break;
        case 0xd600:
            c64io_d600_store(addr, byte);
            break;
        case 0xd700:
            c64io_d700_store(addr, byte);
            break;
        case 0xd800:
        case 0xd900:
        case 0xda00:
        case 0xdb00:
            colorram_store(addr, byte);
            break;
        case 0xdc00:
            cia1_store(addr, byte);
            break;
        case 0xdd00:
            cia2_store(addr, byte);
            break;
        case 0xde00:
            c64io_de00_store(addr, byte);
            break;
        case 0xdf00:
            c64io_df00_store(addr, byte);
            break;
    }
    return;
}

BYTE read_bank_io(WORD addr)
{
    switch (addr & 0xff00) {
        case 0xd000:
            return c64io_d000_read(addr);
        case 0xd100:
            return c64io_d100_read(addr);
        case 0xd200:
            return c64io_d200_read(addr);
        case 0xd300:
            return c64io_d300_read(addr);
        case 0xd400:
            return c64io_d400_read(addr);
        case 0xd500:
            return c64io_d500_read(addr);
        case 0xd600:
            return c64io_d600_read(addr);
        case 0xd700:
            return c64io_d700_read(addr);
        case 0xd800:
        case 0xd900:
        case 0xda00:
        case 0xdb00:
            return colorram_read(addr);
        case 0xdc00:
            return cia1_read(addr);
        case 0xdd00:
            return cia2_read(addr);
        case 0xde00:
            return c64io_de00_read(addr);
        case 0xdf00:
            return c64io_df00_read(addr);
    }
    return 0xff;
}

static BYTE peek_bank_io(WORD addr)
{
    switch (addr & 0xff00) {
        case 0xd000:
            return c64io_d000_peek(addr);
        case 0xd100:
            return c64io_d100_peek(addr);
        case 0xd200:
            return c64io_d200_peek(addr);
        case 0xd300:
            return c64io_d300_peek(addr);
        case 0xd400:
            return c64io_d400_peek(addr);
        case 0xd500:
            return c64io_d500_peek(addr);
        case 0xd600:
            return c64io_d600_peek(addr);
        case 0xd700:
            return c64io_d700_peek(addr);
        case 0xd800:
        case 0xd900:
        case 0xda00:
        case 0xdb00:
            return colorram_read(addr);
        case 0xdc00:
            return cia1_peek(addr);
        case 0xdd00:
            return cia2_peek(addr);
        case 0xde00:
            return c64io_de00_peek(addr);
        case 0xdf00:
            return c64io_df00_peek(addr);
    }
    return 0xff;
}

/* ------------------------------------------------------------------------- */

/* Exported banked memory access functions for the monitor.  */

static const char *banknames[] = {
    "default",
    "cpu",
    "ram",
    "rom",
    "io",
    "cart",
    NULL
};

static const int banknums[] = { 1, 0, 1, 2, 3, 4 };

const char **mem_bank_list(void)
{
    return banknames;
}

int mem_bank_from_name(const char *name)
{
    int i = 0;

    while (banknames[i]) {
        if (!strcmp(name, banknames[i])) {
            return banknums[i];
        }
        i++;
    }
    return -1;
}

BYTE mem_bank_read(int bank, WORD addr, void *context)
{
    switch (bank) {
        case 0:                   /* current */
            return mem_read(addr);
            break;
        case 3:                   /* io */
            if (addr >= 0xd000 && addr < 0xe000) {
                return read_bank_io(addr);
            }
        case 4:                   /* cart */
            return cartridge_peek_mem(addr);
        case 2:                   /* rom */
            if (addr >= 0xa000 && addr <= 0xbfff) {
                return c64memrom_basic64_rom[addr & 0x1fff];
            }
            if (addr >= 0xd000 && addr <= 0xdfff) {
                return mem_chargen_rom[addr & 0x0fff];
            }
            if (addr >= 0xe000) {
                return c64memrom_kernal64_rom[addr & 0x1fff];
            }
        case 1:                   /* ram */
            break;
    }
    return mem_ram[addr];
}

BYTE mem_bank_peek(int bank, WORD addr, void *context)
{
    switch (bank) {
        case 0:                   /* current */
             /* we must check for which bank is currently active, and only use peek_bank_io
                when needed to avoid side effects */
            if (c64meminit_io_config[mem_config]) {
                if ((addr >= 0xd000) && (addr < 0xe000)) {
                    return peek_bank_io(addr);
                }
            }
            return mem_read(addr);
            break;
        case 3:                   /* io */
            if (addr >= 0xd000 && addr < 0xe000) {
                return peek_bank_io(addr);
            }
        case 4:                   /* cart */
            return cartridge_peek_mem(addr);
    }
    return mem_bank_read(bank, addr, context);
}

void mem_bank_write(int bank, WORD addr, BYTE byte, void *context)
{
    switch (bank) {
        case 0:                   /* current */
            mem_store(addr, byte);
            return;
        case 3:                   /* io */
            if (addr >= 0xd000 && addr < 0xe000) {
                store_bank_io(addr, byte);
                return;
            }
        case 2:                   /* rom */
            if (addr >= 0xa000 && addr <= 0xbfff) {
                return;
            }
            if (addr >= 0xd000 && addr <= 0xdfff) {
                return;
            }
            if (addr >= 0xe000) {
                return;
            }
        case 1:                   /* ram */
            break;
    }
    mem_ram[addr] = byte;
}

static int mem_dump_io(WORD addr)
{
    if ((addr >= 0xdc00) && (addr <= 0xdc3f)) {
        return ciacore_dump(machine_context.cia1);
    } else if ((addr >= 0xdd00) && (addr <= 0xdd3f)) {
        return ciacore_dump(machine_context.cia2);
    }
    return -1;
}

mem_ioreg_list_t *mem_ioreg_list_get(void *context)
{
    mem_ioreg_list_t *mem_ioreg_list = NULL;

    mon_ioreg_add_list(&mem_ioreg_list, "CIA1", 0xdc00, 0xdc0f, mem_dump_io);
    mon_ioreg_add_list(&mem_ioreg_list, "CIA2", 0xdd00, 0xdd0f, mem_dump_io);

    io_source_ioreg_add_list(&mem_ioreg_list);

    return mem_ioreg_list;
}

void mem_get_screen_parameter(WORD *base, BYTE *rows, BYTE *columns, int *bank)
{
    *base = ((vicii_peek(0xd018) & 0xf0) << 6) | ((~cia2_peek(0xdd00) & 0x03) << 14);
    *rows = 25;
    *columns = 40;
    *bank = 0;
}

/* ------------------------------------------------------------------------- */

void mem_color_ram_to_snapshot(BYTE *color_ram)
{
    memcpy(color_ram, mem_color_ram, 0x400);
}

void mem_color_ram_from_snapshot(BYTE *color_ram)
{
    memcpy(mem_color_ram, color_ram, 0x400);
}
