/*
 * debug.h - Various debugging options.
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

#ifndef VICE_DEBUG_H
#define VICE_DEBUG_H

#include "types.h"

/* You can also enable/disable this option by hand. */
#if 0
/* This enables debugging.  Attention: It makes things a bit slower.  */
#define DEBUG
#endif

#define DEBUG_NORMAL    0
#define DEBUG_SMALL     1
#define DEBUG_HISTORY   2
#define DEBUG_AUTOPLAY  3

#define DEBUG_HISTORY_MAXFILESIZE   4000000
#define DEBUG_MAXLINELEN             128

typedef struct debug_s {
#ifdef DEBUG
    int maincpu_traceflg;
    int drivecpu_traceflg[4];
    int trace_mode;

     /*
      * if this is set, the CPU will break into the monitor before executing the 
      * next statement. This is often handy for debugging.
      */
    int perform_break_into_monitor;

    /*! If this is set, inputs and outputs to the IEC bus are output. */
    int iec;
#endif
    int do_core_dumps;
} debug_t;

extern debug_t debug;

struct interrupt_cpu_status_s;

extern int debug_resources_init(void);
extern int debug_cmdline_options_init(void);

extern void debug_set_machine_parameter(unsigned int cycles,
                                        unsigned int lines);
extern void debug_maincpu(DWORD reg_pc, CLOCK mclk, const char *dis,
                          BYTE reg_a, BYTE reg_x, BYTE reg_y, BYTE reg_sp);
extern void debug_drive(DWORD reg_pc, CLOCK mclk, const char *dis,
                        BYTE reg_a, BYTE reg_x, BYTE reg_y, BYTE reg_sp,
                        unsigned int driveno);
extern void debug_irq(struct interrupt_cpu_status_s *cs, CLOCK iclk);
extern void debug_nmi(struct interrupt_cpu_status_s *cs, CLOCK iclk);
extern void debug_dma(const char *txt, CLOCK dclk, int num);
extern void debug_text(const char *text);
extern void debug_start_recording(void);
extern void debug_stop_recording(void);
extern void debug_start_playback(void);
extern void debug_stop_playback(void);
extern void debug_set_milestone(void);
extern void debug_reset_milestone(void);
extern void debug_check_autoplay_mode(void);


#ifdef DEBUG

extern void debug_iec_drv_write(unsigned int data);
extern void debug_iec_drv_read(unsigned int data);

extern void debug_iec_bus_write(unsigned int data);
extern void debug_iec_bus_read(unsigned int data);

# define DEBUG_IEC_DRV_WRITE(_data) debug_iec_drv_write(_data)
# define DEBUG_IEC_DRV_READ(_data) debug_iec_drv_read(_data)

# define DEBUG_IEC_BUS_WRITE(_data) debug_iec_bus_write(_data)
# define DEBUG_IEC_BUS_READ(_data) debug_iec_bus_read(_data)

#else

# define DEBUG_IEC_DRV_WRITE(_data)
# define DEBUG_IEC_DRV_READ(_data)

# define DEBUG_IEC_BUS_WRITE(_data)
# define DEBUG_IEC_BUS_READ(_data)

#endif


#ifdef NDEBUG

# define STATIC_ASSERT(_x)

#else

# define STATIC_ASSERT(_x) \
    { \
        BYTE dummy[1 - 2 * ((_x) == 0)]; \
        dummy[0] = dummy[0] - dummy[0]; /* prevent "unused variable" warning */ \
    }

#endif

#endif
