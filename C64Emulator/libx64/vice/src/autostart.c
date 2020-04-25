/*
 * autostart.c - Automatic image loading and starting.
 *
 * Written by
 *  Teemu Rantanen <tvr@cs.hut.fi>
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Andr� Fachat <a.fachat@physik.tu-chemnitz.de>
 *  Andreas Boose <viceteam@t-online.de>
 *  Thomas Bretz <tbretz@ph.tum.de>
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

/* #define DEBUG_AUTOSTART */

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "archapi.h"
#include "archdep.h"
#include "autostart.h"
#include "autostart-prg.h"
#include "attach.h"
#include "charset.h"
#include "cmdline.h"
#include "datasette.h"
#include "drive.h"
#include "fileio.h"
#include "fsdevice.h"
#include "imagecontents.h"
#include "tapecontents.h"
#include "diskcontents.h"
#include "interrupt.h"
#include "kbdbuf.h"
#include "lib.h"
#include "log.h"
#include "machine-bus.h"
#include "machine.h"
#include "maincpu.h"
#include "mem.h"
#include "monitor.h"
#include "network.h"
#include "resources.h"
#include "snapshot.h"
#include "tape.h"
#include "translate.h"
#include "types.h"
#include "uiapi.h"
#include "util.h"
#include "vdrive.h"
#include "vdrive-bam.h"
#include "vice-event.h"

#ifdef DEBUG_AUTOSTART
#define DBG(_x_)        log_debug _x_
#else
#define DBG(_x_)
#endif

static void autostart_done(void);

/* Kernal addresses.  Set by `autostart_init()'.  */

static WORD blnsw;           /* Cursor Blink enable: 0 = Flash Cursor */
static int pnt;                 /* Pointer: Current Screen Line Address */
static int pntr;                /* Cursor Column on Current Line */
static int lnmx;                /* Physical Screen Line Length */

/* Current state of the autostart routine.  */
static enum {
    AUTOSTART_NONE,
    AUTOSTART_ERROR,
    AUTOSTART_HASTAPE,
    AUTOSTART_PRESSPLAYONTAPE,
    AUTOSTART_LOADINGTAPE,
    AUTOSTART_HASDISK,
    AUTOSTART_LOADINGDISK,
    AUTOSTART_HASSNAPSHOT,
    AUTOSTART_WAITLOADREADY,
    AUTOSTART_WAITLOADING,
    AUTOSTART_WAITSEARCHINGFOR,
    AUTOSTART_INJECT,
    AUTOSTART_DONE
} autostartmode = AUTOSTART_NONE;

#define AUTOSTART_WAIT_BLINK   0
#define AUTOSTART_NOWAIT_BLINK 1

/* Log descriptor.  */
static log_t autostart_log = LOG_ERR;

/* Flag: was true drive emulation turned on when we started booting the disk
   image?  */
static int orig_drive_true_emulation_state = -1;

/* Flag: warp mode state before booting */
static int orig_warp_mode = -1;

/* PETSCII name of the program to load. NULL if default */
static char *autostart_program_name = NULL;

/* Minimum number of cycles before we feed BASIC with commands.  */
static CLOCK min_cycles;
static CLOCK autostart_initial_delay_cycles;

/* Flag: Do we want to switch true drive emulation on/off during autostart?
 * Normally, this is the same as handle_drive_true_emulation_by_machine;
 * however, the user can override this decision by specifying
 * -autostart-no-true-drive-emulation
 */
static int handle_drive_true_emulation_overridden;

/* Flag: Does the machine want us to switch true drive emulation on/off during autostart? */
static int handle_drive_true_emulation_by_machine;

/* Flag: autostart is initialized.  */
static int autostart_enabled = 0;

/* Flag: Autostart the file or just load it?  */
static unsigned int autostart_run_mode;

/* Flag: maincpu_clk isn't resetted yet */
static int autostart_wait_for_reset;

/* Flag: load stage after LOADING enters ROM area */
static int entered_rom = 0;

/* Flag: trap monitor after done */
static int trigger_monitor = 0;

int autostart_ignore_reset = 0; /* FIXME: only used by datasette.c, does it really have to be global? */

/* additional random delay of up to 10 frames */
#define AUTOSTART_RAND() (1 + (int)(((float)machine_get_cycles_per_frame()) * 10.0f * rand() / (RAND_MAX + 1.0)))

/* ------------------------------------------------------------------------- */

static int autostart_basic_load = 0;

static int AutostartRunWithColon = 0;

static int AutostartHandleTrueDriveEmulation = 0;

static int AutostartWarp = 0;

static int AutostartDelay = 0;
static int AutostartDelayRandom = 0;

static int AutostartPrgMode = AUTOSTART_PRG_MODE_VFS;

static char *AutostartPrgDiskImage = NULL;

static const char * const AutostartRunCommandsAvailable[] = { "RUN\r", "RUN:\r" };

