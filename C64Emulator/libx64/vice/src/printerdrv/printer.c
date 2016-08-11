/*
 * printer.c - Common printer interface.
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

#include "driver-select.h"
#include "drv-ascii.h"
#include "drv-mps803.h"
#ifndef DINGOO_NATIVE
#include "drv-nl10.h"
#endif
#include "drv-raw.h"
#include "interface-serial.h"
#include "interface-userport.h"
#include "machine-printer.h"
#include "output-graphics.h"
#include "output-select.h"
#include "output-text.h"
#include "printer.h"


int printer_resources_init(void)
{
    if (output_graphics_init_resources() < 0
        || output_text_init_resources() < 0
        || output_select_init_resources() < 0
        || drv_ascii_init_resources() < 0
        || drv_mps803_init_resources() < 0
#ifndef DINGOO_NATIVE
        || drv_nl10_init_resources() < 0
#endif
        || drv_raw_init_resources() < 0
        || driver_select_init_resources() < 0
        || machine_printer_resources_init() < 0) {
        return -1;
    }
    return 0;
}

void printer_resources_shutdown(void)
{
    output_text_shutdown_resources();
    output_select_shutdown_resources();
    driver_select_shutdown_resources();
}

int printer_cmdline_options_init(void)
{
    if (output_graphics_init_cmdline_options() < 0
        || output_text_init_cmdline_options() < 0
        || output_select_init_cmdline_options() < 0
        || driver_select_init_cmdline_options() < 0
        || machine_printer_cmdline_options_init() < 0) {
        return -1;
    }
    return 0;
}

void printer_init(void)
{
    output_graphics_init();
    output_text_init();
    output_select_init();
    drv_ascii_init();
    drv_mps803_init();
#ifndef DINGOO_NATIVE
    drv_nl10_init();
#endif
    drv_raw_init();
    driver_select_init();
    machine_printer_init();
}

void printer_reset(void)
{
    output_graphics_reset();
    output_text_reset();
#ifndef DINGOO_NATIVE
    drv_nl10_reset();
#endif
}

void printer_shutdown(void)
{
    output_select_shutdown();
    drv_mps803_shutdown();
#ifndef DINGOO_NATIVE
    drv_nl10_shutdown();
#endif
    driver_select_shutdown();
    machine_printer_shutdown();
}

void printer_formfeed(unsigned int prnr)
{
    driver_select_formfeed(prnr);
}
