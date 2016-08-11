/*
 * cbm2.c
 *
 * Written by
 *  Andr� Fachat <fachat@physik.tu-chemnitz.de>
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

#include <math.h>   /* modf */
#include <stdio.h>

#include "alarm.h"
#include "autostart.h"
#include "cartridge.h"
#include "cbm2-cmdline-options.h"
#include "cbm2-resources.h"
#include "cbm2-snapshot.h"
#include "cbm2.h"
#include "cbm2acia.h"
#include "cbm2cia.h"
#include "cbm2iec.h"
#include "cbm2mem.h"
#include "cbm2tpi.h"
#include "cbm2ui.h"
#include "cia.h"
#include "clkguard.h"
#include "cmdline.h"
#include "crtc.h"
#include "datasette.h"
#include "debug.h"
#include "drive-cmdline-options.h"
#include "drive-resources.h"
#include "drive-sound.h"
#include "drive.h"
#include "drivecpu.h"
#include "iecdrive.h"
#include "kbdbuf.h"
#include "keyboard.h"
#include "log.h"
#include "machine-drive.h"
#include "machine-printer.h"
#include "machine-video.h"
#include "machine.h"
#include "maincpu.h"
#include "mem.h"
#include "monitor.h"
#include "parallel.h"
#include "printer.h"
#include "resources.h"
#include "rs232drv.h"
#include "screenshot.h"
#include "serial.h"
#include "sid-cmdline-options.h"
#include "sid-resources.h"
#include "sid.h"
#include "snapshot.h"
#include "sound.h"
#include "tape.h"
#include "tpi.h"
#include "traps.h"
#include "types.h"
#include "video.h"
#include "video-sound.h"
#include "vsync.h"

machine_context_t machine_context;

#define NUM_KEYBOARD_MAPPINGS 6

const char *machine_keymap_res_name_list[NUM_KEYBOARD_MAPPINGS] = {
    "KeymapBusinessUKSymFile", "KeymapBusinessUKPosFile",
    "KeymapGraphicsSymFile", "KeymapGraphicsPosFile",
    "KeymapBusinessDESymFile", "KeymapBusinessDEPosFile"
};

char *machine_keymap_file_list[NUM_KEYBOARD_MAPPINGS] = {
    NULL, NULL, NULL, NULL, NULL, NULL
};

const char machine_name[] = "CBM-II";
int machine_class = VICE_MACHINE_CBM6x0;

static void machine_vsync_hook(void);

/*
static long cbm2_cycles_per_sec = C610_PAL_CYCLES_PER_SEC;
static double cbm2_rfsh_per_sec = C610_PAL_RFSH_PER_SEC;
static long cbm2_cycles_per_rfsh = C610_PAL_CYCLES_PER_RFSH;
*/

static log_t cbm2_log = LOG_ERR;
static machine_timing_t machine_timing;

/* ------------------------------------------------------------------------- */

/* CBM-II-specific resource initialization.  This is called before initializing
   the machine itself with `machine_init()'.  */
int machine_resources_init(void)
{
    if (traps_resources_init() < 0
        || vsync_resources_init() < 0
        || machine_video_resources_init() < 0
        || cbm2_resources_init() < 0
        || cartridge_resources_init() < 0
        || crtc_resources_init() < 0
        || sound_resources_init() < 0
        || sid_resources_init() < 0
        || drive_resources_init() < 0
        || datasette_resources_init() < 0
        || acia1_resources_init() < 0
        || rs232drv_resources_init() < 0
        || printer_resources_init() < 0
#ifndef COMMON_KBD
        || pet_kbd_resources_init() < 0
#endif
        )
        return -1;
    return 0;
}

void machine_resources_shutdown(void)
{
    video_resources_shutdown();
    cbm2_resources_shutdown();
    sound_resources_shutdown();
    rs232drv_resources_shutdown();
    printer_resources_shutdown();
    drive_resources_shutdown();
}

/* CBM-II-specific command-line option initialization.  */
int machine_cmdline_options_init(void)
{
    if (traps_cmdline_options_init() < 0
        || vsync_cmdline_options_init() < 0
        || video_init_cmdline_options() < 0
        || cbm2_cmdline_options_init() < 0
        || cartridge_cmdline_options_init() < 0
        || crtc_cmdline_options_init() < 0
        || sound_cmdline_options_init() < 0
        || sid_cmdline_options_init() < 0
        || drive_cmdline_options_init() < 0
        || datasette_cmdline_options_init() < 0
        || acia1_cmdline_options_init() < 0
        || rs232drv_cmdline_options_init() < 0
        || printer_cmdline_options_init() < 0
#ifndef COMMON_KBD
        || pet_kbd_cmdline_options_init() < 0
#endif
        )
        return -1;

    return 0;
}

/* ------------------------------------------------------------------------- */
/* provide the 50(?)Hz IRQ signal for the standard IRQ */

#define SIGNAL_VERT_BLANK_OFF tpicore_set_int(machine_context.tpi1, 0, 1);

#define SIGNAL_VERT_BLANK_ON  tpicore_set_int(machine_context.tpi1, 0, 0);

