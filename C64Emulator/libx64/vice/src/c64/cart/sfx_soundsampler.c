/*
 * sfx_soundsampler.c - SFX Sound Sampler cartridge emulation.
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

#include "c64export.h"
#include "cartio.h"
#include "cartridge.h"
#include "cmdline.h"
#include "lib.h"
#include "machine.h"
#include "maincpu.h"
#include "resources.h"
#include "sfx_soundsampler.h"
#include "sid.h"
#include "snapshot.h"
#include "sound.h"
#include "uiapi.h"
#include "translate.h"

/* ------------------------------------------------------------------------- */

/* some prototypes are needed */
static void sfx_soundsampler_sound_store(WORD addr, BYTE value);

static io_source_t sfx_soundsampler_device = {
    CARTRIDGE_NAME_SFX_SOUND_SAMPLER,
    IO_DETACH_RESOURCE,
    "SFXSoundSampler",
    0xde00, 0xdeff, 0x01,
    0,
    sfx_soundsampler_sound_store,
    NULL,
    NULL, /* TODO: peek */
    NULL, /* TODO: dump */
    CARTRIDGE_SFX_SOUND_SAMPLER,
    0,
    0
};

static io_source_list_t *sfx_soundsampler_list_item = NULL;

static const c64export_resource_t export_res= {
    CARTRIDGE_NAME_SFX_SOUND_SAMPLER, 0, 0, &sfx_soundsampler_device, NULL, CARTRIDGE_SFX_SOUND_SAMPLER
};

/* ------------------------------------------------------------------------- */

/* Some prototypes are needed */
static int sfx_soundsampler_sound_machine_init(sound_t *psid, int speed, int cycles_per_sec);
static int sfx_soundsampler_sound_machine_calculate_samples(sound_t **psid, SWORD *pbuf, int nr, int sound_output_channels, int sound_chip_channels, int *delta_t);
static void sfx_soundsampler_sound_machine_store(sound_t *psid, WORD addr, BYTE val);
static BYTE sfx_soundsampler_sound_machine_read(sound_t *psid, WORD addr);
static void sfx_soundsampler_sound_reset(sound_t *psid, CLOCK cpu_clk);

static int sfx_soundsampler_sound_machine_cycle_based(void)
{
	return 0;
}

static int sfx_soundsampler_sound_machine_channels(void)
{
	return 1;
}

static sound_chip_t sfx_soundsampler_sound_chip = {
    NULL, /* no open */
    sfx_soundsampler_sound_machine_init,
    NULL, /* no close */
    sfx_soundsampler_sound_machine_calculate_samples,
    sfx_soundsampler_sound_machine_store,
    sfx_soundsampler_sound_machine_read,
    sfx_soundsampler_sound_reset,
    sfx_soundsampler_sound_machine_cycle_based,
    sfx_soundsampler_sound_machine_channels,
    0 /* chip enabled */
};

static WORD sfx_soundsampler_sound_chip_offset = 0;

void sfx_soundsampler_sound_chip_init(void)
{
    sfx_soundsampler_sound_chip_offset = sound_chip_register(&sfx_soundsampler_sound_chip);
}

/* ------------------------------------------------------------------------- */

static int sfx_soundsampler_io_swap = 0;

int sfx_soundsampler_cart_enabled(void)
{
    return sfx_soundsampler_sound_chip.chip_enabled;
}

static int set_sfx_soundsampler_enabled(int val, void *param)
{
    if (sfx_soundsampler_sound_chip.chip_enabled != val) {
        if (val) {
            if (c64export_add(&export_res) < 0) {
                return -1;
            }
            if (machine_class == VICE_MACHINE_VIC20) {
                if (sfx_soundsampler_io_swap) {
                    sfx_soundsampler_device.start_address = 0x9c00;
                    sfx_soundsampler_device.end_address = 0x9fff;
                } else {
                    sfx_soundsampler_device.start_address = 0x9800;
                    sfx_soundsampler_device.end_address = 0x9bff;
                }
            }
            sfx_soundsampler_list_item = io_source_register(&sfx_soundsampler_device);
            sfx_soundsampler_sound_chip.chip_enabled = 1;
        } else {
            c64export_remove(&export_res);
            io_source_unregister(sfx_soundsampler_list_item);
            sfx_soundsampler_list_item = NULL;
            sfx_soundsampler_sound_chip.chip_enabled = 0;
        }
    }
    return 0;
}

static int set_sfx_soundsampler_io_swap(int val, void *param)
{
    if (val == sfx_soundsampler_io_swap) {
        return 0;
    }

    if (sfx_soundsampler_sound_chip.chip_enabled) {
        set_sfx_soundsampler_enabled(0, NULL);
        sfx_soundsampler_io_swap = val;
        set_sfx_soundsampler_enabled(1, NULL);
    } else {
        sfx_soundsampler_io_swap = val;
    }
    return 0;
}

void sfx_soundsampler_reset(void)
{
    /* TODO: do nothing ? */
}

int sfx_soundsampler_enable(void)
{
    return resources_set_int("SFXSoundSampler", 1);
}

void sfx_soundsampler_detach(void)
{
    resources_set_int("SFXSoundSampler", 0);
}

