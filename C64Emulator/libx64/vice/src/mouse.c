/*
 * mouse.c - Common mouse handling
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *
 * NEOS & Amiga mouse and paddle support by
 *  Hannu Nuotio <hannu.nuotio@tut.fi>
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

/* #define DEBUG_MOUSE */

#ifdef DEBUG_MOUSE
#define DBG(_x_)  log_debug _x_
#else
#define DBG(_x_)
#endif

#include <stdlib.h> /* abs */
#include "vice.h"

#include "alarm.h"
#include "cmdline.h"
#include "joystick.h"
#include "log.h"
#include "machine.h"
#include "maincpu.h"
#include "mouse.h"
#include "mousedrv.h"
#include "resources.h"
#include "translate.h"
#include "vsyncapi.h"
#include "clkguard.h"

/* Log descriptor.  */
#ifdef DEBUG_MOUSE
static log_t mouse_log = LOG_ERR;
#endif

/* --------------------------------------------------------- */
/* extern variables */

int _mouse_enabled = 0;
int mouse_port = 1;
int mouse_type = MOUSE_TYPE_1351;
int mouse_kind = MOUSE_KIND_OTHER;
/* --------------------------------------------------------- */
/* POT input selection */

/* POT input port. Defaults to 1 for xvic.

    For C64 this actually represents the upper two bits of CIA1 port A
    00 - no paddle selected
    01 - paddle port 1 selected
    10 - paddle port 2 selected
    11 - both paddle ports selected

    For Vic20 (and possibly others?) it must be 1
*/
static BYTE input_port = 1;

/* this is called from c64cia1.c/c128cia1.c */
void mouse_set_input(int port)
{
    input_port = port & 3;
    DBG(("mouse_set_input: %x", input_port));
}

/* --------------------------------------------------------- */
/* 1351 mouse */

/* FIXME: old code, remove this if the code below is proven correct */
#if 0
/* FIXME this is too simplistic as it doesn't take the sampling
   period into account. Using this code breaks (at least) the
   Final Cartridge III mouse code, hence disabling it for now.
*/
static BYTE mouse_get_1351_x(void)
{
    return (input_port == mouse_port) ? mousedrv_get_x() : 0xff;
}

static BYTE mouse_get_1351_y(void)
{
    return (input_port == mouse_port) ? mousedrv_get_y() : 0xff;
}
#endif
#if 0
static BYTE mouse_get_1351_x(void)
{
    return mousedrv_get_x();
}

static BYTE mouse_get_1351_y(void)
{
    return mousedrv_get_y();
}
#endif

/*
    note: for the expected behaviour look at testprogs/SID/paddles/readme.txt

    "The Final Cartridge 3" (and possibly others?) do not work if the mouse
    is inserted (as in enabled) after it has started. so either enable mouse
    emulation on the commandline, or (re)start (or reset incase of a cart)
    after enabling it in the gui. (see testprogs/SID/paddles/fc3detect.asm)

    FIXME: this is too simplistic as it doesn't take the sampling period into
           account.

    HACK: when both ports are selected, a proper combined value should be
          returned. however, since we are currently only emulating a single
          mouse or pair of paddles, always returning its respective value in
          this case is ok.
*/

static BYTE mouse_get_1351_x(void)
{
    switch (input_port) {
    case 3: /* both */
	return mousedrv_get_x(); /* HACK: see above */
    case 1: /* port1 */
    case 2: /* port2 */
	return (input_port == mouse_port) ? mousedrv_get_x() : 0xff;
    }
    return 0xff;
}

static BYTE mouse_get_1351_y(void)
{
    switch (input_port) {
    case 3: /* both */
	return mousedrv_get_y(); /* HACK: see above */
    case 1: /* port1 */
    case 2: /* port2 */
	return (input_port == mouse_port) ? mousedrv_get_y() : 0xff;
    }
    return 0xff;
}

/* --------------------------------------------------------- */
/* NEOS mouse */

#define NEOS_RESET_CLK 100
struct alarm_s *neosmouse_alarm;

