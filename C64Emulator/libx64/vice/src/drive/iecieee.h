/*
 * iecieee.h
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

#ifndef VICE_IECIEEE_H
#define VICE_IECIEEE_H

#include "types.h"

struct drive_context_s;
struct snapshot_s;

extern void iecieee_drive_init(struct drive_context_s *drv);
extern void iecieee_drive_shutdown(struct drive_context_s *drv);
extern void iecieee_drive_reset(struct drive_context_s *drv);
extern void iecieee_drive_setup_context(struct drive_context_s *drv);

extern int iecieee_drive_snapshot_read(struct drive_context_s *ctxptr,
                                       struct snapshot_s *s);
extern int iecieee_drive_snapshot_write(struct drive_context_s *ctxptr,
                                        struct snapshot_s *s);
#endif

