/*
 * soundsdl.c - Implementation of the Simple Directmedia Layer sound device
 *
 * Written by
 *  Teemu Rantanen <tvr@cs.hut.fi>
 *  Daniel Aarno <macbishop@users.sourceforge.net>
 *  Hannu Nuotio <hannu.nuotio@tut.fi>
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

#include "vice_sdl.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "lib.h"
#include "sound.h"


static SWORD *sdl_buf = NULL;
static SDL_AudioSpec sdl_spec;
static volatile int sdl_inptr = 0;
static volatile int sdl_outptr = 0;
static volatile int sdl_full = 0;
static int sdl_len = 0;

static void sdl_callback(void *userdata, Uint8 *stream, int len)
{
    int	amount, total;
    total = 0;

    while (total < len/sizeof(SWORD)) {
        amount = sdl_inptr - sdl_outptr;
        if (amount <= 0) {
            amount = sdl_len - sdl_outptr;
        }

        if (amount + total > len/sizeof(SWORD)) {
            amount = len/sizeof(SWORD) - total;
        }

        sdl_full = 0;

        if (!amount) {
            memset(stream + total*sizeof(SWORD), 0, len - total*sizeof(SWORD));
            return;
        }

        memcpy(stream + total*sizeof(SWORD), sdl_buf + sdl_outptr,
               amount*sizeof(SWORD));
        total += amount;
        sdl_outptr += amount;

        if (sdl_outptr == sdl_len) {
	        sdl_outptr = 0;
        }
    }
}

static int sdl_init(const char *param, int *speed,
                    int *fragsize, int *fragnr, int *channels)
{
    SDL_AudioSpec spec;

    memset(&spec, 0, sizeof(spec));
    spec.freq = *speed;
    spec.format = AUDIO_S16;
    spec.channels = *channels;
    spec.samples = *fragsize;
    spec.callback = sdl_callback;

    if (SDL_OpenAudio(&spec, &sdl_spec)) {
        return 1;
    }

    if (sdl_spec.format != AUDIO_S16 || sdl_spec.channels != *channels) {
        SDL_CloseAudio();
        return 1;
    }

    sdl_len = (*fragsize)*(*fragnr);
    sdl_inptr = sdl_outptr = sdl_full = 0;
    sdl_buf = lib_malloc(sizeof(SWORD)*sdl_len);

    if (!sdl_buf) {
        SDL_CloseAudio();
        return 1;
    }

    *speed = sdl_spec.freq;
    SDL_PauseAudio(0);
    return 0;
}

#if defined(WORDS_BIGENDIAN) && (!defined(HAVE_SWAB) || defined(__BEOS__))
#if !defined(AMIGA_MORPHOS) && !defined(AMIGA_M68K)
void swab(void *src, void *dst, size_t length)
{
    const char *from=src;
    char *to=dst;
    size_t ptr;

    for (ptr=1; ptr<length; ptr+=2)  {
        char p=from[ptr];
        char q=from[ptr-1];
        to[ptr-1]=p;
        to[ptr]=q;
    }

    if (ptr==length) {
        to[ptr-1]=0;
    }
}
#else
#define swab(src, dst, length)              \
    do {                                    \
        const char *from=src;               \
        char *to=dst;                       \
        size_t ptr;                         \
                                            \
        for (ptr=1; ptr<(length); ptr+=2) { \
            char p=from[ptr];               \
            char q=from[ptr-1];             \
            to[ptr-1]=p;                    \
            to[ptr]=q;                      \
        }                                   \
        if (ptr==(length)) {                \
            to[ptr-1]=0;                    \
        }                                   \
    }                                       \
    while (0)
#endif
#endif

static int sdl_write(SWORD *pbuf, size_t nr)
{
    int total, amount;
    total = 0;

#ifdef WORDS_BIGENDIAN
     /* Swap bytes if we're on a big-endian machine, like the Macintosh */
     swab(pbuf, pbuf, sizeof(SWORD)*nr);
#endif

    while (total < nr) {
        amount = sdl_outptr - sdl_inptr;

        if (amount <= 0) {
            amount = sdl_len - sdl_inptr;
        }

        if (total + amount > nr) {
            amount = nr - total;
        }

        if (amount <= 0) {
            SDL_Delay(5);
            continue;
        }

        memcpy(sdl_buf + sdl_inptr, pbuf + total, amount*sizeof(SWORD));
        sdl_inptr += amount;
        total += amount;

        if (sdl_inptr == sdl_len) {
            sdl_inptr = 0;
        }
    }

    if (sdl_inptr == sdl_outptr) {
        sdl_full = 1;
    }

    return 0;
}

static int sdl_bufferspace(void)
{
    int amount;

    if (sdl_full) {
        amount = sdl_len;
    } else {
        amount = sdl_inptr - sdl_outptr;
    }

    if (amount < 0) {
        amount += sdl_len;
    }

    return sdl_len - amount;
}

static void sdl_close(void)
{
    SDL_CloseAudio();
    lib_free(sdl_buf);
    sdl_buf = NULL;
    sdl_inptr = sdl_outptr = sdl_len = sdl_full = 0;
}

static int sdl_suspend(void)
{
    SDL_PauseAudio(1);
    sdl_full = 0;
    return 0;
}

static int sdl_resume(void)
{
    SDL_PauseAudio(0);
    return 0;
}

static sound_device_t sdl_device =
{
    "sdl",
    sdl_init,
    sdl_write,
    NULL,
    NULL,
    sdl_bufferspace,
    sdl_close,
    sdl_suspend,
    sdl_resume,
    1
};

int sound_init_sdl_device(void)
{
    return sound_register_device(&sdl_device);
}