static int neos_and_amiga_buttons;
static int neos_prev;

static BYTE neos_x;
static BYTE neos_y;
static BYTE neos_lastx;
static BYTE neos_lasty;

enum {
    NEOS_IDLE = 0,
    NEOS_XH,
    NEOS_XL,
    NEOS_YH,
    NEOS_YL,
    NEOS_DONE
} neos_state = NEOS_IDLE;

static void neos_get_new_movement(void)
{
    BYTE new_x, new_y;

    new_x = mousedrv_get_x();
    new_y = mousedrv_get_y();
    neos_x = new_x - neos_lastx;

    if (new_x < neos_lastx) {
        if (neos_lastx > 0x6f && new_x < 0x10) {
            neos_x += 0x80;
        }
    } else if (new_x > neos_lastx) {
        if (neos_lastx < 0x10 && new_x > 0x6f) {
            neos_x += 0x80;
        }
    }
    neos_lastx = new_x;

    neos_y = new_y - neos_lasty;
    if (new_y < neos_lasty) {
        if (neos_lasty > 0x6f && new_y < 0x10) {
            neos_y += 0x80;
        }
    } else if (new_y > neos_lasty) {
        if (neos_lasty < 0x10 && new_y > 0x6f) {
            neos_y += 0x80;
        }
    }
    neos_lasty = new_y;

    neos_x = (neos_x & 0x80) ? (neos_x / 2) | 0x80 : neos_x / 2;
    neos_y = (neos_y & 0x80) ? (neos_y / 2) | 0x80 : neos_y / 2;
    neos_x = -neos_x;
}

void neos_mouse_store(BYTE val)
{
    switch (neos_state) {
        case NEOS_IDLE:
            if (((val & 16) ^ (neos_prev & 16)) && ((val & 16) == 0)) {
                ++neos_state;
                neos_get_new_movement();
            }
            break;
        case NEOS_XH:
            if (((val & 16) ^ (neos_prev & 16)) && ((val & 16) != 0)) {
                ++neos_state;
            }
            break;
        case NEOS_XL:
            if (((val & 16) ^ (neos_prev & 16)) && ((val & 16) == 0)) {
                ++neos_state;
            }
            break;
        case NEOS_YH:
            if (((val & 16) ^ (neos_prev & 16)) && ((val & 16) != 0)) {
                ++neos_state;
                alarm_set(neosmouse_alarm, maincpu_clk + NEOS_RESET_CLK);
            }
            break;
        case NEOS_YL:
            ++neos_state;
            break;
        case NEOS_DONE:
        default:
            break;
    }
    neos_prev = val;
}

BYTE neos_mouse_read(void)
{
    switch (neos_state) {
    case NEOS_XH:
        return ((neos_x >> 4) & 0xf) | 0xf0;
        break;
    case NEOS_XL:
        return (neos_x & 0xf) | 0xf0;
        break;
    case NEOS_YH:
        return ((neos_y >> 4) & 0xf) | 0xf0;
        break;
    case NEOS_YL:
        return (neos_y & 0xf) | 0xf0;
        break;
    case NEOS_IDLE:
    case NEOS_DONE:
    default:
        return 0xff;
        break;
    }
}

static void neosmouse_alarm_handler(CLOCK offset, void *data)
{
    alarm_unset(neosmouse_alarm);
    neos_state = NEOS_IDLE;
}

/* --------------------------------------------------------- */
/* quadrature encoding mice support (currently experimental) */

/* range of mice coordinates a and b are [0,63] and we must consider
 * situations where we over- and under flow */
static int subtract_coords(BYTE a, BYTE b)
{
    /* range [-64,63] to [-32,31] */
    return ((a - b + 32) & 63) - 32;
}

/* The mousedev only updates its returned coordinates at certain *
 * frequency. We try to estimate this interval by timestamping unique
 * successive readings. The estimated interval is then converted from
 * vsynchapi units to emulated cpu cycles which in turn are used to
 * clock the quardrature emulation. */