static const char * AutostartRunCommand = NULL;

static void set_handle_true_drive_emulation_state(void)
{
    handle_drive_true_emulation_overridden =
        AutostartHandleTrueDriveEmulation ?
            handle_drive_true_emulation_by_machine : 0;
}

/*! \internal \brief set if autostart should use LOAD ... ,1 */
static int set_autostart_basic_load(int val, void *param)
{
    autostart_basic_load = val ? 1 : 0;

    return 0;
}

/*! \internal \brief set if autostart should execute with a colon or not

 \param val
   if 0, the "RUN" command at the end of autostart is executed without
   a colon; else, it will be executed with a colon.

 \param param
   unused

 \return
   0 on success. else -1.
*/
static int set_autostart_run_with_colon(int val, void *param)
{
    AutostartRunWithColon = val ? 1 : 0;

    AutostartRunCommand = AutostartRunCommandsAvailable[AutostartRunWithColon];

    return 0;
}

/*! \internal \brief set if autostart should handle TDE or not

 \param val
   if 0, autostart does not handle TDE even if the machine says it can
   handle it.

 \param param
   unused

 \return
   0 on success. else -1.
*/
static int set_autostart_handle_tde(int val, void *param)
{
    AutostartHandleTrueDriveEmulation = val ? 1 : 0;

    set_handle_true_drive_emulation_state();

    return 0;
}

/*! \internal \brief set if autostart should enable warp mode */
static int set_autostart_warp(int val, void *param)
{
    AutostartWarp = val ? 1 : 0;

    return 0;
}

/*! \internal \brief set initial autostart delay. 0 means default. */
static int set_autostart_delay(int val, void *param)
{
    if ((val < 0) || (val > 1000)) {
        val = 0;
    }
    AutostartDelay = val;
    return 0;
}

/*! \internal \brief set initial autostart random delay. 0 means off, 1 means on. */
static int set_autostart_delayrandom(int val, void *param)
{
    AutostartDelayRandom = val ? 1 : 0;
    return 0;
}

/*! \internal \brief set autostart prg mode */
static int set_autostart_prg_mode(int val, void *param)
{
    AutostartPrgMode = val;
    if ((val < 0) || (val > AUTOSTART_PRG_MODE_LAST)) {
        val = 0;
    }

    return 0;
}

/*! \internal \brief set disk image name of autostart prg mode */

static int set_autostart_prg_disk_image(const char *val, void *param)
{
    if (util_string_set(&AutostartPrgDiskImage, val))
        return 0;

    return 0;
}

/*! \brief string resources used by autostart */
static resource_string_t resources_string[] = {
    { "AutostartPrgDiskImage", NULL, RES_EVENT_NO, NULL,
      &AutostartPrgDiskImage, set_autostart_prg_disk_image, NULL },
    { NULL }
};

/*! \brief integer resources used by autostart */
static const resource_int_t resources_int[] = {
    { "AutostartBasicLoad", 0, RES_EVENT_NO, (resource_value_t)0,
      &autostart_basic_load, set_autostart_basic_load, NULL },
    { "AutostartRunWithColon", 0, RES_EVENT_NO, (resource_value_t)0,
      &AutostartRunWithColon, set_autostart_run_with_colon, NULL },
    { "AutostartHandleTrueDriveEmulation", 0, RES_EVENT_NO, (resource_value_t)0,
      &AutostartHandleTrueDriveEmulation, set_autostart_handle_tde, NULL },
    { "AutostartWarp", 1, RES_EVENT_NO, (resource_value_t)0,
      &AutostartWarp, set_autostart_warp, NULL },
    { "AutostartPrgMode", 0, RES_EVENT_NO, (resource_value_t)0,
      &AutostartPrgMode, set_autostart_prg_mode, NULL },
    { "AutostartDelay", 0, RES_EVENT_NO, (resource_value_t)0,
      &AutostartDelay, set_autostart_delay, NULL },
    { "AutostartDelayRandom", 1, RES_EVENT_NO, (resource_value_t)0,
      &AutostartDelayRandom, set_autostart_delayrandom, NULL },
    { NULL }
};

/*! \brief initialize the resources
 \return
   0 on success, else -1.

 \remark
   Registers the integer resources
*/
int autostart_resources_init(void)
{
    resources_string[0].factory_value = archdep_default_autostart_disk_image_file_name();

    if (resources_register_string(resources_string) < 0)
        return -1;

    return resources_register_int(resources_int);
}

void autostart_resources_shutdown(void)
{
    lib_free(AutostartPrgDiskImage);
    AutostartPrgDiskImage = NULL;
    
    lib_free(resources_string[0].factory_value);
    resources_string[0].factory_value = NULL;
}

/* ------------------------------------------------------------------------- */

