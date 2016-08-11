/*
 * drivecpu.c - 6502 processor emulation of CBM disk drives.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andreas Boose <viceteam@t-online.de>
 *
 * Patches by
 *  Andre Fachat <a.fachat@physik.tu-chemnitz.de>
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

#include <stdio.h>
#include <string.h>

#include "6510core.h"
#include "alarm.h"
#include "clkguard.h"
#include "debug.h"
#include "drive.h"
#include "drivecpu.h"
#include "drive-check.h"
#include "drivemem.h"
#include "drivetypes.h"
#include "interrupt.h"
#include "lib.h"
#include "log.h"
#include "machine-drive.h"
#include "machine.h"
#include "mem.h"
#include "monitor.h"
#include "mos6510.h"
#include "rotation.h"
#include "snapshot.h"
#include "types.h"


#define DRIVE_CPU

static void drive_jam(drive_context_t *drv);

static BYTE drive_bank_read(int bank, WORD addr, void *context);
static BYTE drive_bank_peek(int bank, WORD addr, void *context);
static void drive_bank_store(int bank, WORD addr, BYTE value, void *context);
static void drivecpu_toggle_watchpoints(int flag, void *context);
static void drivecpu_set_bank_base(void *context);

static interrupt_cpu_status_t *drivecpu_int_status_ptr[DRIVE_NUM];


monitor_interface_t *drivecpu_monitor_interface_get(unsigned int dnr)
{
    return drive_context[dnr]->cpu->monitor_interface;
}

void drivecpu_setup_context(struct drive_context_s *drv)
{
    monitor_interface_t *mi;
    drivecpu_context_t *cpu;

    drv->cpu = lib_calloc(1, sizeof(drivecpu_context_t));
    cpu = drv->cpu;

    drv->cpud = lib_calloc(1, sizeof(drivecpud_context_t));
    drv->func = lib_malloc(sizeof(drivefunc_context_t));

    cpu->int_status = interrupt_cpu_status_new();
    interrupt_cpu_status_init(cpu->int_status, &(cpu->last_opcode_info));
    drivecpu_int_status_ptr[drv->mynumber] = cpu->int_status;

    cpu->rmw_flag = 0;
    cpu->d_bank_limit = -1;
    cpu->pageone = NULL;
    cpu->snap_module_name = lib_msprintf("DRIVECPU%d", drv->mynumber);
    cpu->identification_string = lib_msprintf("DRIVE#%d", drv->mynumber + 8);
    cpu->monitor_interface = monitor_interface_new();
    mi = cpu->monitor_interface;
    mi->context = (void *)drv;
    mi->cpu_regs = &(cpu->cpu_regs);
    mi->z80_cpu_regs = NULL;
    mi->int_status = cpu->int_status;
    mi->clk = &(drive_clk[drv->mynumber]);
    mi->current_bank = 0;
    mi->mem_bank_list = NULL;
    mi->mem_bank_from_name = NULL;
    mi->get_line_cycle = NULL;
    mi->mem_bank_read = drive_bank_read;
    mi->mem_bank_peek = drive_bank_peek;
    mi->mem_bank_write = drive_bank_store;
    mi->mem_ioreg_list_get = drivemem_ioreg_list_get;
    mi->toggle_watchpoints_func = drivecpu_toggle_watchpoints;
    mi->set_bank_base = drivecpu_set_bank_base;
    cpu->monspace = monitor_diskspace_mem(drv->mynumber);
}

/* ------------------------------------------------------------------------- */

#define LOAD(a)           (drv->cpud->read_func[(a) >> 8](drv, (WORD)(a)))
#define LOAD_ZERO(a)      (drv->cpud->read_func[0](drv, (WORD)(a)))
#define LOAD_ADDR(a)      (LOAD(a) | (LOAD((a) + 1) << 8))
#define LOAD_ZERO_ADDR(a) (LOAD_ZERO(a) | (LOAD_ZERO((a) + 1) << 8))
#define STORE(a, b)       (drv->cpud->store_func[(a) >> 8](drv, (WORD)(a), \
                          (BYTE)(b)))
#define STORE_ZERO(a, b)  (drv->cpud->store_func[0](drv, (WORD)(a), \
                          (BYTE)(b)))

/* FIXME: pc can not jump to VIA adress space in 1541 and 1571 emulation.  */
/* FIXME: SFD1001 does not use bank_base at all due to messy memory mapping.
   We should use tables like in maincpu instead (AF) */
