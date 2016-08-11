/*
 * joystick.c - Common joystick emulation.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *
 * Based on old code by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Jouko Valta <jopi@stekt.oulu.fi>
 *  André Fachat <fachat@physik.tu-chemnitz.de>
 *  Bernhard Kuhn <kuhn@eikon.e-technik.tu-muenchen.de>
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

#include <stdlib.h>
#include <string.h>

#include "archdep.h"
#include "alarm.h"
#include "keyboard.h"
#include "joy.h"
#include "joystick.h"
#include "kbd.h"
#include "log.h"
#include "machine.h"
#include "maincpu.h"
#include "network.h"
#include "resources.h"
#include "snapshot.h"
#include "types.h"
#include "uiapi.h"
#include "userport_joystick.h"
#include "vice-event.h"

#define JOYSTICK_RAND() (rand() % machine_get_cycles_per_frame())

/* Global joystick value.  */
/*! \todo SRT: document: what are these values joystick_value[0, 1, 2, ..., 5] used for? */
BYTE joystick_value[JOYSTICK_NUM + 1] = { 0 };

/* Latched joystick status.  */
static BYTE latch_joystick_value[JOYSTICK_NUM + 1] = { 0 };
static BYTE network_joystick_value[JOYSTICK_NUM + 1] = { 0 };

/* mapping of the joystick ports */
int joystick_port_map[JOYSTICK_NUM] = { 0 };

/* to prevent illegal direction combinations */
static int joystick_opposite_enable = 0;
static const BYTE joystick_opposite_direction[] = 
    { 0, 2, 1, 3, 8, 10, 9, 11, 4, 6, 5, 7, 12, 14, 13, 15 };

/* Callback to machine specific joystick routines, needed for lightpen triggering */
static joystick_machine_func_t joystick_machine_func = NULL;

static alarm_t *joystick_alarm = NULL;

static CLOCK joystick_delay;

#ifdef COMMON_KBD
static int joykeys[3][9];
#endif

/*! \todo SRT: offset is unused! */

static void joystick_latch_matrix(CLOCK offset)
{
    BYTE idx;

    if (network_connected()) {
        idx = network_joystick_value[0];
        if (idx > 0)
            joystick_value[idx] = network_joystick_value[idx];
        else
            memcpy(joystick_value, network_joystick_value, sizeof(joystick_value));
    } else {
        memcpy(joystick_value, latch_joystick_value, sizeof(joystick_value));
    }

    if (joystick_machine_func != NULL) {
        joystick_machine_func();
    }

    ui_display_joyport(joystick_value);
}

/*-----------------------------------------------------------------------*/

static void joystick_event_record(void)
{
    event_record(EVENT_JOYSTICK_VALUE, (void *)joystick_value,
                 sizeof(joystick_value));
}

void joystick_event_playback(CLOCK offset, void *data)
{
    memcpy(latch_joystick_value, data, sizeof(joystick_value));

    joystick_latch_matrix(offset);
}

static void joystick_latch_handler(CLOCK offset, void *data)
{
    alarm_unset(joystick_alarm);
    alarm_context_update_next_pending(joystick_alarm->context);

    joystick_latch_matrix(offset);

    joystick_event_record(); 
}

void joystick_event_delayed_playback(void *data)
{
    /*! \todo SRT: why network_joystick_value?
     * and why sizeof latch_joystick_value, 
     * if the target is network_joystick_value?
     */
    memcpy(network_joystick_value, data, sizeof(latch_joystick_value));
    alarm_set(joystick_alarm, maincpu_clk + joystick_delay);
}

void joystick_register_machine(joystick_machine_func_t func)
{
    joystick_machine_func = func;
}

void joystick_register_delay(unsigned int delay)
{
    joystick_delay = delay;
}
/*-----------------------------------------------------------------------*/
static void joystick_process_latch(void)
{
    if (network_connected()) {
        CLOCK joystick_delay = JOYSTICK_RAND();
        network_event_record(EVENT_JOYSTICK_DELAY,
                (void *)&joystick_delay, sizeof(joystick_delay));
        network_event_record(EVENT_JOYSTICK_VALUE, 
                (void *)latch_joystick_value, sizeof(latch_joystick_value));
    } 
    else
    {
        alarm_set(joystick_alarm, maincpu_clk + JOYSTICK_RAND());
    }
}

void joystick_set_value_absolute(unsigned int joyport, BYTE value)
{
    if (event_playback_active())
        return;

    if (latch_joystick_value[joyport] != value) {
        latch_joystick_value[joyport] = value;
        latch_joystick_value[0] = (BYTE)joyport;
        joystick_process_latch();
    }
}