/* ------------------------------------------------------------------------- */
/* ... while the other CBM-II use the CRTC retrace signal. */

static void cbm2_crtc_signal(unsigned int signal) {
    if (signal) {
        SIGNAL_VERT_BLANK_ON
    } else {
        SIGNAL_VERT_BLANK_OFF
    }
}

/* ------------------------------------------------------------------------- */

static void cbm2_monitor_init(void)
{
    unsigned int dnr;
    monitor_cpu_type_t asm6502;
    monitor_interface_t *drive_interface_init[DRIVE_NUM];
    monitor_cpu_type_t *asmarray[2];

    asmarray[0]=&asm6502;
    asmarray[1]=NULL;

    asm6502_init(&asm6502);

    for (dnr = 0; dnr < DRIVE_NUM; dnr++)
        drive_interface_init[dnr] = drivecpu_monitor_interface_get(dnr);

    /* Initialize the monitor.  */
    monitor_init(maincpu_monitor_interface_get(), drive_interface_init,
                 asmarray);
}

void machine_setup_context(void)
{
    cia1_setup_context(&machine_context);
    tpi1_setup_context(&machine_context);
    tpi2_setup_context(&machine_context);
    machine_printer_setup_context(&machine_context);
}

/* CBM-II-specific initialization.  */
int machine_specific_init(void)
{
    cbm2_log = log_open("CBM2");

    cbm2_init_ok = 1;

    /* Setup trap handling - must be before mem_load() */
    traps_init();

    if (mem_load() < 0)
        return -1;

    rs232drv_init();

    /* initialize print devices */
    printer_init();

    if (crtc_init() == NULL) {
        return -1;
    }
    crtc_set_retrace_callback(cbm2_crtc_signal);
    crtc_set_retrace_type(0);
    crtc_set_hw_options(1, 0x7ff, 0x1000, 512, -0x2000);

    cia1_init(machine_context.cia1);
    acia1_init();
    tpi1_init(machine_context.tpi1);
    tpi2_init(machine_context.tpi2);

#ifndef COMMON_KBD
    /* Initialize the keyboard.  */
    if (cbm2_kbd_init() < 0)
        return -1;
#endif

    /* Initialize the datasette emulation.  */
    datasette_init();

    /* Fire up the hardware-level 1541 emulation.  */
    drive_init();

    cbm2_monitor_init();

    /* Initialize vsync and register our hook function.  */
    vsync_init(machine_vsync_hook);
    vsync_set_machine_parameter(machine_timing.rfsh_per_sec,
                                machine_timing.cycles_per_sec);

    /* Initialize native sound chip */
    sid_sound_chip_init();

    drive_sound_init();
    video_sound_init();

    /* Initialize sound.  Notice that this does not really open the audio
       device yet.  */
    sound_init(machine_timing.cycles_per_sec, machine_timing.cycles_per_rfsh);

    /* Initialize keyboard buffer.
       This appears to work but doesn't account for banking. */
    kbdbuf_init(939, 209, 10, (CLOCK)(machine_timing.rfsh_per_sec * machine_timing.cycles_per_rfsh));

    /* Initialize the CBM-II-specific part of the UI.  */
    cbm2ui_init();

    cbm2iec_init();

    machine_drive_stub();

#if defined (USE_XF86_EXTENSIONS) && \
    (defined(USE_XF86_VIDMODE_EXT) || defined (HAVE_XRANDR))
    {
        /* set fullscreen if user used `-fullscreen' on cmdline */
        int fs;
        resources_get_int("UseFullscreen", &fs);
        if (fs) {
            resources_set_int("CRTCFullscreen", 1);
        }
    }
#endif
    return 0;
}

/* CBM-II-specific initialization.  */
void machine_specific_reset(void)
{
    ciacore_reset(machine_context.cia1);
    tpicore_reset(machine_context.tpi1);
    tpicore_reset(machine_context.tpi2);
    acia1_reset();

    sid_reset();

    crtc_reset();

    printer_reset();

    rs232drv_reset();

    drive_reset();
    datasette_reset();

    mem_reset();
}

void machine_specific_powerup(void)
{
}

void machine_specific_shutdown(void)
{
    /* and the tape */
    tape_image_detach_internal(1);

    ciacore_shutdown(machine_context.cia1);
    tpicore_shutdown(machine_context.tpi1);
    tpicore_shutdown(machine_context.tpi2);

    /* close the video chip(s) */
    crtc_shutdown();

    cbm2ui_shutdown();
}

void machine_handle_pending_alarms(int num_write_cycles)
{
}

/* ------------------------------------------------------------------------- */

/* This hook is called at the end of every frame.  */
static void machine_vsync_hook(void)
{
    CLOCK sub;

    drive_vsync_hook();

    autostart_advance();

    screenshot_record();

    sub = clk_guard_prevent_overflow(maincpu_clk_guard);

    /* The drive has to deal both with our overflowing and its own one, so
       it is called even when there is no overflowing in the main CPU.  */
    drivecpu_prevent_clk_overflow_all(sub);
}

/* Dummy - no restore key.  */
void machine_set_restore_key(int v)
{
}

int machine_has_restore_key(void)
{
    return 0;
}

