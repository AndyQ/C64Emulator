/*
 * c64fastiec.h - Fast IEC bus handling for the C64.
 *
 * Written by
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

#ifndef VICE_C64FASTIEC_H
#define VICE_C64FASTIEC_H

#include "types.h"

#define BURST_MOD_NONE 0
#define BURST_MOD_CIA1 1
#define BURST_MOD_CIA2 2

extern void c64fastiec_init(void);
extern void c64fastiec_fast_cpu_write(BYTE data);
extern int burst_mod;
extern int set_burst_mod(int mode, void *param);

#endif