void joystick_set_value_or(unsigned int joyport, BYTE value)
{
    if (event_playback_active())
        return;

    latch_joystick_value[joyport] |= value;

    if (!joystick_opposite_enable) {
        latch_joystick_value[joyport] &= ~joystick_opposite_direction[value & 0xf];
    }

    latch_joystick_value[0] = (BYTE)joyport;
    joystick_process_latch();
}

void joystick_set_value_and(unsigned int joyport, BYTE value)
{
    if (event_playback_active())
        return;

    latch_joystick_value[joyport] &= value;
    latch_joystick_value[0] = (BYTE)joyport;
    joystick_process_latch();
}

void joystick_clear(unsigned int joyport)
{
    latch_joystick_value[joyport] = 0;
    latch_joystick_value[0] = (BYTE)joyport;
    joystick_latch_matrix(0);
}

void joystick_clear_all(void)
{
    memset(latch_joystick_value, 0, sizeof latch_joystick_value);
    joystick_latch_matrix(0);
}

BYTE get_joystick_value(int index)
{
    return joystick_value[index];
}

/*-----------------------------------------------------------------------*/

#ifdef COMMON_KBD
/* the order of values in joypad_bits is the same as in joystick_direction_t */
static int joypad_bits[9] = {
    0x10, 0x06, 0x02, 0x0a, 0x04, 0x08, 0x05, 0x01, 0x09
};

static int joypad_status[3][9];

typedef enum {
    KEYSET_FIRE,
    KEYSET_SW,
    KEYSET_S,
    KEYSET_SE,
    KEYSET_W,
    KEYSET_E,
    KEYSET_NW,
    KEYSET_N,
    KEYSET_NE
} joystick_direction_t;

static int joyreleaseval(int column, int *status)
{
    int val = 0;

    switch (column) {
      case KEYSET_SW:
        val = (status[KEYSET_S] ? 0 : joypad_bits[KEYSET_S]) | 
              (status[KEYSET_W] ? 0 : joypad_bits[KEYSET_W]);
        break;
      case KEYSET_SE:
        val = (status[KEYSET_S] ? 0 : joypad_bits[KEYSET_S]) | 
              (status[KEYSET_E] ? 0 : joypad_bits[KEYSET_E]);
        break;
      case KEYSET_NW:
        val = (status[KEYSET_N] ? 0 : joypad_bits[KEYSET_N]) | 
              (status[KEYSET_W] ? 0 : joypad_bits[KEYSET_W]);
        break;
      case KEYSET_NE:
        val = (status[KEYSET_N] ? 0 : joypad_bits[KEYSET_N]) | 
              (status[KEYSET_E] ? 0 : joypad_bits[KEYSET_E]);
        break;
      default:
        val = joypad_bits[column];
        break;
    }
    return ~val;
}

/* toggle keyset joystick. 
   this disables any active key-based joystick and is useful for typing. */
static int joykeys_enable = 0;

static int set_joykeys_enable(int val, void *param)
{
    joykeys_enable = val;
    return 0;
}

static int set_joystick_opposite_enable(int val, void *param)
{
    joystick_opposite_enable = val;
    return 0;
}

#define DEFINE_SET_KEYSET(num)                       \
    static int set_keyset##num(int val, void *param) \
    {                                                \
        joykeys[num][vice_ptr_to_int(param)] = val;  \
                                                     \
        return 0;                                    \
    }

DEFINE_SET_KEYSET(1)
DEFINE_SET_KEYSET(2)

static const resource_int_t resources_int[] = {
    { "KeySet1NorthWest", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_NW], set_keyset1, (void *)KEYSET_NW },
    { "KeySet1North", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_N], set_keyset1, (void *)KEYSET_N },
    { "KeySet1NorthEast", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_NE], set_keyset1, (void *)KEYSET_NE },
    { "KeySet1East", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_E], set_keyset1, (void *)KEYSET_E },
    { "KeySet1SouthEast", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_SE], set_keyset1, (void *)KEYSET_SE },
    { "KeySet1South", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_S], set_keyset1, (void *)KEYSET_S },
    { "KeySet1SouthWest", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_SW], set_keyset1, (void *)KEYSET_SW },
    { "KeySet1West", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_W], set_keyset1, (void *)KEYSET_W },
    { "KeySet1Fire", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[1][KEYSET_FIRE], set_keyset1, (void *)KEYSET_FIRE },
    { "KeySet2NorthWest", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_NW], set_keyset2, (void *)KEYSET_NW },
    { "KeySet2North", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_N], set_keyset2, (void *)KEYSET_N },
    { "KeySet2NorthEast", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_NE], set_keyset2, (void *)KEYSET_NE },
    { "KeySet2East", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_E], set_keyset2, (void *)KEYSET_E },
    { "KeySet2SouthEast", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_SE], set_keyset2, (void *)KEYSET_SE },
    { "KeySet2South", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_S], set_keyset2, (void *)KEYSET_S },
    { "KeySet2SouthWest", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_SW], set_keyset2, (void *)KEYSET_SW },
    { "KeySet2West", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_W], set_keyset2, (void *)KEYSET_W },
    { "KeySet2Fire", ARCHDEP_KEYBOARD_SYM_NONE, RES_EVENT_NO, NULL,
      &joykeys[2][KEYSET_FIRE], set_keyset2, (void *)KEYSET_FIRE },
    { "KeySetEnable", 1, RES_EVENT_NO, NULL,
      &joykeys_enable, set_joykeys_enable, NULL },
    { "JoyOpposite", 0, RES_EVENT_NO, NULL,
      &joystick_opposite_enable, set_joystick_opposite_enable, NULL },
    { NULL }
};

