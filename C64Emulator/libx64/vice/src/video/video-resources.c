/*
 * video-resources.c - Resources for the video layer
 *
 * Written by
 *  John Selck <graham@cruise.de>
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

/* #define DEBUG_VIDEO */

#ifdef DEBUG_VIDEO
#define DBG(_x_)        log_debug _x_
#else
#define DBG(_x_)
#endif

#include "vice.h"

#include <stdio.h>
#include <string.h>

#include "lib.h"
#include "resources.h"
#include "video-color.h"
#include "video.h"
#include "videoarch.h"
#include "viewport.h"
#include "util.h"
#include "log.h"

int video_resources_init(void)
{
    return video_arch_resources_init();
}

void video_resources_shutdown(void)
{
    video_arch_resources_shutdown();
}

/*-----------------------------------------------------------------------*/
/* Per chip resources.  */

static int video_resources_update_ui(video_canvas_t *canvas);

struct video_resource_chip_mode_s {
    video_canvas_t *resource_chip;
    unsigned int device;
};
typedef struct video_resource_chip_mode_s video_resource_chip_mode_t;

static int set_double_size_enabled(int val, void *param)
{
    cap_render_t *cap_render;
    video_canvas_t *canvas = (video_canvas_t *)param;
    int old_doublesizex, old_doublesizey;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    if (val) {
        cap_render = &video_chip_cap->double_mode;
    } else {
        cap_render = &video_chip_cap->single_mode;
    }

    canvas->videoconfig->rendermode = cap_render->rmode;

    old_doublesizex = canvas->videoconfig->doublesizex;
    old_doublesizey = canvas->videoconfig->doublesizey;

    if (cap_render->sizex > 1
        && (video_chip_cap->dsize_limit_width == 0
            || (canvas->draw_buffer->canvas_width
                   <= video_chip_cap->dsize_limit_width))
        ) {
        canvas->videoconfig->doublesizex = (cap_render->sizex - 1);
    } else {
        canvas->videoconfig->doublesizex = 0;
    }

    if (cap_render->sizey > 1
        && (video_chip_cap->dsize_limit_height == 0
            || (canvas->draw_buffer->canvas_height
                   <= video_chip_cap->dsize_limit_height))
        ) {
        canvas->videoconfig->doublesizey = (cap_render->sizey - 1);
    } else {
        canvas->videoconfig->doublesizey = 0;
    }


    DBG(("set_double_size_enabled sizex:%d sizey:%d doublesizex:%d doublesizey:%d rendermode:%d", cap_render->sizex, cap_render->sizey, canvas->videoconfig->doublesizex, canvas->videoconfig->doublesizey, canvas->videoconfig->rendermode));

    if ((canvas->videoconfig->double_size_enabled != val
         || old_doublesizex != canvas->videoconfig->doublesizex
         || old_doublesizey != canvas->videoconfig->doublesizey)
        && canvas->initialized
        && canvas->viewport->update_canvas > 0) {
        video_viewport_resize(canvas, 1);
    }

    canvas->videoconfig->double_size_enabled = val;

    video_resources_update_ui(canvas);

    return 0;
}

static const char *vname_chip_size[] = { "DoubleSize", NULL };

static resource_int_t resources_chip_size[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_double_size_enabled, NULL },
    RESOURCE_INT_LIST_END
};

static int set_double_scan_enabled(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;

    canvas->videoconfig->doublescan = val;

    if (canvas->initialized) {
        video_canvas_refresh_all(canvas);
    }

    video_resources_update_ui(canvas);

    return 0;
}

static const char *vname_chip_scan[] = { "DoubleScan", NULL };

static resource_int_t resources_chip_scan[] =
{
    { NULL, 1, RES_EVENT_NO, NULL,
      NULL, set_double_scan_enabled, NULL },
    RESOURCE_INT_LIST_END
};

static int hwscale_possible;

static int set_hwscale_possible(int val, void *param)
{
    hwscale_possible = val;

    return 0;
}