static unsigned long latest_os_ts = 0; // in vsynchapi units
static CLOCK done_emu = 0; // in machine cycles
/* The mouse coordinates returned from the latest unique mousedrv
 * reading, range is [0,63] */
static BYTE latest_x = 0;
static BYTE latest_y = 0;

static CLOCK update_x_emu_iv = 0; // in cpu cycle units
static CLOCK update_y_emu_iv = 0; // in cpu cycle units
static CLOCK next_update_x_emu_ts = 0; // in cpu cycle units
static CLOCK next_update_y_emu_ts = 0; // in cpu cycle units
static int sx, sy;

/* the ratio between emulated cpu cycles and vsynchapi time units */
static float emu_units_per_os_units;

/* The current emulated quadrature state of the polled mouse, range is
 * [0,3] */
static BYTE quadrature_x = 0;
static BYTE quadrature_y = 0;

static BYTE polled_joyval = 0xff;

static const BYTE amiga_mouse_table[4] = { 0x0, 0x1, 0x5, 0x4 };
static const BYTE st_mouse_table[4] = { 0x0, 0x2, 0x3, 0x1 };

/* Clock overflow handling.  */
static void clk_overflow_callback(CLOCK sub, void *data)
{
    if (done_emu > (CLOCK) 0) {
        done_emu -= sub;
    }
    if (next_update_x_emu_ts > (CLOCK) 0) {
        next_update_x_emu_ts -= sub;
    }
    if (next_update_y_emu_ts > (CLOCK) 0) {
        next_update_y_emu_ts -= sub;
    }
}

BYTE mouse_poll(void)
{
    BYTE new_x, new_y;
    unsigned long os_now, os_iv;
    CLOCK emu_now, emu_iv;
    int diff_x, diff_y;

    /* get new mouse values, range [0,127] with lsb=0 */
    new_x = mousedrv_get_x() / 2;
    new_y = mousedrv_get_y() / 2;
    /* range of new_x and new_y are [0,63] */
    /* fetch now for both emu and os */
    os_now = mousedrv_get_timestamp();
    emu_now = maincpu_clk;

    /* update the quadrature wheels unless we're done */
    if (emu_now < done_emu) {
        /* update x-wheel until we're ahead */
        while (next_update_x_emu_ts <= emu_now) {
            quadrature_x += sx;
            next_update_x_emu_ts += update_x_emu_iv;
            polled_joyval = 0; // signal that the wheel has changed
        }

        /* update y-wheel until we're ahead */
        while (next_update_y_emu_ts <= emu_now) {
            quadrature_y += sy;
            next_update_y_emu_ts += update_y_emu_iv;
            polled_joyval = 0; // signal that the wheel has changed
        }

#ifdef DEBUG_MOUSE
        log_message(mouse_log, "cpu %u: quad %d,%d",
                    emu_now, quadrature_x & 0x3, quadrature_y & 0x3);
#endif
    }

    if (polled_joyval == 0) {
        /* keep within range */
        quadrature_x &= 0x3;
        quadrature_y &= 0x3;

        switch (mouse_type) {
        case MOUSE_TYPE_AMIGA:
            polled_joyval = ((amiga_mouse_table[quadrature_x] << 1) |
                             amiga_mouse_table[quadrature_y] | 0xf0);
            break;
        case MOUSE_TYPE_CX22:
            polled_joyval = (((quadrature_y & 2) << 2) | ((sy + 1) << 1) |
                             (quadrature_x & 2) | ((sx + 1) >> 1) | 0xf0);
            break;
        case MOUSE_TYPE_ST:
            polled_joyval = (st_mouse_table[quadrature_x] |
                             (st_mouse_table[quadrature_y] << 2) | 0xf0);
            break;
        default:
            polled_joyval = 0xff;
        }
    }

    /* check if the new values belong to a new mouse reading */
    if (latest_os_ts == 0) {
        /* only first time, init stuff */
        latest_x = new_x;
        latest_y = new_y;
        latest_os_ts = os_now;
    }
    else if (os_now != latest_os_ts &&
        (new_x != latest_x || new_y != latest_y)) {
        // yes, we have a new unique mouse coordinate reading

        /* calculate the interval between the latest two mousedrv
         * updates in emulated cycles */
        os_iv = os_now - latest_os_ts;
        if (os_iv > (unsigned long)vsyncarch_frequency()) {
            os_iv = (unsigned long)vsyncarch_frequency(); /* more than a second response time?! */
        }
        emu_iv = (CLOCK)((float)os_iv * emu_units_per_os_units);
#ifdef DEBUG_MOUSE
        log_message(mouse_log,
                    "New interval os_now %lu, os_iv %lu, emu_iv %lu",
                    os_now, os_iv, emu_iv);
#endif

        /* Let's set up quadrature emulation */
        diff_x = subtract_coords(new_x, latest_x);
        diff_y = subtract_coords(new_y, latest_y);

        if (diff_x != 0) {
            sx = diff_x >= 0 ? 1 : -1;
            /* lets calculate the interval between x-quad rotations */
            update_x_emu_iv = emu_iv / abs(diff_x);
            /* and the emulated cpu cycle count when to do the first one */
            next_update_x_emu_ts = emu_now;
        } else {
            next_update_x_emu_ts = CLOCK_MAX; /* never */
        }
        if (diff_y != 0) {
            sy = diff_y >= 0 ? -1 : 1;
            /* lets calculate the interval between y-quad rotations */
            update_y_emu_iv = emu_iv / abs(diff_y);
            /* and the emulated cpu cycle count when to do the first one */
            next_update_y_emu_ts = emu_now;
        } else {
            next_update_y_emu_ts = CLOCK_MAX; /* never */
        }

        /* calculate the timestamp when to stop emulating */
        done_emu = emu_now + emu_iv;

#ifdef DEBUG_MOUSE
        log_message(mouse_log, "cpu %u iv %u,%u old %d,%d new %d,%d",
                    emu_now, update_x_emu_iv, update_y_emu_iv,
                    latest_x, latest_y, new_x, new_y);
#endif

        /* store the new coordinates for next time */
        latest_x = new_x;
        latest_y = new_y;
        latest_os_ts = os_now;
    }

    return polled_joyval;
}

