/*
 * funplay.c - Cartridge handling, Fun Play cart.
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
#include "c64mem.h"
#include "cartio.h"
#include "cartridge.h"
#include "funplay.h"
#include "monitor.h"
#include "snapshot.h"
#include "types.h"
#include "util.h"
#include "crt.h"


/*
    "Fun Play" and "Power Play" game cartridges

    the funplay/powerplay carts have 16 banks of ROM at $8000-9FFF,
    $DE00 is used for bank switching according to the following scheme:

    $DE00 bit 76543210
              xx210xx3

    leading to the following values:

    $00 -> Bank 0
    $08 -> Bank 1
    $10 -> Bank 2
    $18 -> Bank 3
    $20 -> Bank 4
    $28 -> Bank 5
    $30 -> Bank 6
    $38 -> Bank 7
    $01 -> Bank 8
    $09 -> Bank 9
    $11 -> Bank 10
    $19 -> Bank 11
    $21 -> Bank 12
    $29 -> Bank 13
    $31 -> Bank 14
    $39 -> Bank 15
*/

static int currbank = 0;
static BYTE regval = 0;

static void funplay_io1_store(WORD addr, BYTE value)
{
    currbank = ((value >> 3) & 7) | ((value & 1) << 3);
    cart_romhbank_set_slotmain(currbank);
    cart_romlbank_set_slotmain(currbank);
    cart_set_port_exrom_slotmain(1);
    cart_set_port_game_slotmain(1);
    cart_set_port_phi1_slotmain(0);
    cart_set_port_phi2_slotmain(0);
    cart_port_config_changed_slotmain();
}

static BYTE funplay_io1_peek(WORD addr)
{
    return regval;
}

static int funplay_dump(void)
{
    mon_out("Bank: %d\n", currbank);
    return 0;
}

/* ---------------------------------------------------------------------*/

static io_source_t funplay_device = {
    CARTRIDGE_NAME_FUNPLAY,
    IO_DETACH_CART,
    NULL,
    0xde00, 0xdeff, 0xff,
    0,
    funplay_io1_store,
    NULL,
    funplay_io1_peek,
    funplay_dump,
    CARTRIDGE_FUNPLAY,
    0,
    0
};

static io_source_list_t *funplay_list_item = NULL;

static const c64export_resource_t export_res = {
    CARTRIDGE_NAME_FUNPLAY, 1, 1, &funplay_device, NULL, CARTRIDGE_FUNPLAY
};

/* ---------------------------------------------------------------------*/

void funplay_config_init(void)
{
    cart_config_changed_slotmain(1, 1, CMODE_READ);
    funplay_io1_store((WORD)0xde00, 0);
}

void funplay_config_setup(BYTE *rawcart)
{
    memcpy(roml_banks, rawcart, 0x2000 * 64);
    memcpy(romh_banks, &rawcart[0x2000 * 16], 0x2000 * 16);

    /* Hack: using 16kB configuration, but some carts are 8kB only */
    cart_config_changed_slotmain(1, 1, CMODE_READ);
}

/* ---------------------------------------------------------------------*/
static int funplay_common_attach(void)
{
    if (c64export_add(&export_res) < 0) {
        return -1;
    }
    funplay_list_item = io_source_register(&funplay_device);
    return 0;
}

int funplay_bin_attach(const char *filename, BYTE *rawcart)
{
    if (util_file_load(filename, rawcart, 0x20000, UTIL_FILE_LOAD_SKIP_ADDRESS) < 0) {
        return -1;
    }
    return funplay_common_attach();
}

int funplay_crt_attach(FILE *fd, BYTE *rawcart)
{
    crt_chip_header_t chip;

    while (1) {
        if (crt_read_chip_header(&chip, fd)) {
            break;
        }
        if ((chip.start != 0x8000 && chip.start != 0xa000) || chip.size != 0x2000) {
            return -1;
        }
        if (crt_read_chip(rawcart, (((chip.bank >> 3) & 7) | ((chip.bank & 1) << 3)) << 13, &chip, fd)) {
            return -1;
        }
    }
    return funplay_common_attach();
}

void funplay_detach(void)
{
    io_source_unregister(funplay_list_item);
    funplay_list_item = NULL;
    c64export_remove(&export_res);
}

/* ---------------------------------------------------------------------*/

#define CART_DUMP_VER_MAJOR   0
#define CART_DUMP_VER_MINOR   0
#define SNAP_MODULE_NAME  "CARTFUNPLAY"

int funplay_snapshot_write_module(snapshot_t *s)
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

int funplay_snapshot_read_module(snapshot_t *s)
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

    return funplay_common_attach();
}