#define JUMP(addr)                                       \
    do {                                                 \
        reg_pc = (addr);                                 \
        if (drv->drive->type == 1001) {                  \
            cpu->d_bank_base = NULL;                     \
            cpu->d_bank_limit = -1;                      \
        } else if (reg_pc < 0x2000) {                    \
            cpu->d_bank_base = drv->cpud->drive_ram;     \
            cpu->d_bank_limit = 0x07fd;                  \
        } else if (reg_pc >= drv->drive->rom_start) {    \
            cpu->d_bank_base = drv->drive->rom - 0x8000; \
            cpu->d_bank_limit = 0xfffd;                  \
        } else {                                         \
            cpu->d_bank_base = NULL;                     \
            cpu->d_bank_limit = -1;                      \
        }                                                \
    } while (0)

/* ------------------------------------------------------------------------- */

/* This is the external interface for banked memory access.  */

static BYTE drive_bank_read(int bank, WORD addr, void *context)
{
    drive_context_t *drv = (drive_context_t *)context;

    return drv->cpud->read_func[addr >> 8](drv, addr);
}

/* FIXME: use peek in IO area */
static BYTE drive_bank_peek(int bank, WORD addr, void *context)
{
    drive_context_t *drv = (drive_context_t *)context;

    return drv->cpud->read_func[addr >> 8](drv, addr);
}

static void drive_bank_store(int bank, WORD addr, BYTE value, void *context)
{
    drive_context_t *drv = (drive_context_t *)context;

    drv->cpud->store_func[addr >> 8](drv, addr, value);
}

/* ------------------------------------------------------------------------- */

static void cpu_reset(drive_context_t *drv)
{
    int preserve_monitor;

    preserve_monitor = drv->cpu->int_status->global_pending_int & IK_MONITOR;

    log_message(drv->drive->log, "RESET.");

    interrupt_cpu_status_reset(drv->cpu->int_status);

    *(drv->clk_ptr) = 6;
    rotation_reset(drv->drive);
    machine_drive_reset(drv);

    if (preserve_monitor)
        interrupt_monitor_trap_on(drv->cpu->int_status);
}

static void drivecpu_toggle_watchpoints(int flag, void *context)
{
    drive_context_t *drv = (drive_context_t *)context;

    if (flag) {
        memcpy(drv->cpud->read_func, drv->cpud->read_func_watch,
               sizeof(drive_read_func_t *) * 0x101);
        memcpy(drv->cpud->store_func, drv->cpud->store_func_watch,
               sizeof(drive_store_func_t *) * 0x101);
    } else {
        memcpy(drv->cpud->read_func, drv->cpud->read_func_nowatch,
               sizeof(drive_read_func_t *) * 0x101);
        memcpy(drv->cpud->store_func, drv->cpud->store_func_nowatch,
               sizeof(drive_store_func_t *) * 0x101);
    }
}

void drivecpu_reset_clk(drive_context_t *drv)
{
    drv->cpu->last_clk = maincpu_clk;
    drv->cpu->last_exc_cycles = 0;
    drv->cpu->stop_clk = 0;
}

void drivecpu_reset(drive_context_t *drv)
{
    int preserve_monitor;

    *(drv->clk_ptr) = 0;
    drivecpu_reset_clk(drv);

    preserve_monitor = drv->cpu->int_status->global_pending_int & IK_MONITOR;

    interrupt_cpu_status_reset(drv->cpu->int_status);

    if (preserve_monitor)
        interrupt_monitor_trap_on(drv->cpu->int_status);

    /* FIXME -- ugly, should be changed in interrupt.h */
    interrupt_trigger_reset(drv->cpu->int_status, *(drv->clk_ptr));
}

void drivecpu_trigger_reset(unsigned int dnr)
{
    interrupt_trigger_reset(drivecpu_int_status_ptr[dnr], drive_clk[dnr] + 1);
}

static void drive_cpu_early_init(drive_context_t *drv)
{
    drv->cpu->clk_guard = clk_guard_new(drv->clk_ptr, CLOCK_MAX
                                        - CLKGUARD_SUB_MIN);

    drv->cpu->alarm_context = alarm_context_new(
                                  drv->cpu->identification_string);

    machine_drive_init(drv);
}