/* --------------------------------------------------------- */
/* Paddle support */

static BYTE paddle_val[] = {
/*  x     y  */
    0x00, 0xff, /* no port */
    0x00, 0xff, /* port 1 */
    0x00, 0xff, /* port 2 */
    0x00, 0xff  /* both ports */
};

static BYTE paddle_old[] = {
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff,
    0xff, 0xff
};

static inline BYTE mouse_paddle_update(BYTE paddle_v, BYTE *old_v, BYTE new_v)
{
    BYTE diff = new_v - *old_v;
    BYTE new_paddle;

    /* DBG(("new_v from driver: %d", new_v)); */

    if (new_v < *old_v) {
        if (*old_v > 0x6f && new_v < 0x10) {
            diff += 0x80;
        }
    } else if (new_v > *old_v) {
        if (*old_v < 0x10 && new_v > 0x6f) {
            diff += 0x80;
        }
    }

    new_paddle = paddle_v + diff;

    *old_v = new_v;

    if (((paddle_v & 0x80) ^ (new_paddle & 0x80)) && ((new_paddle & 0x80) == (diff & 0x80))) {
        new_paddle = (paddle_v & 0x80) ? 0xff : 0;
    }

    return new_paddle;
}

/* FIXME: old code, remove this if the code below is proven correct */
#if 0
static BYTE mouse_get_paddle_x(void)
{
    int i = (input_port << 1);

    if (input_port == mouse_port) {
        paddle_val[i] = mouse_paddle_update(paddle_val[i], &(paddle_old[i]), mousedrv_get_x());
    }

    return 0xff - paddle_val[i];
}