static int set_hwscale_enabled(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;

    if (val
        && !canvas->videoconfig->hwscale
        && !hwscale_possible) {
        log_message(LOG_DEFAULT, "HW scale not available, forcing to disabled");
        return 0;
    }

    canvas->videoconfig->hwscale = val;

    if (canvas->initialized) {
        video_viewport_resize(canvas, 1);
        video_color_update_palette(canvas);
    }

    video_resources_update_ui(canvas);

    return 0;
}

static const char *vname_chip_hwscale[] = { "HwScale", NULL };

static resource_int_t resources_chip_hwscale[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_hwscale_enabled, NULL },
    RESOURCE_INT_LIST_END
};

static resource_int_t resources_chip_hwscale_possible[] =
{
    { "HwScalePossible",
#ifdef HAVE_HWSCALE
      1,
#else
      0,
#endif
      RES_EVENT_NO, NULL,
      &hwscale_possible, set_hwscale_possible, NULL },
    RESOURCE_INT_LIST_END
};

static int set_chip_rendermode(int val, void *param)
{
    char *chip, *dsize;
    int old, err;
    video_canvas_t *canvas = (video_canvas_t *)param;

    old = canvas->videoconfig->filter;
    chip = canvas->videoconfig->chip_name;

    DBG(("set_chip_rendermode %s (%d->%d)", chip, old, val));

    dsize = util_concat(chip, "DoubleSize", NULL);

    canvas->videoconfig->filter = val;
    canvas->videoconfig->scale2x = 0; /* FIXME: remove this */
    err = 0;
    switch (val) {
        case VIDEO_FILTER_NONE:
            break;
        case VIDEO_FILTER_CRT:
            if (video_color_update_palette(canvas) < 0) {
                err = 1;
            }
            break;
        case VIDEO_FILTER_SCALE2X:
            /* set double size */
            if (resources_set_int(dsize, 1) < 0) {
                err = 1;
            }
            canvas->videoconfig->scale2x = 1; /* FIXME: remove this */
            break;
    }

    if (err) {
        canvas->videoconfig->filter = old;
    }

    lib_free(dsize);

    if (canvas->initialized) {
        video_canvas_refresh_all(canvas);
    }

    video_resources_update_ui(canvas);

    return 0;
}

static const char *vname_chip_rendermode[] = { "Filter", NULL };

static resource_int_t resources_chip_rendermode[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_chip_rendermode, NULL },
    RESOURCE_INT_LIST_END
};

static int set_fullscreen_enabled(int val, void *param)
{
    int r = 0;
    video_canvas_t *canvas = (video_canvas_t *)param;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    canvas->videoconfig->fullscreen_enabled = val;

#ifndef USE_SDLUI
    if (canvas->initialized)
#endif
    {
        if (val) {
            r = (video_chip_cap->fullscreen.enable)(canvas, val);
            (void) (video_chip_cap->fullscreen.statusbar)
            (canvas, canvas->videoconfig->fullscreen_statusbar_enabled); 
        } else {
            /* always show statusbar when coming back to window mode */
            (void) (video_chip_cap->fullscreen.statusbar) (canvas, 1); 
            r = (video_chip_cap->fullscreen.enable)(canvas, val);
        }
    }
    return r;
}

static int set_fullscreen_statusbar(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    canvas->videoconfig->fullscreen_statusbar_enabled = val;

    return (video_chip_cap->fullscreen.statusbar)(canvas, val);
}

static int set_fullscreen_double_size_enabled(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    canvas->videoconfig->fullscreen_double_size_enabled = val;

    return (video_chip_cap->fullscreen.double_size)(canvas, val);
}

static int set_fullscreen_double_scan_enabled(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    canvas->videoconfig->fullscreen_double_scan_enabled = val;

    return (video_chip_cap->fullscreen.double_scan)(canvas, val);
}

static int set_fullscreen_device(const char *val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    if (canvas->videoconfig->fullscreen_enabled) {
        log_message(LOG_DEFAULT,
            _("Fullscreen (%s) already active - disable first."),
            canvas->videoconfig->fullscreen_device);
        return 0;
    }

    if (util_string_set(&canvas->videoconfig->fullscreen_device, val)) {
        return 0;
    }

    return (video_chip_cap->fullscreen.device)(canvas, val);
}