/* ------------------------------------------------------------------------- */

static const resource_int_t resources_int[] = {
    { "SFXSoundSampler", 0, RES_EVENT_STRICT, (resource_value_t)0,
      &sfx_soundsampler_sound_chip.chip_enabled, set_sfx_soundsampler_enabled, NULL },
    { NULL }
};

static const resource_int_t resources_mascuerade_int[] = {
    { "SFXSoundSamplerIOSwap", 0, RES_EVENT_STRICT, (resource_value_t)0,
      &sfx_soundsampler_io_swap, set_sfx_soundsampler_io_swap, NULL },
    { NULL }
};

int sfx_soundsampler_resources_init(void)
{
    if (machine_class == VICE_MACHINE_VIC20) {
        if (resources_register_int(resources_mascuerade_int) < 0) {
            return -1;
        }
    }
    return resources_register_int(resources_int);
}

void sfx_soundsampler_resources_shutdown(void)
{
}

static const cmdline_option_t cmdline_options[] =
{
    { "-sfxss", SET_RESOURCE, 0,
      NULL, NULL, "SFXSoundSampler", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_SFX_SS,
      NULL, NULL },
    { "+sfxss", SET_RESOURCE, 0,
      NULL, NULL, "SFXSoundSampler", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_SFX_SS,
      NULL, NULL },
    { NULL }
};

static const cmdline_option_t cmdline_mascuerade_options[] =
{
    { "-sfxssioswap", SET_RESOURCE, 0,
      NULL, NULL, "SFXSoundSamplerIOSwap", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_STRING,
      IDCLS_UNUSED, IDCLS_UNUSED,
      NULL, T_("Swap io mapping (map SFX SS I/O to VIC20 I/O-3") },
    { "+sfxssioswap", SET_RESOURCE, 0,
      NULL, NULL, "SFXSoundSamplerIOSwap", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_STRING,
      IDCLS_UNUSED, IDCLS_UNUSED,
      NULL, T_("Don't swap io mapping (map SFX SS I/O to VIC20 I/O-2") },
    { NULL }
};

int sfx_soundsampler_cmdline_options_init(void)
{
    if (machine_class == VICE_MACHINE_VIC20) {
        if (cmdline_register_options(cmdline_mascuerade_options) < 0) {
            return -1;
        }
    }
    return cmdline_register_options(cmdline_options);
}

/* ---------------------------------------------------------------------*/

static BYTE sfx_soundsampler_sound_data;

static void sfx_soundsampler_sound_store(WORD addr, BYTE value)
{
    sfx_soundsampler_sound_data = value;
    sound_store(sfx_soundsampler_sound_chip_offset, value, 0);
}

struct sfx_soundsampler_sound_s
{
    BYTE voice0;
};

static struct sfx_soundsampler_sound_s snd;

static int sfx_soundsampler_sound_machine_calculate_samples(sound_t **psid, SWORD *pbuf, int nr, int soc, int scc, int *delta_t)
{
    int i;

    for (i = 0; i < nr; i++) {
        pbuf[i * soc] = sound_audio_mix(pbuf[i * soc], snd.voice0 << 8);
        if (soc > 1) {
            pbuf[(i * soc) + 1] = sound_audio_mix(pbuf[(i * soc) + 1], snd.voice0 << 8);
        }
    }
    return nr;
}

static int sfx_soundsampler_sound_machine_init(sound_t *psid, int speed, int cycles_per_sec)
{
    snd.voice0 = 0;

    return 1;
}

static void sfx_soundsampler_sound_machine_store(sound_t *psid, WORD addr, BYTE val)
{
    snd.voice0 = val;
}

static BYTE sfx_soundsampler_sound_machine_read(sound_t *psid, WORD addr)
{
    return sfx_soundsampler_sound_data;
}

static void sfx_soundsampler_sound_reset(sound_t *psid, CLOCK cpu_clk)
{
    snd.voice0 = 0;
    sfx_soundsampler_sound_data = 0;
}

/* ---------------------------------------------------------------------*/
/*    snapshot support functions                                             */

#define CART_DUMP_VER_MAJOR   0
#define CART_DUMP_VER_MINOR   0
#define SNAP_MODULE_NAME  "CARTSFXSS"

int sfx_soundsampler_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;

    m = snapshot_module_create(s, SNAP_MODULE_NAME, CART_DUMP_VER_MAJOR, CART_DUMP_VER_MINOR);
    if (m == NULL) {
        return -1;
    }

    if (0 || (SMW_B(m, (BYTE)sfx_soundsampler_sound_data) < 0)) {
        snapshot_module_close(m);
        return -1;
    }

    snapshot_module_close(m);
    return 0;
}

int sfx_soundsampler_snapshot_read_module(snapshot_t *s)
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

    if (0 || (SMR_B(m, &sfx_soundsampler_sound_data) < 0)) {
        snapshot_module_close(m);
        return -1;
    }

    if (!sfx_soundsampler_sound_chip.chip_enabled) {
        set_sfx_soundsampler_enabled(1, NULL);
    }
    sound_store(sfx_soundsampler_sound_chip_offset, sfx_soundsampler_sound_data, 0);

    snapshot_module_close(m);
    return 0;
}