static BYTE mouse_get_paddle_y(void)
{
    int i = (input_port << 1) + 1;

    if (input_port == mouse_port) {
        paddle_val[i] = mouse_paddle_update(paddle_val[i], &(paddle_old[i]), mousedrv_get_y());
    }

    return 0xff - paddle_val[i];
}
#endif

/*
    note: for the expected behaviour look at testprogs/SID/paddles/readme.txt

    FIXME: this is too simplistic as it doesn't take the sampling period into
           account.

    HACK: when both ports are selected, a proper combined value should be
          returned. however, since we are currently only emulating a single
          mouse or pair of paddles, always returning its respective value in
          this case is ok.
*/

static BYTE mouse_get_paddle_x(void)
{
    int i = input_port & mouse_port;
    /* DBG(("mouse_get_paddle_x: %x", i)); */
    if (i != 0) {
        i = i << 1;
        /* one of the ports is selected */
        paddle_val[i] = mouse_paddle_update(paddle_val[i], &(paddle_old[i]), mousedrv_get_x());
        return 0xff - paddle_val[i];
    }
    return 0xff;
}

static BYTE mouse_get_paddle_y(void)
{
    int i = input_port & mouse_port;
    if (i != 0) {
        i = (i << 1) + 1;
        /* one of the ports is selected */
        paddle_val[i] = mouse_paddle_update(paddle_val[i], &(paddle_old[i]), mousedrv_get_y());
        return 0xff - paddle_val[i];
    }
    return 0xff;
}

/* --------------------------------------------------------- */
/* Resources & cmdline */

static int set_mouse_enabled(int val, void *param)
{
    _mouse_enabled = val;
    mousedrv_mouse_changed();
    return 0;
}

static int set_mouse_port(int val, void *param)
{
    if (val < 1 || val > 2) {
        return -1;
    }

    mouse_port = val;

    return 0;
}

static int set_mouse_type(int val, void *param)
{
    if (!((val >= 0) && (val < MOUSE_TYPE_NUM))) {
        return -1;
    }

    mouse_type = val;
    if (mouse_type == MOUSE_TYPE_ST ||
        mouse_type == MOUSE_TYPE_AMIGA ||
        mouse_type == MOUSE_TYPE_CX22) {
        mouse_kind = MOUSE_KIND_POLLED;
    } else {
        mouse_kind = MOUSE_KIND_OTHER;
    }

    return 0;
}

static const resource_int_t resources_int[] = {
    { "Mouse", 0, RES_EVENT_SAME, NULL,
      &_mouse_enabled, set_mouse_enabled, NULL },
    { NULL }
};

static const resource_int_t resources_extra_int[] = {
    { "Mousetype", MOUSE_TYPE_1351, RES_EVENT_SAME, NULL,
      &mouse_type, set_mouse_type, NULL },
    { "Mouseport", 1, RES_EVENT_SAME, NULL,
      &mouse_port, set_mouse_port, NULL },
    { NULL }
};

int mouse_resources_init(void)
{
    if (resources_register_int(resources_int) < 0) {
        return -1;
    }

    /* FIXME ugly kludge to remove port and type setting from xvic */
    if (machine_class != VICE_MACHINE_VIC20) {
        if (resources_register_int(resources_extra_int) < 0) {
            return -1;
        }
    }

    return mousedrv_resources_init();
}

static const cmdline_option_t cmdline_options[] = {
    { "-mouse", SET_RESOURCE, 0,
      NULL, NULL, "Mouse", (void *)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_MOUSE_GRAB,
      NULL, NULL },
    { "+mouse", SET_RESOURCE, 0,
      NULL, NULL, "Mouse", (void *)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_MOUSE_GRAB,
      NULL, NULL },
    { NULL }
};

static const cmdline_option_t cmdline_extra_option[] = {
    { "-mousetype", SET_RESOURCE, 1,
      NULL, NULL, "Mousetype", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SELECT_MOUSE_TYPE,
      NULL, NULL },
    { "-mouseport", SET_RESOURCE, 1,
      NULL, NULL, "Mouseport", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_P_VALUE, IDCLS_SELECT_MOUSE_JOY_PORT,
      NULL, NULL },
    { NULL }
};

