/*
 * raster-sprite-cache.c - Cache for a sprite raster line.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
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

#ifndef VICE_RASTER_SPRITE_CACHE_H
#define VICE_RASTER_SPRITE_CACHE_H

#include "types.h"

/* This defines the cache for one sprite line. */
struct raster_sprite_cache_s {
    /* Sprite colors. */
    unsigned int c1, c2, c3;

    /* Data used on the current line. */
    DWORD data;

    /* X expansion flag. */
    int x_expanded;

    /* X coordinate.  Note: this can be negative, when the sprite "wraps"
       to the left! */
    int x;

    /* Activation flag. */
    int visible;

    /* Priority flag. */
    int in_background;

    /* Multicolor flag. */
    int multicolor;
};
typedef struct raster_sprite_cache_s raster_sprite_cache_t;


extern void raster_sprite_cache_init(raster_sprite_cache_t *sc);
extern raster_sprite_cache_t *raster_sprite_cache_new(void);

#endif