static const char *vname_chip_fullscreen[] = {
    "Fullscreen", "FullscreenStatusbar", "FullscreenDevice", NULL
};

static resource_string_t resources_chip_fullscreen_string[] =
{
    { NULL, NULL, RES_EVENT_NO, NULL,
      NULL, set_fullscreen_device, NULL },
    RESOURCE_STRING_LIST_END
};

static resource_int_t resources_chip_fullscreen_int[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_fullscreen_enabled, NULL },
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_fullscreen_statusbar, NULL },
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_fullscreen_double_size_enabled, NULL },
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_fullscreen_double_scan_enabled, NULL },
    RESOURCE_INT_LIST_END
};

static int set_fullscreen_mode(int val, void *param)
{
    video_resource_chip_mode_t *video_resource_chip_mode = (video_resource_chip_mode_t *)param;
    video_canvas_t *canvas = video_resource_chip_mode->resource_chip;
    video_chip_cap_t *video_chip_cap = canvas->videoconfig->cap;

    unsigned device = video_resource_chip_mode->device;


    canvas->videoconfig->fullscreen_mode[device] = val;

    return (video_chip_cap->fullscreen.mode[device])(canvas, val);
}

static const char *vname_chip_fullscreen_mode[] = { "FullscreenMode", NULL };

static resource_int_t resources_chip_fullscreen_mode[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_fullscreen_mode, NULL },
    RESOURCE_INT_LIST_END
};

static int set_ext_palette(int val, void *param)
{
    video_canvas_t *canvas;

    canvas = (video_canvas_t *)param;

    canvas->videoconfig->external_palette = (unsigned int)val;

    return video_color_update_palette(canvas);
}

static int set_palette_file_name(const char *val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;

    util_string_set(&(canvas->videoconfig->external_palette_name), val);

    return video_color_update_palette(canvas);
}

static const char *vname_chip_palette[] = { "PaletteFile", "ExternalPalette",
                                            NULL };

static resource_string_t resources_chip_palette_string[] =
{
    { NULL, NULL, RES_EVENT_NO, NULL,
      NULL, set_palette_file_name, NULL },
    RESOURCE_STRING_LIST_END
};

static resource_int_t resources_chip_palette_int[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_ext_palette, NULL },
    RESOURCE_INT_LIST_END
};

static int set_double_buffer_enabled(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;

    canvas->videoconfig->double_buffer = val;

    return 0;
}

static const char *vname_chip_double_buffer[] = { "DoubleBuffer", NULL };

static resource_int_t resources_chip_double_buffer[] =
{
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_double_buffer_enabled, NULL },
    RESOURCE_INT_LIST_END
};

/*
      resources for the color/palette generator
*/

static int set_color_saturation(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 2000) {
        val = 2000;
    }
    canvas->videoconfig->video_resources.color_saturation = val;
    return video_color_update_palette(canvas);
}

static int set_color_contrast(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 2000) {
        val = 2000;
    }
    canvas->videoconfig->video_resources.color_contrast = val;
    return video_color_update_palette(canvas);
}

static int set_color_brightness(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 2000) {
        val = 2000;
    }
    canvas->videoconfig->video_resources.color_brightness = val;
    return video_color_update_palette(canvas);
}

static int set_color_gamma(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 4000) {
        val = 4000;
    }
    canvas->videoconfig->video_resources.color_gamma = val;
    return video_color_update_palette(canvas);
}

static int set_color_tint(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 2000) {
        val = 2000;
    }
    canvas->videoconfig->video_resources.color_tint = val;
    return video_color_update_palette(canvas);
}

static const char *vname_chip_colors[] = { "ColorSaturation", "ColorContrast", "ColorBrightness", "ColorGamma", "ColorTint", NULL };