int mouse_cmdline_options_init(void)
{
    if (cmdline_register_options(cmdline_options) < 0) {
        return -1;
    }

    /* FIXME ugly kludge to remove port and type setting from xvic */
    if (machine_class != VICE_MACHINE_VIC20) {
        if (cmdline_register_options(cmdline_extra_option) < 0) {
            return -1;
        }
    }

    return mousedrv_cmdline_options_init();
}

void mouse_init(void)
{
    /* FIXME ugly kludge to set the correct port and type setting for xvic */
    if (machine_class == VICE_MACHINE_VIC20) {
        set_mouse_port(1, NULL);
        set_mouse_type(MOUSE_TYPE_PADDLE, NULL);
    }

    emu_units_per_os_units =
        (float)machine_get_cycles_per_second() / vsyncarch_frequency();
#ifdef DEBUG_MOUSE
    mouse_log = log_open("Mouse");
    log_message(mouse_log, "cpu cycles / time unit %.5f",
                emu_units_per_os_units);
#endif

    neos_and_amiga_buttons = 0;
    neos_prev = 0xff;
    neosmouse_alarm = alarm_new(maincpu_alarm_context, "NEOSMOUSEAlarm", neosmouse_alarm_handler, NULL);
    mousedrv_init();
    clk_guard_add_callback(maincpu_clk_guard, clk_overflow_callback, NULL);
}

/* --------------------------------------------------------- */
/* Main API */

void mouse_button_left(int pressed)
{
    BYTE joypin = ((mouse_type == MOUSE_TYPE_PADDLE) ? 4 : 16);

    if (pressed) {
        joystick_set_value_or(mouse_port, joypin);
    } else {
        joystick_set_value_and(mouse_port, (BYTE)~joypin);
    }
}

void mouse_button_right(int pressed)
{
    switch (mouse_type) {
    case MOUSE_TYPE_1351:
	if (pressed) {
	    joystick_set_value_or(mouse_port, 1);
	} else {
	    joystick_set_value_and(mouse_port, ~1);
	}
	break;
    case MOUSE_TYPE_PADDLE:
	if (pressed) {
	    joystick_set_value_or(mouse_port, 8);
	} else {
	    joystick_set_value_and(mouse_port, ~8);
	}
	break;
    case MOUSE_TYPE_NEOS:
    case MOUSE_TYPE_AMIGA:
    case MOUSE_TYPE_CX22:
    case MOUSE_TYPE_ST:
	if (pressed) {
	    neos_and_amiga_buttons |= 1;
	} else {
	    neos_and_amiga_buttons &= ~1;
	}
	break;
    default:
	break;
    }
}

BYTE mouse_get_x(void)
{
    /* DBG(("mouse_get_x: %d", mouse_type)); */
    switch (mouse_type) {
    case MOUSE_TYPE_1351:
	return mouse_get_1351_x();
    case MOUSE_TYPE_PADDLE:
	return mouse_get_paddle_x();
    case MOUSE_TYPE_NEOS:
    case MOUSE_TYPE_AMIGA:
	return (neos_and_amiga_buttons & 1) ? 0xff : 0;
    default:
	DBG(("mouse_get_x: invalid mouse_type"));
	break;
    }
    return 0xff;
}

BYTE mouse_get_y(void)
{
    switch (mouse_type) {
    case MOUSE_TYPE_1351:
	return mouse_get_1351_y();
    case MOUSE_TYPE_PADDLE:
	return mouse_get_paddle_y();
    case MOUSE_TYPE_NEOS:
    case MOUSE_TYPE_AMIGA:
    case MOUSE_TYPE_CX22:
    case MOUSE_TYPE_ST:
	/* FIXME: is this correct ?! */
	break;
    default:
	DBG(("mouse_get_y: invalid mouse_type"));
	break;
    }
    return 0xff;
}