static const cmdline_option_t cmdline_options[] =
{
    { "-basicload", SET_RESOURCE, 0,
      NULL, NULL, "AutostartBasicLoad", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_AUTOSTART_LOAD_TO_BASIC_START,
      NULL, NULL },
    { "+basicload", SET_RESOURCE, 0,
      NULL, NULL, "AutostartBasicLoad", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_AUTOSTART_LOAD_WITH_1,
      NULL, NULL },
    { "-autostartwithcolon", SET_RESOURCE, 0,
      NULL, NULL, "AutostartRunWithColon", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_AUTOSTARTWITHCOLON,
      NULL, NULL },
    { "+autostartwithcolon", SET_RESOURCE, 0,
      NULL, NULL, "AutostartRunWithColon", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_AUTOSTARTWITHCOLON,
      NULL, NULL },
    { "-autostart-handle-tde", SET_RESOURCE, 0,
      NULL, NULL, "AutostartHandleTrueDriveEmulation", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_AUTOSTART_HANDLE_TDE,
      NULL, NULL },
    { "+autostart-handle-tde", SET_RESOURCE, 0,
      NULL, NULL, "AutostartHandleTrueDriveEmulation", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_AUTOSTART_HANDLE_TDE,
      NULL, NULL },
    { "-autostart-warp", SET_RESOURCE, 0,
      NULL, NULL, "AutostartWarp", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_WARP_MODE_AUTOSTART,
      NULL, NULL },
    { "+autostart-warp", SET_RESOURCE, 0,
      NULL, NULL, "AutostartWarp", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_WARP_MODE_AUTOSTART,
      NULL, NULL },
    { "-autostartprgmode", SET_RESOURCE, 1,
      NULL, NULL, "AutostartPrgMode", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_SET_AUTOSTART_MODE_FOR_PRG,
      NULL, NULL },
    { "-autostartprgdiskimage", SET_RESOURCE, 1,
      NULL, NULL, "AutostartPrgDiskImage", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_SET_DISK_IMAGE_FOR_AUTOSTART_PRG,
      NULL, NULL },
    { "-autostart-delay", SET_RESOURCE, 1,
      NULL, NULL, "AutostartDelay", NULL,
      USE_PARAM_ID, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_SET_AUTOSTART_DELAY,
      NULL, NULL },
    { "-autostart-delay-random", SET_RESOURCE, 0,
      NULL, NULL, "AutostartDelayRandom", (resource_value_t)1,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_ENABLE_AUTOSTART_RANDOM_DELAY,
      NULL, NULL },
    { "+autostart-delay-random", SET_RESOURCE, 0,
      NULL, NULL, "AutostartDelayRandom", (resource_value_t)0,
      USE_PARAM_STRING, USE_DESCRIPTION_ID,
      IDCLS_UNUSED, IDCLS_DISABLE_AUTOSTART_RANDOM_DELAY,
      NULL, NULL },
    { NULL }
};

/*! \brief initialize the command-line options

 \return
   0 on success, else -1.

 \remark
   Registers the command-line options
*/
int autostart_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}

/* ------------------------------------------------------------------------- */

/* Deallocate program name if we have one */
static void deallocate_program_name(void)
{
    lib_free(autostart_program_name);
    autostart_program_name = NULL;
}

static enum { YES, NO, NOT_YET } check(const char *s, unsigned int blink_mode)
{
    int screen_addr, line_length, cursor_column, addr, i;

    screen_addr = (int)(mem_read((WORD)(pnt))
                  | (mem_read((WORD)(pnt + 1)) << 8));
    cursor_column = (int)mem_read((WORD)(pntr));

    line_length = (int)(lnmx < 0 ? -lnmx : mem_read((WORD)(lnmx)) + 1);

    if (!kbdbuf_is_empty())
        return NOT_YET;

    if (blink_mode == AUTOSTART_WAIT_BLINK && cursor_column != 0)
        return NOT_YET;

    if (blink_mode == AUTOSTART_WAIT_BLINK && blnsw != 0
        && mem_read(blnsw) != 0)
        return NOT_YET;

    if (blink_mode == AUTOSTART_WAIT_BLINK)
        addr = screen_addr - line_length;
    else
        addr = screen_addr;

    for (i = 0; s[i] != '\0'; i++) {

        if (mem_read((WORD)(addr + i)) != s[i] % 64) {
            if (mem_read((WORD)(addr + i)) != (BYTE)32)
                return NO;
            return NOT_YET;
        }
    }

    return YES;
}

static void set_true_drive_emulation_mode(int on)
{
    resources_set_int("DriveTrueEmulation", on);
    ui_update_menus();
}

static int get_true_drive_emulation_state(void)
{
    int value;

    if (resources_get_int("DriveTrueEmulation", &value) < 0)
        return 0;

    return value;
}

static void set_warp_mode(int on)
{
    resources_set_int("WarpMode", on);
    ui_update_menus();
}

static int get_warp_mode(void)
{
    int value;

    if (resources_get_int("WarpMode", &value) < 0) {
        return 0;
    }

    return value;
}

