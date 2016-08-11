/*
 * vicii-cmdline-options.c - Command-line options for the MOS 6569 (VIC-II)
 * emulation.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andreas Boose <viceteam@t-online.de>
 *  Gunnar Ruthenberg <Krill.Plush@gmail.com>
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
 * */

#include "vice.h"

#include <stdio.h>
#include <string.h>

#include "cmdline.h"
#include "machine.h"
#include "raster-cmdline-options.h"
#include "resources.h"
#include "translate.h"
#include "vicii-cmdline-options.h"
#include "vicii-resources.h"
#include "vicii-timing.h"
#include "vicii.h"
#include "viciitypes.h"


int border_set_func(const char *value, void *extra_param)
{
   int video;

   resources_get_int("MachineVideoStandard", &video);

   if (strcmp(value, "1") == 0 || strcmp(value, "full") == 0) {
       vicii_resources.border_mode = VICII_FULL_BORDERS;
   } else if (strcmp(value, "2") == 0 || strcmp(value, "debug") == 0) {
       vicii_resources.border_mode = VICII_DEBUG_BORDERS;
   } else if (strcmp(value, "3") == 0 || strcmp(value, "none") == 0) {
       vicii_resources.border_mode = VICII_NO_BORDERS;
   } else {
       vicii_resources.border_mode = VICII_NORMAL_BORDERS;
   }

   machine_change_timing(video ^ VICII_BORDER_MODE(vicii_resources.border_mode));

   return 0;
}

struct model_s {
    const char *name;
    int model;
};

static struct model_s model_match[] = {
    /* PAL, 63 cycle, 9 luma, "old" */
    { "6569", VICII_MODEL_6569 },
    { "6569r3", VICII_MODEL_6569 },
    { "pal", VICII_MODEL_6569 },

    /* PAL, 63 cycle, 9 luma, "new" */
    { "8565", VICII_MODEL_8565 },
    { "newpal", VICII_MODEL_8565 },

    /* PAL, 63 cycle, 5 luma, "old" */
    { "6569r1", VICII_MODEL_6569R1 },
    { "oldpal", VICII_MODEL_6569R1 },

    /* NTSC, 65 cycle, 9 luma, "old" */
    { "6567", VICII_MODEL_6567 },
    { "ntsc", VICII_MODEL_6567 },

    /* NTSC, 65 cycle, 9 luma, "new" */
    { "8562", VICII_MODEL_8562 },
    { "newntsc", VICII_MODEL_8562 },

    /* NTSC, 64 cycle, ? luma, "old" */
    { "6567r56a", VICII_MODEL_6567R56A },
    { "oldntsc", VICII_MODEL_6567R56A },

    /* PAL-N, 65 cycle, ? luma, "old" */
    { "6572", VICII_MODEL_6572 },
    { "paln", VICII_MODEL_6572 },
    { "drean", VICII_MODEL_6572 },

    { NULL, -1 }
};

static int set_vicii_model(const char *param, void *extra_param)
{
    int model = -1;
    int i = 0;

    if (!param) {
        return -1;
    }

    do {
        if (strcmp(model_match[i].name, param) == 0) {
            model = model_match[i].model;
        }
        i++;
    } while ((model == -1) && (model_match[i].name != NULL));

    if (model == -1) {
        return -1;
    }

    return resources_set_int("VICIIModel", model);
}

/* VIC-II command-line options.  */
static const cmdline_option_t cmdline_options[] =
{
    { "-VICIIborders", CALL_FUNCTION, 1,
      border_set_func, NULL, "VICIIBorderMode", (void *)0,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_MODE, IDCLS_SET_BORDER_MODE,
      NULL, NULL },
    { "-VICIIchecksb", SET_RESOURCE, 0,
      NULL, NULL, "VICIICheckSbColl", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_SPRITE_BACKGROUND,
      NULL, NULL },
    { "+VICIIchecksb", SET_RESOURCE, 0,
      NULL, NULL, "VICIICheckSbColl", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_SPRITE_BACKGROUND,
      NULL, NULL },
    { "-VICIIcheckss", SET_RESOURCE, 0,
      NULL, NULL, "VICIICheckSsColl", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_SPRITE_SPRITE,
      NULL, NULL },
    { "+VICIIcheckss", SET_RESOURCE, 0,
      NULL, NULL, "VICIICheckSsColl", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_SPRITE_SPRITE,
      NULL, NULL },
    { "-VICIInewluminance", SET_RESOURCE, 0,
      NULL, NULL, "VICIINewLuminances", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_USE_NEW_LUMINANCES,
      NULL, NULL },
    { "+VICIInewluminance", SET_RESOURCE, 0,
      NULL, NULL, "VICIINewLuminances", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_USE_OLD_LUMINANCES,
      NULL, NULL },
    { "-VICIImodel", CALL_FUNCTION, 1,
      set_vicii_model, NULL, NULL, NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_MODEL, IDCLS_SET_VICII_MODEL,
      NULL, NULL },
    CMDLINE_LIST_END
};

int vicii_cmdline_options_init(void)
{
    if (raster_cmdline_options_chip_init("VICII", vicii.video_chip_cap) < 0) {
        return -1;
    }

    return cmdline_register_options(cmdline_options);
}
