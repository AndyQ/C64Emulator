/*
 * video-sound.h - Video to Audio leak emulation
 *
 * Written by
 *  groepaz <groepaz@gmx.net>
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

/* #define DEBUG_VIDEOSOUND */

#ifdef DEBUG_VIDEOSOUND
#define DBG(x)  log_debug x
#else
#define DBG(x)
#endif

#include <string.h>

#include "log.h"
#include "machine.h"
#include "sound.h"
#include "vice.h"
#include "viewport.h"
#include "video-sound.h"
#include "video.h"

#define NOISE_VOLUME            (0.05f)
#define LUMALINES_VOLUME        (0.15f)

#define NOISE_RATE              (44100)
#define LUMALINES_RATE          (15000)

#define MAX_LUMALINES   512 /* maximum height of picture */

/* noise floor vaguely resembling random spikes at line frequency (~15khz) */
const static signed char noise_sample[] = {
    7,0,0,8,0,7,0,0,7,0,6,0,0,8,0,0
};

static sound_chip_t video_sound;
static WORD video_sound_offset;
static int cycles_per_sec = 1000000;
static int sample_rate = 22050;
static int numchips = 1;

typedef struct
{
    float lumas[MAX_LUMALINES];
    float avglum;
    const signed char *sampleptr;
    float *lumaptr;
    int firstline;
    int lastline;
    int enabled;
    int div1;
    int div2;
} videosound_t;
static videosound_t chip[2];

static int video_sound_machine_calculate_samples(sound_t **psid, SWORD *pbuf, int nr, int soc, int scc, int *delta_t)
{
    int i, num;
    int smpval1, smpval2;

    /* DBG(("video_sound_machine_calculate_samples")); */

    for (i = 0; i < nr; i++) {
        for (num = 0; num < numchips; num++) {
            smpval1 = (int)((float)(*chip[num].sampleptr) * chip[num].avglum * NOISE_VOLUME) / (1 << 19);
            smpval2 = (int)((*chip[num].lumaptr) * LUMALINES_VOLUME) / (1 << 16);
            switch (soc) {
                default:
                case 1:
                    pbuf[i] = sound_audio_mix(pbuf[i], smpval2);
                    pbuf[i] = sound_audio_mix(pbuf[i], smpval1);
                    break;
                case 2:
                    pbuf[i * 2] = sound_audio_mix(pbuf[i * 2], smpval2);
                    pbuf[i * 2] = sound_audio_mix(pbuf[i * 2], smpval1);
                    pbuf[i * 2 + 1] = sound_audio_mix(pbuf[i * 2 + 1], smpval2);
                    pbuf[i * 2 + 1] = sound_audio_mix(pbuf[i * 2 + 1], smpval1);
                    break;
            }

            chip[num].div1 += NOISE_RATE;
            while (chip[num].div1 >= sample_rate) {
                chip[num].div1 -= sample_rate;
                chip[num].sampleptr++;
                if (chip[num].sampleptr == &noise_sample[sizeof(noise_sample)]) {
                    chip[num].sampleptr = noise_sample;
                }
            }
            chip[num].div2 += LUMALINES_RATE;
            while (chip[num].div2 >= sample_rate) {
                chip[num].div2 -= sample_rate;
                chip[num].lumaptr++;
                if (chip[num].lumaptr == &chip[num].lumas[chip[num].lastline + 1]) {
                    chip[num].lumaptr = &chip[num].lumas[chip[num].firstline];
                }
            }
        }
    }
    return nr;
}

static int video_sound_machine_init(sound_t *psid, int speed, int cycles)
{
    cycles_per_sec = cycles;
    sample_rate = speed;
    return 1;
}

static void video_sound_reset(sound_t *psid, CLOCK cpu_clk)
{
}

static int video_sound_machine_cycle_based(void)
{
    return 0;
}

static int video_sound_machine_channels(void)
{
    return 1;
}

static void video_sound_machine_store(sound_t *psid, WORD addr, BYTE val)
{
}

static BYTE video_sound_machine_read(sound_t *psid, WORD addr)
{
    return 0;
}

static sound_chip_t video_sound = {
    NULL, /* no open */
    video_sound_machine_init,
    NULL, /* no close */
    video_sound_machine_calculate_samples,
    video_sound_machine_store,
    video_sound_machine_read,
    video_sound_reset,
    video_sound_machine_cycle_based,
    video_sound_machine_channels,
    0 /* chip enabled */
};

/*
    this is a sort of ugly hack, unfortunately the video_render_config_t does
    not tell us which chip it belongs to by other means.
 */
static inline int get_chip_num(video_render_config_t *config)
{
    if ((numchips == 2) &&
        (config->chip_name[0] == 'V') &&
        (config->chip_name[1] == 'D') &&
        (config->chip_name[2] == 'C')) {
        return 1;
    }
    return 0;
}

static inline int check_enabled(void)
{
    int i;
    for (i = 0; i < numchips; i++) {
        if (chip[i].enabled) {
            return 1;
        }
    }
    return 0;
}

void video_sound_update(video_render_config_t *config, const BYTE *src,
                      unsigned int width, unsigned int height,
                      unsigned int xs, unsigned int ys,
                      unsigned int pitchs, viewport_t *viewport)
{
    const SDWORD *c1 = config->color_tables.ytablel;
    const SDWORD *c2 = config->color_tables.ytableh;
    unsigned int x, y;
    const BYTE *tmpsrc;
    float lum;
    int chipnum = get_chip_num(config);

    chip[chipnum].enabled = config->video_resources.audioleak;
    if (!check_enabled()) {
        video_sound.chip_enabled = 0;
        return;
    }
    video_sound.chip_enabled = 1;

    chip[chipnum].firstline = viewport->first_line;
    chip[chipnum].lastline = viewport->last_line;
    DBG(("video_sound_update (firstline:%d lastline:%d w:%d h:%d xs:%d ys:%d)", 
         chip[chipnum].firstline, chip[chipnum].lastline, width, height, xs, ys));

    width /= (config->doublesizex + 1);
    /* height /= (config->doublesizey + 1); */

    /* width += xs; */
    ys = chip[chipnum].firstline;
    height = chip[chipnum].lastline - chip[chipnum].firstline;

    src += (pitchs * ys) + xs;

    for (y = 0; y < height; y++) {
        lum = 0;
        tmpsrc = src;
        for (x = 0; x < width; x++) {
            lum += (c1[*tmpsrc] << 2) + c2[*tmpsrc] + 0x00010000;
            tmpsrc++;
        }
        chip[chipnum].lumas[ys] = lum / (float)width;
        src += pitchs;
        ys++;
    }
    lum = 0;
    for (y = chip[chipnum].firstline; y < chip[chipnum].lastline; y++) {
        lum += chip[chipnum].lumas[y];
    }
    chip[chipnum].avglum = lum / (float)height;
}

void video_sound_init(void)
{
    int i;

    DBG(("video_sound_init"));

    video_sound_offset = sound_chip_register(&video_sound);

    if (machine_class == VICE_MACHINE_C128) {
        numchips = 2;
    } else {
        numchips = 1;
    }

    for (i = 0; i < numchips; i++) {
        chip[i].sampleptr = noise_sample;
        chip[i].lumaptr = &chip[i].lumas[0];
        memset (chip[i].lumas, 0, sizeof(float) * MAX_LUMALINES);
    }
}
