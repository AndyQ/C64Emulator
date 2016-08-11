/*
 * petsound.h - implementation of PET sound code
 *
 * Written by
 *  Teemu Rantanen <tvr@cs.hut.fi>
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

#ifndef VICE_PETSOUND_H
#define VICE_PETSOUND_H

#include "sound.h"
#include "types.h"

extern void petsound_store_onoff(int value);
extern void petsound_store_rate(CLOCK t);
extern void petsound_store_sample(BYTE value);
extern void petsound_store_manual(int value);

extern void petsound_reset(sound_t *psid, CLOCK cpu_clk);

extern void pet_sound_chip_init(void);

#endif