static void enable_warp_if_requested(void)
{
    /* enable warp mode? */
    if (AutostartWarp) {
        orig_warp_mode = get_warp_mode();
        if (!orig_warp_mode) {
            log_message(autostart_log, "Turning Warp mode on");
            set_warp_mode(1);
        }
    }
}

static void disable_warp_if_was_requested(void)
{
    /* disable warp mode */
    if (AutostartWarp) {
        if (!orig_warp_mode) {
            log_message(autostart_log, "Turning Warp mode off");
            set_warp_mode(0);
        }
    }
}

static void check_rom_area(void)
{
    /* enter ROM ? */
    if (!entered_rom) {
        if (reg_pc >= 0xe000) {
            log_message(autostart_log, "Entered ROM at $%04x", reg_pc);
            entered_rom = 1;
        }
    } else {
        /* special case for auto-starters: ROM left. We also consider
         * BASIC area to be ROM, because it's responsible for writing "READY."
         */
        if (machine_addr_in_ram(reg_pc)) {
            log_message(autostart_log, "Left ROM for $%04x", reg_pc);
            disable_warp_if_was_requested();
            autostart_done();
        }
    }
}

/* ------------------------------------------------------------------------- */

static void load_snapshot_trap(WORD unused_addr, void *unused_data)
{
    if (autostart_program_name
        && machine_read_snapshot((char *)autostart_program_name, 0) < 0) {
        ui_error(translate_text(IDGS_CANNOT_LOAD_SNAPSHOT_FILE));
    }

    ui_update_menus();
}

/* ------------------------------------------------------------------------- */

/* Reset autostart.  */
void autostart_reinit(CLOCK _min_cycles, int _handle_drive_true_emulation,
                      int _blnsw, int _pnt, int _pntr, int _lnmx)
{
    blnsw = (WORD)(_blnsw);
    pnt = _pnt;
    pntr = _pntr;
    lnmx = _lnmx;

    min_cycles = _min_cycles;

    handle_drive_true_emulation_by_machine = _handle_drive_true_emulation;

    set_handle_true_drive_emulation_state();

    if (_min_cycles) {
        autostart_enabled = 1;
    } else {
        autostart_enabled = 0;
    }
}

/* Initialize autostart.  */
int autostart_init(CLOCK _min_cycles, int handle_drive_true_emulation,
                   int blnsw, int pnt, int pntr, int lnmx)
{
    autostart_prg_init();

    autostart_reinit(_min_cycles, handle_drive_true_emulation, blnsw, pnt,
                     pntr, lnmx);

    if (autostart_log == LOG_ERR) {
        autostart_log = log_open("AUTOSTART");
        if (autostart_log == LOG_ERR)
            return -1;
    }

    return 0;
}

void autostart_disable(void)
{
    if (!autostart_enabled)
        return;

    autostartmode = AUTOSTART_ERROR;
    trigger_monitor = 0;
    deallocate_program_name();
    log_error(autostart_log, "Turned off.");
}

/* Control if the monitor will be triggered after an autostart */
void autostart_trigger_monitor(int enable)
{
    trigger_monitor = enable;
}

/* This is called if all steps of an autostart operation were passed successfully */
void autostart_done(void)
{
    autostartmode = AUTOSTART_DONE;
    
    /* Enter monitor after done */
    if(trigger_monitor) {
        trigger_monitor = 0;
        monitor_startup_trap();
        log_message(autostart_log, "Done. Returning to Monitor.");
    } else {
        log_message(autostart_log, "Done.");
    }
}

/* ------------------------------------------------------------------------- */

/* This function is called by the `serialreceivebyte()' trap as soon as EOF
   is reached.  */
static void disk_eof_callback(void)
{
    if (handle_drive_true_emulation_overridden) {
        BYTE id[2], *buffer;
        unsigned int track, sector;

        if (orig_drive_true_emulation_state) {
            log_message(autostart_log, "Turning true drive emulation on.");
            vdrive_bam_get_disk_id(8, id);
            vdrive_get_last_read(&track, &sector, &buffer);
        }
        set_true_drive_emulation_mode(orig_drive_true_emulation_state);
        if (orig_drive_true_emulation_state) {
            drive_set_disk_memory(id, track, sector, drive_context[0]);
            drive_set_last_read(track, sector, buffer, drive_context[0]);
        }
    }

    if (autostartmode != AUTOSTART_NONE) {
        if (autostart_run_mode == AUTOSTART_MODE_RUN) {
            log_message(autostart_log, "Starting program.");
            kbdbuf_feed(AutostartRunCommand);
        }
        else {
            log_message(autostart_log, "Program loaded.");
        }
    }
    
    autostart_done();

    machine_bus_eof_callback_set(NULL);

    disable_warp_if_was_requested();
}

/* This function is called by the `serialattention()' trap before
   returning.  */
