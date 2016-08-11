/*
 * c64cia1.c - Definitions for the first MOS6526 (CIA) chip in the C64
 * ($DC00).
 *
 * Written by
 *  Andr� Fachat <fachat@physik.tu-chemnitz.de>
 *  Ettore Perazzoli <ettore@comm2000.it>
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
 * */

#include "vice.h"

#include <stdio.h>

#include "c64fastiec.h"
#include "c64-resources.h"
#include "c64.h"
#include "c64cia.h"
#include "cia.h"
#include "interrupt.h"
#include "drivecpu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "maincpu.h"
#include "types.h"
#include "userport_joystick.h"
#include "vicii.h"

#ifdef HAVE_RS232
#include "rsuser.h"
#endif

#ifdef HAVE_MOUSE
#include "mouse.h"
#endif

void cia1_store(WORD addr, BYTE data)
{
    ciacore_store(machine_context.cia1, addr, data);
}

BYTE cia1_read(WORD addr)
{
    return ciacore_read(machine_context.cia1, addr);
}

BYTE cia1_peek(WORD addr)
{
    return ciacore_peek(machine_context.cia1, addr);
}

void cia1_update_model(void)
{
    if (machine_context.cia1) {
        machine_context.cia1->model = cia1_model;
    }
}

static void cia_set_int_clk(cia_context_t *cia_context, int value, CLOCK clk)
{
    interrupt_set_irq(maincpu_int_status, cia_context->int_num, value, clk);
}

static void cia_restore_int(cia_context_t *cia_context, int value)
{
    interrupt_restore_irq(maincpu_int_status, cia_context->int_num, value);
}

/*************************************************************************
 * I/O
 */

void cia1_set_extended_keyboard_rows_mask(BYTE value)
{
}

static void pulse_ciapc(cia_context_t *cia_context, CLOCK rclk)
{
}

static void pre_store(void)
{
    vicii_handle_pending_alarms_external_write();
}

static void pre_read(void)
{
    vicii_handle_pending_alarms_external(0);
}

static void pre_peek(void)
{
    vicii_handle_pending_alarms_external(0);
}

static void do_reset_cia(cia_context_t *cia_context)
{
}

static void cia1_internal_lightpen_check(BYTE pa, BYTE pb)
{
    BYTE val = 0xff;
    BYTE msk = pa & ~joystick_value[2];
    BYTE m;
    int i;

    for (m = 0x1, i = 0; i < 8; m <<= 1, i++) {
        if (!(msk & m)) {
            val &= ~keyarr[i];
        }
    }

    m = val & pb & ~joystick_value[1];

    vicii_set_light_pen(maincpu_clk, !(m & 0x10));
}

void cia1_check_lightpen(void)
{
    cia1_internal_lightpen_check(machine_context.cia1->old_pa, machine_context.cia1->old_pb);
}

static void store_ciapa(cia_context_t *cia_context, CLOCK rclk, BYTE b)
{
    cia1_internal_lightpen_check(b,  machine_context.cia1->old_pb);

#ifdef HAVE_MOUSE
    mouse_set_input((b >> 6) & 0x03);

    if (_mouse_enabled) {
        if ((mouse_type == MOUSE_TYPE_NEOS) && (mouse_port == 2)) {
            neos_mouse_store(b);
        }
    }
#endif
}

static void undump_ciapa(cia_context_t *cia_context, CLOCK rclk, BYTE b)
{
}

static void store_ciapb(cia_context_t *cia_context, CLOCK rclk, BYTE byte)
{
    cia1_internal_lightpen_check(machine_context.cia1->old_pa, byte);

#ifdef HAVE_MOUSE
    if (_mouse_enabled && (mouse_type == MOUSE_TYPE_NEOS) && (mouse_port == 1)) {
        neos_mouse_store(byte);
    }
#endif
}

static void undump_ciapb(cia_context_t *cia_context, CLOCK rclk, BYTE byte)
{
}

static BYTE read_ciapa(cia_context_t *cia_context)
{
    BYTE byte;
    BYTE val = 0xff;
    BYTE msk = cia_context->old_pb & ~joystick_value[1];
    BYTE m;
    int i;

    for (m = 0x1, i = 0; i < 8; m <<= 1, i++) {
        if (!(msk & m)) {
            val &= ~rev_keyarr[i];
        }
    }

    byte = (val & (cia_context->c_cia[CIA_PRA] | ~(cia_context->c_cia[CIA_DDRA]))) & ~joystick_value[2];

#ifdef HAVE_MOUSE
    if (_mouse_enabled && (mouse_port == 2)) {
        if (mouse_type == MOUSE_TYPE_NEOS) {
            byte &= neos_mouse_read();
        }
        if (mouse_kind == MOUSE_KIND_POLLED) {
            byte &= mouse_poll();
        }
    }
#endif

    return byte;
}