void drivecpu_early_init_all(void)
{
    unsigned int dnr;

    for (dnr = 0; dnr < DRIVE_NUM; dnr++)
        drive_cpu_early_init(drive_context[dnr]);
}

void drivecpu_shutdown(drive_context_t *drv)
{
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    if (cpu->alarm_context != NULL)
        alarm_context_destroy(cpu->alarm_context);
    if (cpu->clk_guard != NULL)
        clk_guard_destroy(cpu->clk_guard);

    monitor_interface_destroy(cpu->monitor_interface);
    interrupt_cpu_status_destroy(cpu->int_status);

    lib_free(cpu->snap_module_name);
    lib_free(cpu->identification_string);

    machine_drive_shutdown(drv);

    lib_free(drv->func);
    lib_free(drv->cpud);
    lib_free(cpu);
}

void drivecpu_init(drive_context_t *drv, int type)
{
    drivemem_init(drv, type);
    drivecpu_reset(drv);
}

inline void drivecpu_wake_up(drive_context_t *drv)
{
    /* FIXME: this value could break some programs, or be way too high for
       others.  Maybe we should put it into a user-definable resource.  */
    if (maincpu_clk - drv->cpu->last_clk > 0xffffff
        && *(drv->clk_ptr) > 934639) {
        log_message(drv->drive->log, "Skipping cycles.");
        drv->cpu->last_clk = maincpu_clk;
    }
}

inline void drivecpu_sleep(drive_context_t *drv)
{
    /* Currently does nothing.  But we might need this hook some day.  */
}

/* Make sure the drive clock counters never overflow; return nonzero if
   they have been decremented to prevent overflow.  */
static CLOCK drivecpu_prevent_clk_overflow(drive_context_t *drv, CLOCK sub)
{
    if (sub != 0) {
        /* First, get in sync with what the main CPU has done.  Notice that
           `clk' has already been decremented at this point.  */
        if (drv->drive->enable) {
            if (drv->cpu->last_clk < sub) {
                /* Hm, this is kludgy.  :-(  */
                drivecpu_execute_all(maincpu_clk + sub);
            }
            drv->cpu->last_clk -= sub;
        } else {
            drv->cpu->last_clk = maincpu_clk;
        }
    }

    /* Then, check our own clock counters.  */
    return clk_guard_prevent_overflow(drv->cpu->clk_guard);
}

void drivecpu_prevent_clk_overflow_all(CLOCK sub)
{
    unsigned int dnr;

    for (dnr = 0; dnr < DRIVE_NUM; dnr++)
        drivecpu_prevent_clk_overflow(drive_context[dnr], sub);
}

/* Handle a ROM trap. */
inline static DWORD drive_trap_handler(drive_context_t *drv)
{
    if (MOS6510_REGS_GET_PC(&(drv->cpu->cpu_regs)) == drv->drive->trap) {
        MOS6510_REGS_SET_PC(&(drv->cpu->cpu_regs), drv->drive->trapcont);
        if (drv->drive->idling_method == DRIVE_IDLE_TRAP_IDLE) {
            CLOCK next_clk;

            next_clk = alarm_context_next_pending_clk(drv->cpu->alarm_context);

            if (next_clk > drv->cpu->stop_clk)
                next_clk = drv->cpu->stop_clk;

            *(drv->clk_ptr) = next_clk;
        }
        return 0;
    }
    return (DWORD)-1;
}

static void drive_generic_dma(void)
{
    /* Generic DMA hosts can be implemented here.
       Not very likey for disk drives. */
}

/* -------------------------------------------------------------------------- */

/* Return nonzero if a pending NMI should be dispatched now.  This takes
   account for the internal delays of the 6510, but does not actually check
   the status of the NMI line.  */
inline static int interrupt_check_nmi_delay(interrupt_cpu_status_t *cs,
                                            CLOCK cpu_clk)
{
    CLOCK nmi_clk = cs->nmi_clk + INTERRUPT_DELAY;

    /* Branch instructions delay IRQs and NMI by one cycle if branch
       is taken with no page boundary crossing.  */
    if (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr))
        nmi_clk++;

    if (cpu_clk >= nmi_clk)
        return 1;

    return 0;
}

/* Return nonzero if a pending IRQ should be dispatched now.  This takes
   account for the internal delays of the 6510, but does not actually check
   the status of the IRQ line.  */