static void disk_attention_callback(void)
{
    machine_bus_attention_callback_set(NULL);

    /* Next step is waiting for end of loading, to turn true drive emulation
       on.  */
    machine_bus_eof_callback_set(disk_eof_callback);
}

/* ------------------------------------------------------------------------- */

static void advance_hastape(void)
{
    char *tmp;

    switch (check("READY.", AUTOSTART_WAIT_BLINK)) {
      case YES:
        log_message(autostart_log, "Loading file.");
        if (autostart_program_name) {
            tmp = util_concat("LOAD\"", autostart_program_name, "\":\r", NULL);
            kbdbuf_feed(tmp);
            lib_free(tmp);
        } else {
            kbdbuf_feed("LOAD:\r");
        }
        if (tape_tap_attached()) {
            autostartmode = AUTOSTART_PRESSPLAYONTAPE;
        } else {
            autostartmode = AUTOSTART_LOADINGTAPE;
        }
        entered_rom = 0;
        deallocate_program_name();
        break;
      case NO:
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        break;
    }
}

static void advance_pressplayontape(void)
{
    switch (check("PRESS PLAY ON TAPE", AUTOSTART_NOWAIT_BLINK)) {
      case YES:
        autostartmode = AUTOSTART_LOADINGTAPE;
        datasette_control(DATASETTE_CONTROL_START);
        break;
      case NO:
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        break;
    }
}

static void advance_loadingtape(void)
{
    switch (check("READY.", AUTOSTART_WAIT_BLINK)) {
      case YES:
        disable_warp_if_was_requested();

        if (autostart_run_mode == AUTOSTART_MODE_RUN) {
            log_message(autostart_log, "Starting program.");
            kbdbuf_feed(AutostartRunCommand);
        }
        autostart_done();
        break;
      case NO:
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        /* leave autostart and disable warp if ROM area was left */
        check_rom_area();
        break;
    }
}

static void advance_hasdisk(void)
{
    char *tmp;
    int traps;

    switch (check("READY.", AUTOSTART_WAIT_BLINK)) {
      case YES:
        if (autostart_program_name)
            log_message(autostart_log, "Loading program '%s'",
                        autostart_program_name);
        else
            log_message(autostart_log, "Loading program '*'");
        orig_drive_true_emulation_state = get_true_drive_emulation_state();
        if (handle_drive_true_emulation_overridden) {
            resources_get_int("VirtualDevices", &traps);
            if (traps) {
                if (orig_drive_true_emulation_state)
                    log_message(autostart_log,
                                "Turning true drive emulation off.");
                set_true_drive_emulation_mode(0);
            } else {
                if (!orig_drive_true_emulation_state)
                    log_message(autostart_log,
                                "Turning true drive emulation on.");
                set_true_drive_emulation_mode(1);
            }
        } else {
            if (!orig_drive_true_emulation_state) {
                traps = 1;
            } else {
                traps = 0;
            }
        }
        tmp = lib_msprintf("LOAD\"%s\",8%s:\r", 
            autostart_program_name ?
              autostart_program_name : "*",
            autostart_basic_load ? "" : ",1");
        kbdbuf_feed(tmp);
        lib_free(tmp);

        if (!traps) {
            if (AutostartWarp) {
                autostartmode = AUTOSTART_WAITSEARCHINGFOR;
            } else {
                /* be most compatible if warp is disabled */
                if (autostart_run_mode == AUTOSTART_MODE_RUN) {
                    kbdbuf_feed(AutostartRunCommand);
                }
                autostart_done();
            }
        } else {
            autostartmode = AUTOSTART_LOADINGDISK;
            machine_bus_attention_callback_set(disk_attention_callback);
        }

        deallocate_program_name();
        break;
      case NO:
        orig_drive_true_emulation_state = get_true_drive_emulation_state();
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        break;
    }
}

static void advance_hassnapshot(void)
{
    switch (check("READY.", AUTOSTART_WAIT_BLINK)) {
      case YES:
        autostart_done();
        log_message(autostart_log, "Restoring snapshot.");
        interrupt_maincpu_trigger_trap(load_snapshot_trap, 0);
        break;
      case NO:
        autostart_disable();
        break;
      case NOT_YET:
        break;
    }
}

/* ----- stages for tde disk loading with warp --------------------------- */

static void advance_waitsearchingfor(void)
{
    switch (check("SEARCHING FOR", AUTOSTART_NOWAIT_BLINK)) {
      case YES:
        log_message(autostart_log, "Searching for ...");
        autostartmode = AUTOSTART_WAITLOADING;
        break;
      case NO:
        log_message(autostart_log, "NO Searching for ...");
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        break;
    }
}

