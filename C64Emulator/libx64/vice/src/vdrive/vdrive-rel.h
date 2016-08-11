/*
 * vdrive-rel.h - Virtual disk-drive implementation.
 *                Relative files specific functions.
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

#ifndef VICE_VDRIVE_REL_H
#define VICE_VDRIVE_REL_H

#include "types.h"

struct vdrive_s;

extern void vdrive_rel_init(void);
extern int vdrive_rel_open(struct vdrive_s *vdrive, unsigned int secondary,
                           cbmdos_cmd_parse_t *cmd_parse, const BYTE *name);
extern int vdrive_rel_position(struct vdrive_s *vdrive, unsigned int channel,
                               unsigned int rec_lo, unsigned int rec_hi,
                               unsigned int position);
extern int vdrive_rel_read(struct vdrive_s *vdrive, BYTE *data,
                           unsigned int secondary);
extern int vdrive_rel_write(struct vdrive_s *vdrive, BYTE data,
                            unsigned int secondary);
extern int vdrive_rel_close(struct vdrive_s *vdrive, unsigned int secondary);
extern void vdrive_rel_listen(struct vdrive_s *vdrive,
                              unsigned int secondary);

#endif

