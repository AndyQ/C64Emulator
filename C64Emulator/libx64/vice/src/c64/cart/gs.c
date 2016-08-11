/*
 * gs.c - Cartridge handling, GS cart.
 *
 * Written by
 *  Marco van den Heuvel <blackystardust68@yahoo.com>
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

#define CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64cartsystem.h"
#undef CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64export.h"
#include "cartio.h"
#include "cartridge.h"
#include "gs.h"
#include "monitor.h"
#include "snapshot.h"
#include "types.h"
#include "util.h"
#include "crt.h"

/*
    GS Cartridge

    - 512kb ROM

    - reading from io1 switches to 8k game config

    - writing to io1 switches to 16k game config. the lower 6 bits
      of the address are the bank number
*/

static int currbank = 0;
static BYTE regval = 0;

static void gs_io1_store(WORD addr, BYTE value)
{
    regval = value;
    cart_romlbank_set_slotmain(addr & 0x3f);
    cart_set_port_exrom_slotmain(1);
    cart_set_port_game_slotmain(0);
    cart_port_config_changed_slotmain();
    currbank = addr & 0x3f;
}

static BYTE gs_io1_read(WORD addr)
{
    cart_config_changed_slotmain(0, 0, CMODE_READ);
    return 0;
}

static BYTE gs_io1_peek(WORD addr)
{
    return regval;
}

static int gs_dump(void)
{
    mon_out("Bank: %d\n", currbank);
    return 0;
}

/* ---------------------------------------------------------------------*/

static io_source_t gs_device = {
    CARTRIDGE_NAME_GS,
    IO_DETACH_CART,
    NULL,
    0xde00, 0xdeff, 0xff,
    0, /* read is never valid */
    gs_io1_store,
    gs_io1_read,
    gs_io1_peek,
    gs_dump,
    CARTRIDGE_GS,
    0,
    0
};

static io_source_list_t *gs_list_item = NULL;

static const c64export_resource_t export_res = {
    CARTRIDGE_NAME_GS, 1, 1, &gs_device, NULL, CARTRIDGE_GS
};

/* ---------------------------------------------------------------------*/

void gs_config_init(void)
{
    cart_config_changed_slotmain(0, 0, CMODE_READ);
    gs_io1_store((WORD)0xde00, 0);
}

void gs_config_setup(BYTE *rawcart)
{
    memcpy(roml_banks, rawcart, 0x2000 * 64);
    memcpy(romh_banks, &rawcart[0x2000 * 16], 0x2000 * 16);

    /* Hack: using 16kB configuration, but some carts are 8kB only */
    cart_config_changed_slotmain(1, 1, CMODE_READ);
}

/* ---------------------------------------------------------------------*/
static int gs_common_attach(void)
{
    if (c64export_add(&export_res) < 0) {
        return -1;
    }
    gs_list_item = io_source_register(&gs_device);
    return 0;
}

int gs_bin_attach(const char *filename, BYTE *rawcart)
{
    if (util_file_load(filename, rawcart, 0x80000, UTIL_FILE_LOAD_SKIP_ADDRESS) < 0) {
        return -1;
    }
    return gs_common_attach();
}

int gs_crt_attach(FILE *fd, BYTE *rawcart)
{
    crt_chip_header_t chip;

    while (1) {
        if (crt_read_chip_header(&chip, fd)) {
            break;
        }
        if (chip.bank > 63 || (chip.start != 0x8000 && chip.start != 0xa000) || chip.size != 0x2000) {
            return -1;
        }
        if (crt_read_chip(rawcart, chip.bank << 13, &chip, fd)) {
            return -1;
        }
    }
    return gs_common_attach();
}

void gs_detach(void)
{
    c64export_remove(&export_res);
    io_source_unregister(gs_list_item);
    gs_list_item = NULL;
}

/* ---------------------------------------------------------------------*/

#define CART_DUMP_VER_MAJOR   0
#define CART_DUMP_VER_MINOR   0
#define SNAP_MODULE_NAME  "CARTGS"

int gs_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;

    m = snapshot_module_create(s, SNAP_MODULE_NAME,
                          CART_DUMP_VER_MAJOR, CART_DUMP_VER_MINOR);
    if (m == NULL) {
        return -1;
    }

    if (0
        || (SMW_B(m, (BYTE)currbank) < 0)
        || (SMW_BA(m, roml_banks, 0x2000 * 64) < 0)
        || (SMW_BA(m, romh_banks, 0x2000 * 16) < 0)) {
        snapshot_module_close(m);
        return -1;
    }

    snapshot_module_close(m);
    return 0;
}

int gs_snapshot_read_module(snapshot_t *s)
{
    BYTE vmajor, vminor;
    snapshot_module_t *m;

    m = snapshot_module_open(s, SNAP_MODULE_NAME, &vmajor, &vminor);
    if (m == NULL) {
        return -1;
    }

    if ((vmajor != CART_DUMP_VER_MAJOR) || (vminor != CART_DUMP_VER_MINOR)) {
        snapshot_module_close(m);
        return -1;
    }

    if (0
        || (SMR_B_INT(m, &currbank) < 0)
        || (SMR_BA(m, roml_banks, 0x2000 * 64) < 0)
        || (SMR_BA(m, romh_banks, 0x2000 * 16) < 0)) {
        snapshot_module_close(m);
        return -1;
    }

    snapshot_module_close(m);

    return gs_common_attach();
}