static void advance_waitloading(void)
{
    switch (check("LOADING", AUTOSTART_NOWAIT_BLINK)) {
      case YES:
        log_message(autostart_log, "Loading");
        entered_rom = 0;
        autostartmode = AUTOSTART_WAITLOADREADY;
        break;
      case NO:
        /* still showing SEARCHING FOR ? */
        if (check("SEARCHING FOR", AUTOSTART_NOWAIT_BLINK)==YES) {
            return;
        }
        /* no something else is shown -> error! */
        log_message(autostart_log, "NO Loading");
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        break;
    }
}

static void advance_waitloadready(void)
{    
    switch (check("READY.", AUTOSTART_WAIT_BLINK)) {
      case YES:
        log_message(autostart_log, "Ready");
        disable_warp_if_was_requested();

        if (autostart_run_mode == AUTOSTART_MODE_RUN) {
            kbdbuf_feed(AutostartRunCommand);
            log_message(autostart_log, "Running program");
        }
        autostart_done();
        break;
      case NO:
        log_message(autostart_log, "NO Ready");
        disable_warp_if_was_requested();
        autostart_disable();
        break;
      case NOT_YET:
        /* leave autostart and disable warp if ROM area was left */
        check_rom_area();
        break;
    }
}

/* After a reset a PRG file has to be injected into RAM */
static void advance_inject(void)
{
    if (autostart_prg_perform_injection(autostart_log) < 0) {
        disable_warp_if_was_requested();
        autostart_disable();        
    } else {
        /* wait for ready cursor and type RUN */
        autostartmode = AUTOSTART_WAITLOADREADY;
    }
}

/* Execute the actions for the current `autostartmode', advancing to the next
   mode if necessary.  */
void autostart_advance(void)
{
    if (!autostart_enabled) {
        return;
    }

    if ( orig_drive_true_emulation_state == -1) {
        orig_drive_true_emulation_state = get_true_drive_emulation_state();
    }

    if (maincpu_clk < autostart_initial_delay_cycles) {
        autostart_wait_for_reset = 0;
        return;
    }

    if (autostart_wait_for_reset) {
        return;
    }

    switch (autostartmode) {
      case AUTOSTART_HASTAPE:
        advance_hastape();
        break;
      case AUTOSTART_PRESSPLAYONTAPE:
        advance_pressplayontape();
        break;
      case AUTOSTART_LOADINGTAPE:
        advance_loadingtape();
        break;
      case AUTOSTART_HASDISK:
        advance_hasdisk();
        break;
      case AUTOSTART_HASSNAPSHOT:
        advance_hassnapshot();
        break;
      case AUTOSTART_WAITLOADREADY:
        advance_waitloadready();
        break;
      case AUTOSTART_WAITLOADING:
        advance_waitloading();
        break;
      case AUTOSTART_WAITSEARCHINGFOR:
        advance_waitsearchingfor();
        break;
      case AUTOSTART_INJECT:
        advance_inject();
        break;
      default:
        return;
    }

    if (autostartmode == AUTOSTART_ERROR && handle_drive_true_emulation_overridden) {
        log_message(autostart_log, "Now turning true drive emulation %s.",
                    orig_drive_true_emulation_state ? "on" : "off");
        set_true_drive_emulation_mode(orig_drive_true_emulation_state);
    }
}

/* Clean memory and reboot for autostart.  */
static void reboot_for_autostart(const char *program_name, unsigned int mode,
                                 unsigned int runmode)
{
    int rnd;

    if (!autostart_enabled) {
        return;
    }

    log_message(autostart_log, "Resetting the machine to autostart '%s'",
                program_name ? program_name : "*");

    mem_powerup();

    autostart_ignore_reset = 1;
    deallocate_program_name();
    if (program_name && program_name[0]) {
        autostart_program_name = lib_stralloc(program_name);
    }

    autostart_initial_delay_cycles = min_cycles;
    resources_get_int("AutostartDelayRandom", &rnd);
    if (rnd) {
        autostart_initial_delay_cycles += AUTOSTART_RAND();
    }
    DBG(("autostart_initial_delay_cycles: %d", autostart_initial_delay_cycles));

    machine_trigger_reset(MACHINE_RESET_MODE_SOFT);

    /* The autostartmode must be set AFTER the shutdown to make the autostart
       threadsafe for OS/2 */
    autostartmode = mode;
    autostart_run_mode = runmode;
    autostart_wait_for_reset = 1;

    /* enable warp before reset */
    if (mode != AUTOSTART_HASSNAPSHOT) {
        enable_warp_if_requested();
    }
}

/* ------------------------------------------------------------------------- */

/* Autostart snapshot file `file_name'.  */
int autostart_snapshot(const char *file_name, const char *program_name)
{
    BYTE vmajor, vminor;
    snapshot_t *snap;

    if (network_connected() || event_record_active() || event_playback_active()
        || file_name == NULL || !autostart_enabled)
        return -1;

    deallocate_program_name();  /* not needed at all */

    if (!(snap = snapshot_open(file_name, &vmajor, &vminor, machine_get_name())) ) {
        autostartmode = AUTOSTART_ERROR;
        return -1;
    }

    log_message(autostart_log, "Loading snapshot file `%s'.", file_name);
    snapshot_close(snap);

    /*autostart_program_name = lib_stralloc(file_name);
    interrupt_maincpu_trigger_trap(load_snapshot_trap, 0);*/
    /* use for snapshot */
    reboot_for_autostart(file_name, AUTOSTART_HASSNAPSHOT, AUTOSTART_MODE_RUN);

    return 0;
}