inline static int interrupt_check_irq_delay(interrupt_cpu_status_t *cs,
                                            CLOCK cpu_clk)
{
    CLOCK irq_clk = cs->irq_clk + INTERRUPT_DELAY;

    /* Branch instructions delay IRQs and NMI by one cycle if branch
       is taken with no page boundary crossing.  */
    if (OPINFO_DELAYS_INTERRUPT(*cs->last_opcode_info_ptr))
        irq_clk++;

    /* If an opcode changes the I flag from 1 to 0, the 6510 needs
       one more opcode before it triggers the IRQ routine.  */
    if (cpu_clk >= irq_clk) {
        if (!OPINFO_ENABLES_IRQ(*cs->last_opcode_info_ptr)) {
            return 1;
        } else {
            cs->global_pending_int |= IK_IRQPEND;
        }
    }
    return 0;
}


/* MPi: For some reason MSVC is generating a compiler fatal error when optimising this function? */
#ifdef _MSC_VER
#pragma optimize("",off)
#endif
/* -------------------------------------------------------------------------- */
/* Execute up to the current main CPU clock value.  This automatically
   calculates the corresponding number of clock ticks in the drive.  */
void drivecpu_execute(drive_context_t *drv, CLOCK clk_value)
{
    CLOCK cycles;
	int tcycles;
    drivecpu_context_t *cpu;

#define reg_a   (cpu->cpu_regs.a)
#define reg_x   (cpu->cpu_regs.x)
#define reg_y   (cpu->cpu_regs.y)
#define reg_pc  (cpu->cpu_regs.pc)
#define reg_sp  (cpu->cpu_regs.sp)
#define reg_p   (cpu->cpu_regs.p)
#define flag_z  (cpu->cpu_regs.z)
#define flag_n  (cpu->cpu_regs.n)

    cpu = drv->cpu;

    drivecpu_wake_up(drv);

    /* Calculate number of main CPU clocks to emulate */
    if (clk_value > cpu->last_clk)
        cycles = clk_value - cpu->last_clk;
    else
        cycles = 0;

    while (cycles != 0) {
        tcycles = cycles > 10000 ? 10000 : cycles;
        cycles -= tcycles;

        cpu->cycle_accum += drv->cpud->sync_factor * tcycles;
        cpu->stop_clk += cpu->cycle_accum >> 16;
        cpu->cycle_accum &= 0xffff;
    }

    /* Run drive CPU emulation until the stop_clk clock has been reached.
     * There appears to be a nasty 32-bit overflow problem here, so we
     * paper over it by only considering subtractions of 2nd complement
     * integers. */
    while ((int) (*(drv->clk_ptr) - cpu->stop_clk) < 0) {

/* Include the 6502/6510 CPU emulation core.  */

#define CLK (*(drv->clk_ptr))
#define RMW_FLAG (cpu->rmw_flag)
#define PAGE_ONE (cpu->pageone)
#define LAST_OPCODE_INFO (cpu->last_opcode_info)
#define LAST_OPCODE_ADDR (cpu->last_opcode_addr)
#define TRACEFLG (debug.drivecpu_traceflg[drv->mynumber])

#define CPU_INT_STATUS (cpu->int_status)

#define ALARM_CONTEXT (cpu->alarm_context)

#define JAM() drive_jam(drv)

#define ROM_TRAP_ALLOWED() 1

#define ROM_TRAP_HANDLER() drive_trap_handler(drv)

#define CALLER (cpu->monspace)

#define DMA_FUNC drive_generic_dma()

#define DMA_ON_RESET

#define drivecpu_byte_ready_egde_clear()          \
    do {                                          \
        rotation_rotate_disk(drv->drive);         \
        drv->drive->byte_ready_edge = 0;          \
    } while (0)

#define drivecpu_byte_ready() (rotation_rotate_disk(drv->drive), \
                               drv->drive->byte_ready_edge)

#define cpu_reset() (cpu_reset)(drv)
#define bank_limit (cpu->d_bank_limit)
#define bank_base (cpu->d_bank_base)

#include "6510core.c"

    }

    cpu->last_clk = clk_value;
    drivecpu_sleep(drv);
}

#ifdef _MSC_VER
#pragma optimize("",on)
#endif

void drivecpu_execute_all(CLOCK clk_value)
{
    unsigned int dnr;

    for (dnr = 0; dnr < DRIVE_NUM; dnr++) {
        if (drive_context[dnr]->drive->enable)
            drivecpu_execute(drive_context[dnr], clk_value);
    }
}