static resource_int_t resources_chip_colors[] =
{
    { NULL, 1000, RES_EVENT_NO, NULL,
      NULL, set_color_saturation, NULL },
    { NULL, 1000, RES_EVENT_NO, NULL,
      NULL, set_color_contrast, NULL },
    { NULL, 1000, RES_EVENT_NO, NULL,
      NULL, set_color_brightness, NULL },
    { NULL, 2200, RES_EVENT_NO, NULL,
      NULL, set_color_gamma, NULL },
    { NULL, 1000, RES_EVENT_NO, NULL,
      NULL, set_color_tint, NULL },
    RESOURCE_INT_LIST_END
};

static int set_pal_scanlineshade(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 1000) {
        val = 1000;
    }
    canvas->videoconfig->video_resources.pal_scanlineshade = val;
    return video_color_update_palette(canvas);
}

static int set_pal_oddlinesphase(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 2000) {
        val = 2000;
    }
    canvas->videoconfig->video_resources.pal_oddlines_phase = val;
    return video_color_update_palette(canvas);
}

static int set_pal_oddlinesoffset(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 2000) {
        val = 2000;
    }
    canvas->videoconfig->video_resources.pal_oddlines_offset = val;
    return video_color_update_palette(canvas);
}

static int set_pal_blur(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    if (val < 0) {
        val = 0;
    }
    if (val > 1000) {
        val = 1000;
    }
    canvas->videoconfig->video_resources.pal_blur = val;
    return video_color_update_palette(canvas);
}

static int set_audioleak(int val, void *param)
{
    video_canvas_t *canvas = (video_canvas_t *)param;
    canvas->videoconfig->video_resources.audioleak = val ? 1 : 0;
    return 0;
}

static const char *vname_chip_crtemu[] = { "PALScanLineShade", "PALBlur", "PALOddLinePhase", "PALOddLineOffset", "AudioLeak", NULL };

static resource_int_t resources_chip_crtemu[] =
{
    { NULL, 800, RES_EVENT_NO, NULL,
      NULL, set_pal_scanlineshade, NULL },
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_pal_blur, NULL },
    { NULL, 1250, RES_EVENT_NO, NULL,
      NULL, set_pal_oddlinesphase, NULL },
    { NULL, 1000, RES_EVENT_NO, NULL,
      NULL, set_pal_oddlinesoffset, NULL },
    { NULL, 0, RES_EVENT_NO, NULL,
      NULL, set_audioleak, NULL },
    RESOURCE_INT_LIST_END
};

/*-----------------------------------------------------------------------*/