/* Autostart tape image `file_name'.  */
int autostart_tape(const char *file_name, const char *program_name,
                   unsigned int program_number, unsigned int runmode)
{
    BYTE do_seek = 1;

    if (network_connected() || event_record_active() || event_playback_active()
        || !file_name || !autostart_enabled)
        return -1;

    if (!(tape_image_attach(1, file_name) < 0)) {
        log_message(autostart_log,
                    "Attached file `%s' as a tape image.", file_name);
        if (!tape_tap_attached()) {
            if (program_number == 0 || program_number == 1)
                do_seek = 0;
             program_number -= 1;
        }
        if (do_seek) {
            if (program_number > 0) {
                /* program numbers in tape_seek_to_file() start at 0 */
                tape_seek_to_file(tape_image_dev1, program_number-1);
            } else {
                tape_seek_start(tape_image_dev1);
            }
        }
        resources_set_int("VirtualDevices", 1); /* Kludge: iAN CooG - for t64 images we need devtraps ON */
        reboot_for_autostart(program_name, AUTOSTART_HASTAPE, runmode);

        return 0;
    }

    autostartmode = AUTOSTART_ERROR;
    deallocate_program_name();

    return -1;
}

/* Cope with 0xa0 padded file names.  */
static void autostart_disk_cook_name(char **name)
{
    unsigned int pos;

    pos = 0;

    while ((*name)[pos] != '\0') {
        if (((unsigned char)((*name)[pos])) == 0xa0) {
            char *ptr;

            ptr = lib_malloc(pos + 1);
            memcpy(ptr, *name, pos);
            ptr[pos] = '\0';
            lib_free(*name);
            *name = ptr;
            break;
        }
        pos++;
    }
}

/* Autostart disk image `file_name'.  */
int autostart_disk(const char *file_name, const char *program_name,
                   unsigned int program_number, unsigned int runmode)
{
    char *name = NULL;

    if (network_connected() || event_record_active() || event_playback_active()
        || !file_name || !autostart_enabled)
        return -1;

    /* Get program name first to avoid more than one file handle open on
       image.  */
    if (!program_name && program_number > 0)
        name = image_contents_filename_by_number(diskcontents_filesystem_read(file_name), program_number);
    else
        name = lib_stralloc(program_name ? program_name : "*");

    if (name) {
        autostart_disk_cook_name(&name);
        if (!(file_system_attach_disk(8, file_name) < 0)) {
            log_message(autostart_log,
                        "Attached file `%s' as a disk image.", file_name);
            reboot_for_autostart(name, AUTOSTART_HASDISK, runmode);
            lib_free(name);

            return 0;
        }
    }

    autostartmode = AUTOSTART_ERROR;
    deallocate_program_name();
    lib_free(name);

    return -1;
}

/* Autostart PRG file `file_name'.  The PRG file can either be a raw CBM file
   or a P00 file */
int autostart_prg(const char *file_name, unsigned int runmode)
{
    fileio_info_t *finfo;
    int result;
    const char *boot_file_name;
    int mode;

    if (network_connected() || event_record_active() || event_playback_active()) {
        return -1;
    }

    /* open prg file */
    finfo = fileio_open(file_name, NULL, FILEIO_FORMAT_RAW | FILEIO_FORMAT_P00,
                        FILEIO_COMMAND_READ | FILEIO_COMMAND_FSNAME,
                        FILEIO_TYPE_PRG);

    /* can't open file */
    if (finfo == NULL) {
        log_error(autostart_log, "Cannot open `%s'.", file_name);
        return -1;
    }

    /* determine how to load file */
    switch(AutostartPrgMode) {
    case AUTOSTART_PRG_MODE_VFS:
        log_message(autostart_log, "Loading PRG file `%s' with virtual FS on unit #8.", file_name);
        result = autostart_prg_with_virtual_fs(file_name, finfo, autostart_log);
        mode = AUTOSTART_HASDISK;
        boot_file_name = (const char *)finfo->name;
        break;
    case AUTOSTART_PRG_MODE_INJECT:
        log_message(autostart_log, "Loading PRG file `%s' with direct RAM injection.", file_name);
        result = autostart_prg_with_ram_injection(file_name, finfo, autostart_log);
        mode = AUTOSTART_INJECT;
        boot_file_name = NULL;
        break;
    case AUTOSTART_PRG_MODE_DISK:
        log_message(autostart_log, "Loading PRG file `%s' with autostart disk image.", file_name);
        result = autostart_prg_with_disk_image(file_name, finfo, autostart_log, AutostartPrgDiskImage);
        mode = AUTOSTART_HASDISK;
        boot_file_name = "*";
        break;
    default:
        log_error(autostart_log, "Invalid PRG autostart mode: %d", AutostartPrgMode);
        result = -1;
        break;
    }

    /* Now either proceed with disk image booting or prg injection after reset */
    if (result >= 0) {
        ui_update_menus();
        reboot_for_autostart(boot_file_name, mode, runmode);
    }

    /* close prg file */
    fileio_close(finfo);

    return result;
}