inline static int ciapb_forcelow(int i)
{
    BYTE v;

    /* Check for shift lock.
       FIXME: keyboard_shiftlock state may be inconsistent
              with the (rev_)keyarr state. */
    if ((i == 7) && keyboard_shiftlock) {
        return 1;
    }

    /* Check if two or more keys are pressed. */
    v = rev_keyarr[i];
    if ((v & (v - 1)) != 0) {
        return 1;
    }

    /* TODO: check joysticks? */
    return 0;
}

static BYTE read_ciapb(cia_context_t *cia_context)
{
    BYTE byte;
    BYTE val = 0xff;
    BYTE val_outhi = ((cia_context->c_cia[CIA_DDRA]) & (cia_context->c_cia[CIA_DDRB])) & (cia_context->c_cia[CIA_PRB]);
    BYTE msk = cia_context->old_pa & ~joystick_value[2];
    BYTE m;
    int i;

    for (m = 0x1, i = 0; i < 8; m <<= 1, i++) {
        if (!(msk & m)) {
            val &= ~keyarr[i];

            /*
                Handle the special case when both port A and port B are programmed as output,
                port A outputs (active) low, and port B outputs high.

                In this case pressing either shift-lock or two or more keys of the same column
                is required to drive port B low, pressing a single key is not enough (and the
                port will read back as high). (see testprogs/CIA/ciaports)

                The initial value for val_outhi will drive the respective port B
                bits high if the above mentioned condition is met, which gives the
                expected result for single key presses.
            */
            if (ciapb_forcelow(i)) {
                val_outhi &= ~m;
            }
        }
    }

    byte = val & (cia_context->c_cia[CIA_PRB] | ~(cia_context->c_cia[CIA_DDRB]));
    byte |= val_outhi;
    byte &= ~joystick_value[1];

#ifdef HAVE_MOUSE
    if (_mouse_enabled && (mouse_port == 1)) {
        if (mouse_type == MOUSE_TYPE_NEOS) {
            byte &= neos_mouse_read();
        }
        if (mouse_kind == MOUSE_KIND_POLLED) {
            byte &= mouse_poll();
        }
    }
#endif

    return byte;
}

static void read_ciaicr(cia_context_t *cia_context)
{
    if (burst_mod == BURST_MOD_CIA1) {
        drivecpu_execute_all(maincpu_clk);
    }
}

static void read_sdr(cia_context_t *cia_context)
{
    if (burst_mod == BURST_MOD_CIA1) {
        drivecpu_execute_all(maincpu_clk);
    }
}

static void store_sdr(cia_context_t *cia_context, BYTE byte)
{
    if (burst_mod == BURST_MOD_CIA1) {
        c64fastiec_fast_cpu_write((BYTE)byte);
    }
#ifdef HAVE_RS232
    if (rsuser_enabled) {
        rsuser_tx_byte(byte);
    }
#endif
    /* FIXME: in the upcoming userport system this call needs to be conditional */
    userport_joystick_store_sdr(byte);
}

void cia1_init(cia_context_t *cia_context)
{
    ciacore_init(machine_context.cia1, maincpu_alarm_context, maincpu_int_status, maincpu_clk_guard);
}

void cia1_setup_context(machine_context_t *machine_context)
{
    cia_context_t *cia;

    machine_context->cia1 = lib_calloc(1,sizeof(cia_context_t));
    cia = machine_context->cia1;

    cia->prv = NULL;
    cia->context = NULL;

    cia->rmw_flag = &maincpu_rmw_flag;
    cia->clk_ptr = &maincpu_clk;

    cia->todticks = C64_PAL_CYCLES_PER_RFSH;

    ciacore_setup_context(cia);

    if (machine_class == VICE_MACHINE_C64SC) {
        cia->write_offset = 0;
    }

    cia->model = cia1_model;

    cia->debugFlag = 0;
    cia->irq_line = IK_IRQ;
    cia->myname = lib_msprintf("CIA1");

    cia->undump_ciapa = undump_ciapa;
    cia->undump_ciapb = undump_ciapb;
    cia->store_ciapa = store_ciapa;
    cia->store_ciapb = store_ciapb;
    cia->store_sdr = store_sdr;
    cia->read_ciapa = read_ciapa;
    cia->read_ciapb = read_ciapb;
    cia->read_ciaicr = read_ciaicr;
    cia->read_sdr = read_sdr;
    cia->cia_set_int_clk = cia_set_int_clk;
    cia->cia_restore_int = cia_restore_int;
    cia->do_reset_cia = do_reset_cia;
    cia->pulse_ciapc = pulse_ciapc;
    cia->pre_store = pre_store;
    cia->pre_read = pre_read;
    cia->pre_peek = pre_peek;
}

void cia1_set_timing(cia_context_t *cia_context, int todticks)
{
    cia_context->todticks = todticks;
}
