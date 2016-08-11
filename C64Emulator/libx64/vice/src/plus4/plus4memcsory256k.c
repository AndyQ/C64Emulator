/*
 * plus4memcsory256k.c - CSORY 256K EXPANSION emulation.
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
#include <stdlib.h>
#include <string.h>

#include "cmdline.h"
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "mem.h"
#include "plus4mem.h"
#include "plus4memcsory256k.h"
#include "plus4memhannes256k.h"
#include "resources.h"
#include "snapshot.h"
#include "translate.h"
#include "types.h"
#include "uiapi.h"


static log_t cs256k_log = LOG_ERR;

static int cs256k_activate(void);
static int cs256k_deactivate(void);

int cs256k_enabled = 0;

static int cs256k_block = 0xf;
static int cs256k_segment = 3;

BYTE *cs256k_ram = NULL;


static int set_cs256k_enabled(int val, void *param)
{
    if (!val) {
        if (cs256k_enabled) {
            if (cs256k_deactivate() < 0) {
                return -1;
            }
        }
        cs256k_enabled = 0;
        return 0;
    } else {
        if (!cs256k_enabled) {
            if (cs256k_activate() < 0) {
                return -1;
            }
        }

        cs256k_enabled = 1;

        if (h256k_enabled)
            resources_set_int("H256K", 0);
        resources_set_int("RamSize", 256);

        return 0;
    }
}

static const resource_int_t resources_int[] = {
    { "CS256K", 0, RES_EVENT_SAME, NULL,
      &cs256k_enabled, set_cs256k_enabled, NULL },
    { NULL }
};

int cs256k_resources_init(void)
{
    return resources_register_int(resources_int);
}

/* ------------------------------------------------------------------------- */

static const cmdline_option_t cmdline_options[] =
{
    { "-cs256k", SET_RESOURCE, 0,
      NULL, NULL, "CS256K", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_CS256K_EXPANSION,
      NULL, NULL },
    { NULL }
};

int cs256k_cmdline_options_init(void)
{
  return cmdline_register_options(cmdline_options);
}

/* ------------------------------------------------------------------------- */

void cs256k_init(void)
{
  cs256k_log = log_open("CS256K");
}

void cs256k_reset(void)
{
  cs256k_block=0xf;
  cs256k_segment=3;
}

static int cs256k_activate(void)
{
  cs256k_ram = lib_realloc((void *)cs256k_ram, (size_t)0x40000);

  log_message(cs256k_log, "CSORY 256K expansion installed.");

  cs256k_reset();
  return 0;
}

static int cs256k_deactivate(void)
{
  lib_free(cs256k_ram);
  cs256k_ram = NULL;
  return 0;
}

void cs256k_shutdown(void)
{
  if (cs256k_enabled)
    cs256k_deactivate();
}

/* ------------------------------------------------------------------------- */

BYTE cs256k_reg_read(WORD addr)
{
  return 0xff;
}

void cs256k_reg_store(WORD addr, BYTE value)
{
  cs256k_block=(value&0xf);
  cs256k_segment=(value&0xc0)>>6;
}

void cs256k_store(WORD addr, BYTE value)
{
  if (addr>=(cs256k_segment*0x4000) && addr<((cs256k_segment+1)*0x4000))
    cs256k_ram[(cs256k_block*0x4000)+(addr&0x3fff)]=value;
  else
    mem_ram[addr]=value;
}

BYTE cs256k_read(WORD addr)
{
  if (addr>=(cs256k_segment*0x4000) && addr<((cs256k_segment+1)*0x4000))
    return cs256k_ram[(cs256k_block*0x4000)+(addr&0x3fff)];
  else
    return mem_ram[addr];
}