static int set_userport_joystick_enable(int val, void *param)
{
    userport_joystick_enable = val;
    return 0;
}

static int set_userport_joystick_type(int val, void *param)
{
    if (val >= USERPORT_JOYSTICK_NUM) {
        return -1;
    }
    userport_joystick_type = val;
    return 0;
}

/* FIXME: command line options to set these resources are missing */
/* FIXME: ExtraJoy* needs to be renamed to UserportJoy* in due time */
static const resource_int_t userport_resources_int[] = {
    { "ExtraJoy", 0, RES_EVENT_NO, NULL,
      &userport_joystick_enable, set_userport_joystick_enable, NULL },
    { "ExtraJoyType", 0, RES_EVENT_NO, NULL,
      &userport_joystick_type, set_userport_joystick_type, NULL },
    { NULL }
};

int joystick_check_set(signed long key, int keysetnum, unsigned int joyport)
{
    int column;

    /* if joykeys are disabled then ignore key sets */
    if (!joykeys_enable) {
        return 0;
    }

    for (column = 0; column < 9; column++) {
        if (key == joykeys[keysetnum][column]) {
            if (joypad_bits[column]) {
                /*joystick_value[joyport] |= joypad_bits[column];*/
                joystick_set_value_or(joyport, (BYTE)joypad_bits[column]);
                joypad_status[keysetnum][column] = 1;
            } else {
                /*joystick_value[joyport] = 0;*/
                joystick_set_value_absolute(joyport, 0);
                memset(joypad_status[keysetnum], 0, sizeof(joypad_status[keysetnum]));
            }
            return 1;
        }
    }
    return 0;
}

int joystick_check_clr(signed long key, int keysetnum, unsigned int joyport)
{
    int column;

    /* if joykeys are disabled then ignore key sets */
    if (!joykeys_enable) {
        return 0;
    }

    for (column = 0; column < 9; column++) {
        if (key == joykeys[keysetnum][column]) {
            /*joystick_value[joyport] &= joyreleaseval(column,
                                                     joypad_status[joynum]);*/
            joystick_set_value_and(joyport, (BYTE)joyreleaseval(column,
                                   joypad_status[keysetnum]));
            joypad_status[keysetnum][column] = 0;
            return 1;
        }
    }
    return 0;
}

void joystick_joypad_clear(void)
{
    memset(joypad_status, 0, sizeof(joypad_status));
}

/*-----------------------------------------------------------------------*/

int joystick_init_resources(void)
{
    resources_register_int(resources_int);

    if (machine_class != VICE_MACHINE_PLUS4) {
        resources_register_int(userport_resources_int);
    }

    return joystick_arch_init_resources();
}
#endif

int joystick_init(void)
{
    joystick_alarm = alarm_new(maincpu_alarm_context, "Joystick",
                               joystick_latch_handler, NULL);

#ifdef COMMON_KBD
    kbd_initialize_numpad_joykeys(joykeys[0]);
#endif

    return joy_arch_init();
}

/*--------------------------------------------------------------------------*/
int joystick_snapshot_write_module(snapshot_t *s)
{
    snapshot_module_t *m;

    m = snapshot_module_create(s, "JOYSTICK", 1, 0);
    if (m == NULL) {
       return -1;
    }

    if (SMW_BA(m, joystick_value, (JOYSTICK_NUM + 1)) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    if (snapshot_module_close(m) < 0) {
        return -1;
    }

    return 0;
}

int joystick_snapshot_read_module(snapshot_t *s)
{
    BYTE major_version, minor_version;
    snapshot_module_t *m;

    m = snapshot_module_open(s, "JOYSTICK",
                             &major_version, &minor_version);
    if (m == NULL) {
        return 0;
    }

    if (SMR_BA(m, joystick_value, (JOYSTICK_NUM + 1)) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    snapshot_module_close(m);
    return 0;
}