/* ------------------------------------------------------------------------- */

static void drivecpu_set_bank_base(void *context)
{
    drive_context_t *drv;
    drivecpu_context_t *cpu;

    drv = (drive_context_t *)context;
    cpu = drv->cpu;

    JUMP(reg_pc);
}

/* Inlining this fuction makes no sense and would only bloat the code.  */
static void drive_jam(drive_context_t *drv)
{
    unsigned int tmp;
    char *dname = "  Drive";
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    switch(drv->drive->type) {
      case DRIVE_TYPE_1541:
        dname = "  1541";
        break;
      case DRIVE_TYPE_1541II:
        dname = "1541-II";
        break;
      case DRIVE_TYPE_1551:
        dname = "  1551";
        break;
      case DRIVE_TYPE_1570:
        dname = "  1570";
        break;
      case DRIVE_TYPE_1571:
        dname = "  1571";
        break;
      case DRIVE_TYPE_1571CR:
        dname = "  1571CR";
        break;
      case DRIVE_TYPE_1581:
        dname = "  1581";
        break;
      case DRIVE_TYPE_2000:
        dname = "  2000";
        break;
      case DRIVE_TYPE_4000:
        dname = "  4000";
        break;
      case DRIVE_TYPE_2031:
        dname = "  2031";
        break;
      case DRIVE_TYPE_1001:
        dname = "  1001";
        break;
      case DRIVE_TYPE_2040:
        dname = "  2040";
        break;
      case DRIVE_TYPE_3040:
        dname = "  3040";
        break;
      case DRIVE_TYPE_4040:
        dname = "  4040";
        break;
      case DRIVE_TYPE_8050:
        dname = "  8050";
        break;
      case DRIVE_TYPE_8250:
        dname = "  8250";
      break;
    }

    tmp = machine_jam("%s CPU: JAM at $%04X  ", dname, (int)reg_pc);
    switch (tmp) {
      case JAM_RESET:
        reg_pc = 0xeaa0;
        drivecpu_set_bank_base((void *)drv);
        machine_trigger_reset(MACHINE_RESET_MODE_SOFT);
        break;
      case JAM_HARD_RESET:
        reg_pc = 0xeaa0;
        drivecpu_set_bank_base((void *)drv);
        machine_trigger_reset(MACHINE_RESET_MODE_HARD);
        break;
      case JAM_MONITOR:
        monitor_startup(drv->cpu->monspace);
        break;
      default:
        CLK++;
    }
}

/* ------------------------------------------------------------------------- */

#define SNAP_MAJOR 1
#define SNAP_MINOR 1

