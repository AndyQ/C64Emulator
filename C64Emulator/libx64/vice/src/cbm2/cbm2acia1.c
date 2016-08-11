/*
 * cbm2acia1.c - Definitions for a 6551 ACIA interface
 *
 * Written by
 *  Andre' Fachat <fachat@physik.tu-chemnitz.de>
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

#define mycpu           maincpu
#define myclk           maincpu_clk
#define mycpu_rmw_flag  maincpu_rmw_flag
#define mycpu_clk_guard maincpu_clk_guard

#define myacia acia1

/* resource defaults */
#define MYACIA   "Acia1"
#define MyDevice 0
#define MyIrq    IK_IRQ

#define myaciadev acia1dev

#define myacia_init acia1_init
#define myacia_init_cmdline_options acia1_cmdline_options_init
#define myacia_init_resources acia1_resources_init
#define myacia_snapshot_read_module acia1_snapshot_read_module
#define myacia_snapshot_write_module acia1_snapshot_write_module
#define myacia_peek acia1_peek
#define myacia_read acia1_read
#define myacia_reset acia1_reset
#define myacia_store acia1_store

/* no set mode */
#define myacia_set_mode(x) 0

#include "maincpu.h"

#define mycpu_alarm_context maincpu_alarm_context

#include "cbm2.h"
#include "tpi.h"

#define mycpu_set_irq(b,a) tpicore_set_int(machine_context.tpi1, 4,(a))
#define mycpu_set_nmi(b,a) tpicore_set_int(machine_context.tpi1, 4,(a))
#define mycpu_set_int_noclk(b,c) tpicore_restore_int(machine_context.tpi1, 4,(c))

#include "aciacore.c"

