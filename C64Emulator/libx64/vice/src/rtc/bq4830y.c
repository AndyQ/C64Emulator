/*
 * bq4830y.c - BQ4830Y RTC emulation.
 *
 * Written by
 *  Marco van den Heuvel <blackystardust68@yahoo.com>
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

#include "bq4830y.h"
#include "lib.h"
#include "rtc.h"

#include <string.h>

/* The BQ4830Y is a ram + RTC module, it can be used in place of a ram
 * and the RTC registers can be accessed at $7FF8-$7FFF and it has the
 * following features:
 * - Real-Time Clock Counts Seconds, Minutes, Hours, Date of the Month,
 *   Month, Day of the Week, and Year with Leap-Year,
 *   Compensation Valid Up to 2100
 * - 32760 x 8 Battery-Backed General-Purpose RAM
 * - Clock Halt flag
 * - Calibration register
 * - Frequency Test bit
 * - All clock registers are in BCD format
 */

/* The BQ4830Y has the following clock registers:
 *
 * register $7FF8 : bit  7   Write to clock  (halts updates to clock registers)
 *                  bit  6   Read from clock (halts updates to clock registers)
 *                  bit  5   Direction of correction for calibration (emulated as a RAM bit)
 *                  bits 4-0 Calibration (emulated as RAM bits)
 *
 * register $7FF9 : bit  7   Clock Halt
 *                  bits 6-4 10 seconds
 *                  bits 3-0 seconds
 *
 * register $7FFA : bit  7   RAM bit
 *                  bits 6-4 10 minutes
 *                  bits 3-0 minutes
 *
 * register $7FFB : bits 7-6 RAM bits
 *                  bits 5-4 10 hours
 *                  bits 3-0 hours
 *
 * register $7FFC : bit  7   RAM bit
 *                  bit  6   Test Frequency bit (emulated as RAM bit)
 *                  bits 5-3 RAM bits
 *                  bits 2-0 days (of week)
 *
 * register $7FFD : bits 7-6 RAM bits
 *                  bits 5-4 10 days (of month)
 *                  bits 3-0 days (of month)
 *
 * register $7FFE : bits 7-5 RAM bits
 *                  bit  4   10 months
 *                  bits 3-0 months
 *
 * register $7FFF : bits 7-4 10 years
 *                  bits 3-0 years
 */

/* ---------------------------------------------------------------------------------------------------- */

rtc_bq4830y_t *bq4830y_init(BYTE *ram, time_t *offset)
{
    rtc_bq4830y_t *retval = lib_malloc(sizeof(rtc_bq4830y_t));
    memset(retval, 0, sizeof(rtc_bq4830y_t));
    retval->ram = ram;
    retval->offset = offset;

    return retval;
}

void bq4830y_destroy(rtc_bq4830y_t *context)
{
    lib_free(context);
}

/* ---------------------------------------------------------------------------------------------------- */

static void bq4830y_latch_write_regs(rtc_bq4830y_t *context)
{
    int i;

    context->clock_regs[BQ4830Y_REG_SECONDS & 7] &= 0x80;
    context->clock_regs[BQ4830Y_REG_SECONDS & 7] |= rtc_get_second(context->latch, 1);
    context->clock_regs[BQ4830Y_REG_MINUTES & 7] &= 0x80;
    context->clock_regs[BQ4830Y_REG_MINUTES & 7] |= rtc_get_minute(context->latch, 1);
    context->clock_regs[BQ4830Y_REG_HOURS & 7] &= 0xc0;
    context->clock_regs[BQ4830Y_REG_HOURS & 7] |= rtc_get_hour(context->latch, 1);
    context->clock_regs[BQ4830Y_REG_DAYS_OF_WEEK & 7] &= 0xf8;
    context->clock_regs[BQ4830Y_REG_DAYS_OF_WEEK & 7] |= rtc_get_weekday(context->latch) + 1;
    context->clock_regs[BQ4830Y_REG_DAYS_OF_MONTH & 7] &= 0xc0;
    context->clock_regs[BQ4830Y_REG_DAYS_OF_MONTH & 7] |= rtc_get_day_of_month(context->latch, 1);
    context->clock_regs[BQ4830Y_REG_MONTHS & 7] &= 0xe0;
    context->clock_regs[BQ4830Y_REG_MONTHS & 7] |= rtc_get_month(context->latch, 1);
    context->clock_regs[BQ4830Y_REG_YEARS & 7] = rtc_get_year(context->latch, 1);
    for (i = 0; i < 8; i++) {
        context->clock_regs_changed[i] = 0;
    }
}