int drivecpu_snapshot_write_module(drive_context_t *drv, snapshot_t *s)
{
    snapshot_module_t *m;
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    m = snapshot_module_create(s, drv->cpu->snap_module_name,
                               ((BYTE)(SNAP_MAJOR)), ((BYTE)(SNAP_MINOR)));
    if (m == NULL)
        return -1;

    if (0
        || SMW_DW(m, (DWORD) *(drv->clk_ptr)) < 0
        || SMW_B(m, (BYTE)MOS6510_REGS_GET_A(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (BYTE)MOS6510_REGS_GET_X(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (BYTE)MOS6510_REGS_GET_Y(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (BYTE)MOS6510_REGS_GET_SP(&(cpu->cpu_regs))) < 0
        || SMW_W(m, (WORD)MOS6510_REGS_GET_PC(&(cpu->cpu_regs))) < 0
        || SMW_B(m, (BYTE)MOS6510_REGS_GET_STATUS(&(cpu->cpu_regs))) < 0
        || SMW_DW(m, (DWORD)(cpu->last_opcode_info)) < 0
        || SMW_DW(m, (DWORD)(cpu->last_clk)) < 0
        || SMW_DW(m, (DWORD)(cpu->cycle_accum)) < 0
        || SMW_DW(m, (DWORD)(cpu->last_exc_cycles)) < 0
        || SMW_DW(m, (DWORD)(cpu->stop_clk)) < 0
        )
        goto fail;

    if (interrupt_write_snapshot(cpu->int_status, m) < 0)
        goto fail;

    if (drv->drive->type == DRIVE_TYPE_1541
        || drv->drive->type == DRIVE_TYPE_1541II
        || drv->drive->type == DRIVE_TYPE_1551
        || drv->drive->type == DRIVE_TYPE_1570
        || drv->drive->type == DRIVE_TYPE_1571
        || drv->drive->type == DRIVE_TYPE_1571CR
        || drv->drive->type == DRIVE_TYPE_2031) {
        if (SMW_BA(m, drv->cpud->drive_ram, 0x800) < 0)
            goto fail;
    }

    if (drv->drive->type == DRIVE_TYPE_1581
        || drv->drive->type == DRIVE_TYPE_2000
        || drv->drive->type == DRIVE_TYPE_4000) {
        if (SMW_BA(m, drv->cpud->drive_ram, 0x2000) < 0)
            goto fail;
    }
    if (drive_check_old(drv->drive->type)) {
        if (SMW_BA(m, drv->cpud->drive_ram, 0x1100) < 0)
            goto fail;
    }

    if (interrupt_write_new_snapshot(cpu->int_status, m) < 0)
        goto fail;

    return snapshot_module_close(m);

fail:
    if (m != NULL)
        snapshot_module_close(m);
    return -1;
}

int drivecpu_snapshot_read_module(drive_context_t *drv, snapshot_t *s)
{
    BYTE major, minor;
    snapshot_module_t *m;
    BYTE a, x, y, sp, status;
    WORD pc;
    drivecpu_context_t *cpu;

    cpu = drv->cpu;

    m = snapshot_module_open(s, drv->cpu->snap_module_name, &major, &minor);
    if (m == NULL)
        return -1;

    /* Before we start make sure all devices are reset.  */
    drivecpu_reset(drv);

    /* XXX: Assumes `CLOCK' is the same size as a `DWORD'.  */
    if (0
        || SMR_DW(m, drv->clk_ptr) < 0
        || SMR_B(m, &a) < 0
        || SMR_B(m, &x) < 0
        || SMR_B(m, &y) < 0
        || SMR_B(m, &sp) < 0
        || SMR_W(m, &pc) < 0
        || SMR_B(m, &status) < 0
        || SMR_DW_UINT(m, &(cpu->last_opcode_info)) < 0
        || SMR_DW(m, &(cpu->last_clk)) < 0
        || SMR_DW(m, &(cpu->cycle_accum)) < 0
        || SMR_DW(m, &(cpu->last_exc_cycles)) < 0
        || SMR_DW(m, &(cpu->stop_clk)) < 0
        )
        goto fail;

    MOS6510_REGS_SET_A(&(cpu->cpu_regs), a);
    MOS6510_REGS_SET_X(&(cpu->cpu_regs), x);
    MOS6510_REGS_SET_Y(&(cpu->cpu_regs), y);
    MOS6510_REGS_SET_SP(&(cpu->cpu_regs), sp);
    MOS6510_REGS_SET_PC(&(cpu->cpu_regs), pc);
    MOS6510_REGS_SET_STATUS(&(cpu->cpu_regs), status);

    log_message(drv->drive->log, "RESET (For undump).");

    interrupt_cpu_status_reset(cpu->int_status);

    machine_drive_reset(drv);

    if (interrupt_read_snapshot(cpu->int_status, m) < 0)
        goto fail;

    if (drv->drive->type == DRIVE_TYPE_1541
        || drv->drive->type == DRIVE_TYPE_1541II
        || drv->drive->type == DRIVE_TYPE_1551
        || drv->drive->type == DRIVE_TYPE_1570
        || drv->drive->type == DRIVE_TYPE_1571
        || drv->drive->type == DRIVE_TYPE_1571CR
        || drv->drive->type == DRIVE_TYPE_2031) {
        if (SMR_BA(m, drv->cpud->drive_ram, 0x800) < 0)
            goto fail;
    }

    if (drv->drive->type == DRIVE_TYPE_1581
        || drv->drive->type == DRIVE_TYPE_2000
        || drv->drive->type == DRIVE_TYPE_4000) {
        if (SMR_BA(m, drv->cpud->drive_ram, 0x2000) < 0)
            goto fail;
    }

    if (drive_check_old(drv->drive->type)) {
        if (SMR_BA(m, drv->cpud->drive_ram, 0x1100) < 0)
            goto fail;
    }

    /* Update `*bank_base'.  */
    JUMP(reg_pc);

    if (interrupt_read_new_snapshot(drv->cpu->int_status, m) < 0) {
        goto fail;
    }

    return snapshot_module_close(m);

fail:
    if (m != NULL)
        snapshot_module_close(m);
    return -1;
}

