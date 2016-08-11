/*
 * glue1571.c
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

#include "vice.h"

#include "drive.h"
#include "glue1571.h"
#include "rotation.h"


void glue1571_side_set(unsigned int side, struct drive_s *drive)
{
    unsigned int num;

    num = drive->current_half_track;

    rotation_rotate_disk(drive);

    drive_gcr_data_writeback(drive);

    drive->side = side;
    if (num > 70)
        num -= 70;
    num += side * 70;

    drive_set_half_track(num, drive);
}

