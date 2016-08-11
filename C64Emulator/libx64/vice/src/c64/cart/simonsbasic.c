/*
 * simonsbasic.c - Cartridge handling, Simons Basic cart.
 *
 * Written by
 *  Groepaz <groepaz@gmx.net>
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

#define CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64cartsystem.h"
#undef CARTRIDGE_INCLUDE_SLOTMAIN_API
#include "c64export.h"
#include "cartio.h"
#include "cartridge.h"
#include "simonsbasic.h"
#include "snapshot.h"
#include "types.h"
#include "util.h"
#include "crt.h"

/*
    Simon's Basic Cartridge

    - 16kb ROM

    - reading io1 switches to 8k game config (bank 0 at $8000)
    - writing io1 switches to 16k game config (bank 0 at $8000, bank 1 at $a000)
*/

static BYTE simon_io1_read(WORD addr)
{
    cart_config_changed_slotmain(0, 0, CMODE_READ);
    return 0;
}

static BYTE simon_io1_peek(WORD addr)
{
    return 0;
}

static void simon_io1_store(WORD addr, BYTE value)
{
    cart_config_changed_slotmain(1, 1, CMODE_WRITE);
}

/* ---------------------------------------------------------------------*/

static io_source_t simon_device = {
    CARTRIDGE_NAME_SIMONS_BASIC,
    IO_DETACH_CART,
    NULL,
    0xde00, 0xdeff, 0xff,
    0, /* read is never valid */
    simon_io1_store,
    simon_io1_read,
    simon_io1_peek,
    NULL, /* TODO: dump */
    CARTRIDGE_SIMONS_BASIC,
    0
};

static io_source_list_t *simon_list_item = NULL;

static const c64export_resource_t export_res_simon = {
    CARTRIDGE_NAME_SIMONS_BASIC, 1, 1, &simon_device, NULL, CARTRIDGE_SIMONS_BASIC
};

/* ---------------------------------------------------------------------*/

void simon_config_init(void)
{
    cart_config_changed_slotmain(1, 1, CMODE_READ);
}

void simon_config_setup(BYTE *rawcart)
{
    memcpy(roml_banks, rawcart, 0x2000);
    memcpy(romh_banks, &rawcart[0x2000], 0x2000);
    cart_config_changed_slotmain(1, 1, CMODE_READ);
}

static int simon_common_attach(void)
{
    if (c64export_add(&export_res_simon) < 0) {
        return -1;
    }
    simon_list_item = io_source_register(&simon_device);
    return 0;
}

int simon_bin_attach(const char *filename, BYTE *rawcart)
{
    if (util_file_load(filename, rawcart, 0x4000, UTIL_FILE_LOAD_SKIP_ADDRESS) < 0) {
        return -1;
    }
    return simon_common_attach();
}

int simon_crt_attach(FILE *fd, BYTE *rawcart)
{
    crt_chip_header_t chip;
    int i;

    for (i = 0; i <= 1; i++) {
        if (crt_read_chip_header(&chip, fd)) {
            return -1;
        }

        if ((chip.start != 0x8000 && chip.start != 0xa000) || chip.size != 0x2000) {
            return -1;
        }

        if (crt_read_chip(rawcart, chip.start - 0x8000, &chip, fd)) {
            return -1;
        }
    }

    return simon_common_attach();
}

void simon_detach(void)
{
    c64export_remove(&export_res_simon);
    io_source_unregister(simon_list_item);
    simon_list_item = NULL;
}

/* ---------------------------------------------------------------------*/

#define CART_DUMP_VER_MAJOR   0
#define CART_DUMP_VER_MINOR   0
#define SNAP_MODULE_NAME  "CARTSIMON"

int simon_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;

    m = snapshot_module_create(s, SNAP_MODULE_NAME,
                          CART_DUMP_VER_MAJOR, CART_DUMP_VER_MINOR);
    if (m == NULL) {
        return -1;
    }

    if (0
        || (SMW_BA(m, roml_banks, 0x2000) < 0)
        || (SMW_BA(m, romh_banks, 0x2000) < 0)) {
        snapshot_module_close(m);
        return -1;
    }

    snapshot_module_close(m);
    return 0;
}

int simon_snapshot_read_module(snapshot_t *s)
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
        || (SMR_BA(m, roml_banks, 0x2000) < 0)
        || (SMR_BA(m, romh_banks, 0x2000) < 0)) {
        snapshot_module_close(m);
        return -1;
    }

    snapshot_module_close(m);

    return simon_common_attach();
}