/* ------------------------------------------------------------------------- */

int autostart_autodetect_opt_prgname(const char *file_prog_name, 
                                     unsigned int alt_prg_number,
                                     unsigned int runmode)
{
    char *tmp;
    int result;

    /* Check for image:prg -format.  */
    tmp = strrchr(file_prog_name, ':');
    if (tmp) {
        char *autostart_prg_name;
        char *autostart_file;

        autostart_file = lib_stralloc(file_prog_name);
        autostart_prg_name = strrchr(autostart_file, ':');
        *autostart_prg_name++ = '\0';
        /* Does the image exist?  */
        if (util_file_exists(autostart_file)) {
            char *name;

            charset_petconvstring((BYTE *)autostart_prg_name, 0);
            name = charset_replace_hexcodes(autostart_prg_name);
            result = autostart_autodetect(autostart_file, name, 0,
                                          runmode);
            lib_free(name);
        } else {
            result = autostart_autodetect(file_prog_name, NULL, alt_prg_number,
                                          runmode);
        }
        lib_free(autostart_file);
    } else {
        result = autostart_autodetect(file_prog_name, NULL, alt_prg_number,
                                      runmode);
    }
    return result;
}

/* Autostart `file_name', trying to auto-detect its type.  */
int autostart_autodetect(const char *file_name, const char *program_name,
                         unsigned int program_number, unsigned int runmode)
{
    if (network_connected() || event_record_active() || event_playback_active()
        || file_name == NULL)
        return -1;

    if (!autostart_enabled) {
        log_error(autostart_log,
                  "Autostart is not available on this setup.");
        return -1;
    }

    log_message(autostart_log, "Autodetecting image type of `%s'.", file_name);

    if (autostart_disk(file_name, program_name, program_number, runmode) == 0) {
        log_message(autostart_log, "`%s' recognized as disk image.", file_name);
        return 0;
    }

    if (machine_class != VICE_MACHINE_C64DTV)
    {
        if (autostart_tape(file_name, program_name, program_number, runmode) == 0) {
            log_message(autostart_log, "`%s' recognized as tape image.", file_name);
            return 0;
        }
    }


    if (autostart_snapshot(file_name, program_name) == 0) {
        log_message(autostart_log, "`%s' recognized as snapshot image.",
                    file_name);
        return 0;
    }

    if (autostart_prg(file_name, runmode) == 0) {
        log_message(autostart_log, "`%s' recognized as program/p00 file.",
                    file_name);
        return 0;
    }

    log_error(autostart_log, "`%s' is not a valid file.", file_name);
    return -1;
}

/* Autostart the image attached to device `num'.  */
int autostart_device(int num)
{
    if (network_connected() || event_playback_active() || event_record_active()
        || !autostart_enabled)
        return -1;

    switch (num) {
      case 8:
        reboot_for_autostart(NULL, AUTOSTART_HASDISK, AUTOSTART_MODE_RUN);
        return 0;
      case 1:
        reboot_for_autostart(NULL, AUTOSTART_HASTAPE, AUTOSTART_MODE_RUN);
        return 0;
    }
    return -1;
}

int autostart_in_progress(void)
{
    return (autostartmode != AUTOSTART_NONE && autostartmode != AUTOSTART_DONE);
}

/* Disable autostart on reset.  */
void autostart_reset(void)
{
    int oldmode;

    if (!autostart_enabled)
        return;

    if (!autostart_ignore_reset
        && autostartmode != AUTOSTART_NONE
        && autostartmode != AUTOSTART_ERROR) {
        oldmode = autostartmode;
        autostartmode = AUTOSTART_NONE;
        if (oldmode != AUTOSTART_DONE) {
            disk_eof_callback();
        }
        autostartmode = AUTOSTART_NONE;
        trigger_monitor = 0;
        deallocate_program_name();
        log_message(autostart_log, "Turned off.");
    }
    autostart_ignore_reset = 0;
}

void autostart_shutdown(void)
{
    deallocate_program_name();

    orig_drive_true_emulation_state = -1;
    orig_warp_mode = -1;
    autostart_enabled = 0;
    entered_rom = 0;
    autostart_ignore_reset = 0;
    
    log_close(autostart_log);
    autostart_log = LOG_ERR;
    
    autostart_prg_shutdown();
}