static void bq4830y_write_clock_data(rtc_bq4830y_t *context)
{
    int val;

    if (context->clock_halt) {
        if (context->clock_regs_changed[BQ4830Y_REG_YEARS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_YEARS & 7];
            context->clock_halt_latch = rtc_set_latched_year(val, context->clock_halt_latch, 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_MONTHS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_MONTHS & 7] & 0x1f;
            context->clock_halt_latch = rtc_set_latched_month(val, context->clock_halt_latch, 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_DAYS_OF_MONTH & 7]) {
            val = context->clock_regs[BQ4830Y_REG_DAYS_OF_MONTH & 7] & 0x3f;
            context->clock_halt_latch = rtc_set_latched_day_of_month(val, context->clock_halt_latch, 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_DAYS_OF_WEEK & 7]) {
            val = (context->clock_regs[BQ4830Y_REG_DAYS_OF_WEEK & 7] & 7) - 1;
            context->clock_halt_latch = rtc_set_latched_weekday(val, context->clock_halt_latch);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_HOURS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_HOURS & 7] & 0x3f;
            context->clock_halt_latch = rtc_set_latched_hour(val, context->clock_halt_latch, 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_MINUTES & 7]) {
            val = context->clock_regs[BQ4830Y_REG_MINUTES & 7] & 0x7f;
            context->clock_halt_latch = rtc_set_latched_minute(val, context->clock_halt_latch, 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_SECONDS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_SECONDS & 7] & 0x7f;
            context->clock_halt_latch = rtc_set_latched_second(val, context->clock_halt_latch, 1);
        }
    } else {
        if (context->clock_regs_changed[BQ4830Y_REG_YEARS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_YEARS & 7];
            context->offset[0] = rtc_set_year(val, context->offset[0], 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_MONTHS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_MONTHS & 7] & 0x1f;
            context->offset[0] = rtc_set_month(val, context->offset[0], 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_DAYS_OF_MONTH & 7]) {
            val = context->clock_regs[BQ4830Y_REG_DAYS_OF_MONTH & 7] & 0x3f;
            context->offset[0] = rtc_set_day_of_month(val, context->offset[0], 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_DAYS_OF_WEEK & 7]) {
            val = (context->clock_regs[BQ4830Y_REG_DAYS_OF_WEEK & 7] & 7) - 1;
            context->offset[0] = rtc_set_weekday(val, context->offset[0]);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_HOURS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_HOURS & 7] & 0x3f;
            context->offset[0] = rtc_set_hour(val, context->offset[0], 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_MINUTES & 7]) {
            val = context->clock_regs[BQ4830Y_REG_MINUTES & 7] & 0x7f;
            context->offset[0] = rtc_set_minute(val, context->offset[0], 1);
        }
        if (context->clock_regs_changed[BQ4830Y_REG_SECONDS & 7]) {
            val = context->clock_regs[BQ4830Y_REG_SECONDS & 7] & 0x7f;
            context->offset[0] = rtc_set_second(val, context->offset[0], 1);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------- */

void bq4830y_store(rtc_bq4830y_t *context, WORD address, BYTE val)
{
    int latch_state = context->read_latch | (context->write_latch << 1);

    switch (address & 0x7fff) {
        case BQ4830Y_REG_MINUTES:
            if (context->write_latch) {
                context->clock_regs[address & 7] = val;
                context->clock_regs_changed[address & 7] = 1;
            } else {
                context->clock_regs[address & 7] &= 0x7f;
                context->clock_regs[address & 7] |= val & 0x80;
            }
            break;
        case BQ4830Y_REG_HOURS:
        case BQ4830Y_REG_DAYS_OF_MONTH:
            if (context->write_latch) {
                context->clock_regs[address & 7] = val;
                context->clock_regs_changed[address & 7] = 1;
            } else {
                context->clock_regs[address & 7] &= 0x3f;
                context->clock_regs[address & 7] |= val & 0xc0;
            }
            break;
        case BQ4830Y_REG_DAYS_OF_WEEK:
            if (context->write_latch) {
                context->clock_regs[address & 7] = val;
                context->clock_regs_changed[address & 7] = 1;
            } else {
                context->clock_regs[address & 7] &= 7;
                context->clock_regs[address & 7] |= val & 0xf8;
            }
            break;
        case BQ4830Y_REG_MONTHS:
            if (context->write_latch) {
                context->clock_regs[address & 7] = val;
                context->clock_regs_changed[address & 7] = 1;
            } else {
                context->clock_regs[address & 7] &= 0x1f;
                context->clock_regs[address & 7] |= val & 0xe0;
            }
            break;
        case BQ4830Y_REG_YEARS:
            if (context->write_latch) {
                context->clock_regs[address & 7] = val;
                context->clock_regs_changed[address & 7] = 1;
            }
            break;
        case BQ4830Y_REG_SECONDS:
            context->clock_regs[address & 7] &= 0x7f;
            context->clock_regs[address & 7] |= val & 0x80;
            if (context->write_latch) {
                context->clock_regs[address & 7] = val;
                context->clock_regs_changed[address & 7] = 1;
            } else {
                context->clock_regs[address & 7] &= 0x7f;
                context->clock_regs[address & 7] |= val & 0x80;
            }
            if ((val >> 7) != context->clock_halt) {
                if (val & 0x80) {
                    context->clock_halt_latch = rtc_get_latch(context->offset[0]);
                    context->clock_halt = 1;
                } else {
                    context->offset[0] = context->offset[0] - (rtc_get_latch(0) - (context->clock_halt_latch - context->offset[0]));
                    context->clock_halt = 0;
                }
            }
            break;
        case BQ4830Y_REG_CONTROL:
            context->clock_regs[address & 7] &= 0xc0;
            context->clock_regs[address & 7] |= val & 0x3f;
            switch (val >> 6) {
                case LATCH_NONE:
                    switch (latch_state) {
                        case READ_LATCH:
                            context->read_latch = 0;
                            break;
                        case WRITE_LATCH:
                            bq4830y_write_clock_data(context);
                            context->write_latch = 0;
                            break;
                        case READ_WRITE_LATCH:
                            bq4830y_write_clock_data(context);
                            context->read_latch = 0;
                            context->write_latch = 0;
                            break;
                    }
                    break;
                case READ_LATCH:
                    switch (latch_state) {
                        case WRITE_LATCH:
                            bq4830y_write_clock_data(context);
                            context->write_latch = 0;
                            /* fall through */
                        case LATCH_NONE:
                            if (context->clock_halt) {
                                context->latch = context->clock_halt_latch;
                            } else {
                                context->latch = rtc_get_latch(context->offset[0]);
                            }
                            context->read_latch = 1;
                            break;
                        case READ_WRITE_LATCH:
                            bq4830y_write_clock_data(context);
                            context->write_latch = 0;
                            break;
                    }
                    break;
                case WRITE_LATCH:
                    switch (latch_state) {
                        case READ_LATCH:
                            context->read_latch = 0;
                            /* fall through */
                        case LATCH_NONE:
                            if (context->clock_halt) {
                                context->latch = context->clock_halt_latch;
                            } else {
                                context->latch = rtc_get_latch(context->offset[0]);
                            }
                            bq4830y_latch_write_regs(context);
                            context->write_latch = 1;
                            break;
                        case READ_WRITE_LATCH:
                            context->read_latch = 0;
                            break;
                    }
                    break;
                case READ_WRITE_LATCH:
                    switch (latch_state) {
                        case LATCH_NONE:
                            if (context->clock_halt) {
                                context->latch = context->clock_halt_latch;
                            } else {
                                context->latch = rtc_get_latch(context->offset[0]);
                            }
                            context->read_latch = 1;
                            bq4830y_latch_write_regs(context);
                            context->write_latch = 1;
                            break;
                        case READ_LATCH:
                            bq4830y_latch_write_regs(context);
                            context->write_latch = 1;
                            break;
                        case WRITE_LATCH:
                            context->read_latch = 1;
                            break;
                    }
                    break;
            }
            break;
        default:
            context->ram[address] = val;
            break;
    }
}

BYTE bq4830y_read(rtc_bq4830y_t *context, WORD address)
{
    BYTE retval;
    int latch_state = context->read_latch | (context->write_latch << 1) | (context->clock_halt << 2);
    time_t latch;

    if (latch_state != LATCH_NONE) {
        if (!context->clock_halt) {
            latch = context->latch;
        } else {
            latch = context->clock_halt_latch;
        }
    } else {
        latch = rtc_get_latch(context->offset[0]);
    }

    switch (address & 0x7fff) {
        case BQ4830Y_REG_MINUTES:
            retval = context->clock_regs[address & 7] & 0x80;
            retval |= rtc_get_minute(latch, 1);
            break;
        case BQ4830Y_REG_HOURS:
            retval = context->clock_regs[address & 7] & 0xc0;
            retval |= rtc_get_hour(latch, 1);
            break;
        case BQ4830Y_REG_DAYS_OF_WEEK:
            retval = context->clock_regs[address & 7] & 0xf8;
            retval |= rtc_get_weekday(latch) + 1;
            break;
        case BQ4830Y_REG_DAYS_OF_MONTH:
            retval = context->clock_regs[address & 7] & 0xc0;
            retval |= rtc_get_day_of_month(latch, 1);
            break;
        case BQ4830Y_REG_MONTHS:
            retval = context->clock_regs[address & 7] & 0xe0;
            retval |= rtc_get_month(latch, 1);
            break;
        case BQ4830Y_REG_YEARS:
            retval = rtc_get_year(latch, 1);
            break;
        case BQ4830Y_REG_CONTROL:
            retval = context->clock_regs[address & 7] & 0x3f;
            retval |= (context->write_latch << 7);
            retval |= (context->read_latch << 6);
            break;
        case BQ4830Y_REG_SECONDS:
            retval = context->clock_halt << 7;
            retval |= rtc_get_second(latch, 1);
            break;
        default:
            retval = context->ram[address];
    }
    return retval;
}