/* ------------------------------------------------------------------------- */

long machine_get_cycles_per_second(void)
{
    return machine_timing.cycles_per_sec;
}

long machine_get_cycles_per_frame(void)
{
    return machine_timing.cycles_per_rfsh;
}

void machine_get_line_cycle(unsigned int *line, unsigned int *cycle, int *half_cycle)
{
    *line = (unsigned int)((maincpu_clk) / machine_timing.cycles_per_line
            % machine_timing.screen_lines);

    *cycle = (unsigned int)((maincpu_clk) % machine_timing.cycles_per_line);

    *half_cycle = (int)-1;
}

void machine_change_timing(int timeval)
{
    /* log_message(LOG_DEFAULT, "machine_change_timing_c610 %d", timeval); */

    switch (timeval) {
        case MACHINE_SYNC_PAL:
            machine_timing.cycles_per_sec = C610_PAL_CYCLES_PER_SEC;
            machine_timing.cycles_per_rfsh = C610_PAL_CYCLES_PER_RFSH;
            machine_timing.rfsh_per_sec = C610_PAL_RFSH_PER_SEC;
            machine_timing.cycles_per_line = C610_PAL_CYCLES_PER_LINE;
            machine_timing.screen_lines = C610_PAL_SCREEN_LINES;
            break;
        case MACHINE_SYNC_NTSC:
            machine_timing.cycles_per_sec = C610_NTSC_CYCLES_PER_SEC;
            machine_timing.cycles_per_rfsh = C610_NTSC_CYCLES_PER_RFSH;
            machine_timing.rfsh_per_sec = C610_NTSC_RFSH_PER_SEC;
            machine_timing.cycles_per_line = C610_NTSC_CYCLES_PER_LINE;
            machine_timing.screen_lines = C610_NTSC_SCREEN_LINES;
            break;
        default:
            log_error(LOG_DEFAULT, "Unknown machine timing.");
    }

    debug_set_machine_parameter(machine_timing.cycles_per_line,
                                machine_timing.screen_lines);
    drive_set_machine_parameter(machine_timing.cycles_per_sec);
    clk_guard_set_clk_base(maincpu_clk_guard, machine_timing.cycles_per_rfsh);

    cia1_set_timing(machine_context.cia1, machine_timing.cycles_per_rfsh);
}

/* Set the screen refresh rate, as this is variable in the CRTC */
void machine_set_cycles_per_frame(long cpf)
{
    double i, f;

    machine_timing.cycles_per_rfsh = cpf;
    machine_timing.rfsh_per_sec = ((double)machine_timing.cycles_per_sec)
                                  / ((double)cpf);

    f = modf(machine_timing.rfsh_per_sec, &i) * 1000;
    log_message(cbm2_log, "cycles per frame set to %ld, refresh to %d.%03dHz",
                cpf, (int)i, (int)f);

    vsync_set_machine_parameter(machine_timing.rfsh_per_sec,
                                machine_timing.cycles_per_sec);
}

/* ------------------------------------------------------------------------- */

int machine_write_snapshot(const char *name, int save_roms, int save_disks,
                           int event_mode)
{
    return cbm2_snapshot_write(name, save_roms, save_disks, event_mode);
}

int machine_read_snapshot(const char *name, int event_mode)
{
    return cbm2_snapshot_read(name, event_mode);
}

/* ------------------------------------------------------------------------- */

int machine_autodetect_psid(const char *name)
{
    return -1;
}

void machine_play_psid(int tune)
{
}

int machine_screenshot(screenshot_t *screenshot, struct video_canvas_s *canvas)
{
    if (canvas == crtc_get_canvas()) {
        crtc_screenshot(screenshot);
        return 0;
    }

    return -1;
}

int machine_canvas_async_refresh(struct canvas_refresh_s *refresh,
                                 struct video_canvas_s *canvas)
{
    if (canvas == crtc_get_canvas()) {
        crtc_async_refresh(refresh);
        return 0;
    }
    return -1;
}

/*-----------------------------------------------------------------------*/

int machine_num_keyboard_mappings(void)
{
    return NUM_KEYBOARD_MAPPINGS;
}

struct image_contents_s *machine_diskcontents_bus_read(unsigned int unit)
{
    return NULL;
}

BYTE machine_tape_type_default(void)
{
    return TAPE_CAS_TYPE_PRG;
}

int machine_addr_in_ram(unsigned int addr)
{
    /* FIXME are these correct? */
    return (addr < 0xe000 && !(addr >= 0xa000 && addr < 0xc000)) ? 1 : 0;
}

const char *machine_get_name(void)
{
    return (machine_class == VICE_MACHINE_CBM6x0) ? machine_name : "CBM-II-5x0";
}

#ifdef USE_SDLUI
/* Kludges for vsid & linking issues */
const char **csidmodel = NULL;
void psid_init_driver(void) {}
#endif

/* FIXME: further rework snapshot stuff and remove these */
int cbm2_c500_snapshot_write_module(snapshot_t *p)
{
    return 0;
}

int cbm2_c500_snapshot_read_module(snapshot_t *p)
{
    return 0;
}