int video_resources_chip_init(const char *chipname,
                              struct video_canvas_s **canvas,
                              video_chip_cap_t *video_chip_cap)
{
    unsigned int i;

    video_render_initconfig((*canvas)->videoconfig);
    (*canvas)->videoconfig->cap = video_chip_cap;

    (*canvas)->videoconfig->chip_name = strdup(chipname);

    /* Set single size render as default.  */
    (*canvas)->videoconfig->rendermode = video_chip_cap->single_mode.rmode;
    (*canvas)->videoconfig->doublesizex
        = video_chip_cap->single_mode.sizex > 1 ? 1 : 0;
    (*canvas)->videoconfig->doublesizey
        = video_chip_cap->single_mode.sizey > 1 ? 1 : 0;

    /* CHIPDoubleScan */
    if (video_chip_cap->dscan_allowed != 0) {
        resources_chip_scan[0].name
            = util_concat(chipname, vname_chip_scan[0], NULL);
        resources_chip_scan[0].value_ptr
            = &((*canvas)->videoconfig->doublescan);
        resources_chip_scan[0].param = (void *)*canvas;
        if (resources_register_int(resources_chip_scan) < 0) {
            return -1;
        }

        lib_free((char *)(resources_chip_scan[0].name));
    }

    if (video_chip_cap->hwscale_allowed != 0) {
        resources_chip_hwscale[0].name
            = util_concat(chipname, vname_chip_hwscale[0], NULL);
        resources_chip_hwscale[0].value_ptr
            = &((*canvas)->videoconfig->hwscale);
        resources_chip_hwscale[0].param = (void *)*canvas;
        if (resources_register_int(resources_chip_hwscale) < 0) {
            return -1;
        }

        lib_free((char *)(resources_chip_hwscale[0].name));
    }

    if (resources_register_int(resources_chip_hwscale_possible) < 0) {
        return -1;
    }

    /* CHIPDoubleSize */
    if (video_chip_cap->dsize_allowed != 0) {
        resources_chip_size[0].name
            = util_concat(chipname, vname_chip_size[0], NULL);
        resources_chip_size[0].factory_value
            = video_chip_cap->dsize_default;
        resources_chip_size[0].value_ptr
            = &((*canvas)->videoconfig->double_size_enabled);
        resources_chip_size[0].param = (void *)*canvas;
        if (resources_register_int(resources_chip_size) < 0) {
            return -1;
        }

        lib_free((char *)(resources_chip_size[0].name));
    }

    if (video_chip_cap->fullscreen.device_num > 0) {
        video_resource_chip_mode_t *resource_chip_mode;

        resources_chip_fullscreen_int[0].name
            = util_concat(chipname, vname_chip_fullscreen[0], NULL);
        resources_chip_fullscreen_int[0].value_ptr
            = &((*canvas)->videoconfig->fullscreen_enabled);
        resources_chip_fullscreen_int[0].param = (void *)*canvas;

        resources_chip_fullscreen_int[1].name
            = util_concat(chipname, vname_chip_fullscreen[1], NULL);
        resources_chip_fullscreen_int[1].value_ptr
            = &((*canvas)->videoconfig->fullscreen_statusbar_enabled);
        resources_chip_fullscreen_int[1].param = (void *)*canvas;

        resources_chip_fullscreen_string[0].name
            = util_concat(chipname, vname_chip_fullscreen[2], NULL);
        resources_chip_fullscreen_string[0].factory_value
            = video_chip_cap->fullscreen.device_name[0];
        resources_chip_fullscreen_string[0].value_ptr
            = &((*canvas)->videoconfig->fullscreen_device);
        resources_chip_fullscreen_string[0].param = (void *)*canvas;

        if (resources_register_string(resources_chip_fullscreen_string) < 0) {
            return -1;
        }

        if (resources_register_int(resources_chip_fullscreen_int) < 0) {
            return -1;
        }

        lib_free((char *)(resources_chip_fullscreen_int[0].name));
        lib_free((char *)(resources_chip_fullscreen_int[1].name));
        lib_free((char *)(resources_chip_fullscreen_string[0].name));

        for (i = 0; i < video_chip_cap->fullscreen.device_num; i++) {
            resource_chip_mode = lib_malloc(sizeof(video_resource_chip_mode_t));
            resource_chip_mode->resource_chip = *canvas;
            resource_chip_mode->device = i;

            resources_chip_fullscreen_mode[0].name
                = util_concat(chipname,
                    video_chip_cap->fullscreen.device_name[i],
                    vname_chip_fullscreen_mode[0], NULL);
            resources_chip_fullscreen_mode[0].value_ptr
                = &((*canvas)->videoconfig->fullscreen_mode[i]);
            resources_chip_fullscreen_mode[0].param
                = (void *)resource_chip_mode;

            if (resources_register_int(resources_chip_fullscreen_mode) < 0) {
                return -1;
            }

            lib_free((char *)(resources_chip_fullscreen_mode[0].name));
        }
    }

    /* Palette related */
    resources_chip_palette_string[0].name
        = util_concat(chipname, vname_chip_palette[0], NULL);
    resources_chip_palette_string[0].factory_value
        = video_chip_cap->external_palette_name;
    resources_chip_palette_string[0].value_ptr
        = &((*canvas)->videoconfig->external_palette_name);
    resources_chip_palette_string[0].param = (void *)*canvas;

    if (video_chip_cap->internal_palette_allowed != 0) {
        resources_chip_palette_int[0].name
            = util_concat(chipname, vname_chip_palette[1], NULL);
        resources_chip_palette_int[0].value_ptr
            = &((*canvas)->videoconfig->external_palette);
        resources_chip_palette_int[0].param = (void *)*canvas;
    } else {
        resources_chip_palette_int[0].name = NULL;
        (*canvas)->videoconfig->external_palette = 1;
    }

    if (resources_register_string(resources_chip_palette_string) < 0) {
        return -1;
    }

    if (resources_register_int(resources_chip_palette_int) < 0) {
        return -1;
    }

    lib_free((char *)(resources_chip_palette_string[0].name));
    if (video_chip_cap->internal_palette_allowed != 0) {
        lib_free((char *)(resources_chip_palette_int[0].name));
    }

    /* double buffering */
    if (video_chip_cap->double_buffering_allowed != 0) {
        resources_chip_double_buffer[0].name
            = util_concat(chipname, vname_chip_double_buffer[0], NULL);
        resources_chip_double_buffer[0].value_ptr
            = &((*canvas)->videoconfig->double_buffer);
        resources_chip_double_buffer[0].param = (void *)*canvas;
        if (resources_register_int(resources_chip_double_buffer) < 0) {
            return -1;
        }

        lib_free((char *)(resources_chip_double_buffer[0].name));
    }

    /* palette generator */
    i = 0; while (vname_chip_colors[i]) {
        resources_chip_colors[i].name = util_concat(chipname, vname_chip_colors[i], NULL);
        resources_chip_colors[i].param = (void *)*canvas;
        ++i;
    }
    resources_chip_colors[0].value_ptr = &((*canvas)->videoconfig->video_resources.color_saturation);
    resources_chip_colors[1].value_ptr = &((*canvas)->videoconfig->video_resources.color_contrast);
    resources_chip_colors[2].value_ptr = &((*canvas)->videoconfig->video_resources.color_brightness);
    resources_chip_colors[3].value_ptr = &((*canvas)->videoconfig->video_resources.color_gamma);
    resources_chip_colors[4].value_ptr = &((*canvas)->videoconfig->video_resources.color_tint);

    if (resources_register_int(resources_chip_colors) < 0) {
        return -1;
    }

    i = 0; while (vname_chip_colors[i]) {
        lib_free((char *)(resources_chip_colors[i].name));
        ++i;
    }

    /* crt emulation */
    i = 0; while (vname_chip_crtemu[i]) {
        resources_chip_crtemu[i].name = util_concat(chipname, vname_chip_crtemu[i], NULL);
        resources_chip_crtemu[i].param = (void *)*canvas;
        ++i;
    }
    resources_chip_crtemu[0].value_ptr = &((*canvas)->videoconfig->video_resources.pal_scanlineshade);
    resources_chip_crtemu[1].value_ptr = &((*canvas)->videoconfig->video_resources.pal_blur);
    resources_chip_crtemu[2].value_ptr = &((*canvas)->videoconfig->video_resources.pal_oddlines_phase);
    resources_chip_crtemu[3].value_ptr = &((*canvas)->videoconfig->video_resources.pal_oddlines_offset);
    resources_chip_crtemu[4].value_ptr = &((*canvas)->videoconfig->video_resources.audioleak);

    if (resources_register_int(resources_chip_crtemu) < 0) {
        return -1;
    }

    i = 0; while (vname_chip_crtemu[i]) {
        lib_free((char *)(resources_chip_crtemu[i].name));
        ++i;
    }

    /* CHIPFilter */
    resources_chip_rendermode[0].name
        = util_concat(chipname, vname_chip_rendermode[0], NULL);
    resources_chip_rendermode[0].value_ptr
        = &((*canvas)->videoconfig->filter);
    resources_chip_rendermode[0].param = (void *)*canvas;
    if (resources_register_int(resources_chip_rendermode) < 0) {
        return -1;
    }

    lib_free((char *)(resources_chip_rendermode[0].name));

    return 0;
}

void video_resources_chip_shutdown(struct video_canvas_s *canvas)
{
    lib_free(canvas->videoconfig->external_palette_name);
    lib_free(canvas->videoconfig->chip_name);

    if (canvas->videoconfig->cap->fullscreen.device_num > 0) {
        lib_free(canvas->videoconfig->fullscreen_device);
    }
}

/* FIXME: this one seems weird */
static int video_resources_update_ui(video_canvas_t *canvas)
{
    return video_color_update_palette(canvas);
}
