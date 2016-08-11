/*
 * vicii-fetch.h - Phi1/Phi2 data fetch for the VIC-II emulation.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *  Ettore Perazzoli <ettore@comm2000.it>
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

#ifndef VICE_VICII_FETCH_H
#define VICE_VICII_FETCH_H

#include "types.h"

extern void vicii_fetch_matrix(void);
extern BYTE vicii_fetch_graphics(void);
extern BYTE vicii_fetch_idle(void);
extern BYTE vicii_fetch_idle_gfx(void);
extern BYTE vicii_fetch_refresh(void);
extern BYTE vicii_fetch_sprite_pointer(int sprite);
extern BYTE vicii_fetch_sprite_dma_1(int sprite);
extern int vicii_check_sprite_ba(unsigned int cycle_flags);
extern void vicii_fetch_sprites(unsigned int cycle_flags);

#endif

