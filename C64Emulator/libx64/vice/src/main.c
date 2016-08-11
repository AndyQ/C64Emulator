/*
 * main.c - VICE main startup entry.
 *
 * Written by
 *  Ettore Perazzoli <ettore@comm2000.it>
 *  Teemu Rantanen <tvr@cs.hut.fi>
 *  Vesa-Matti Puro <vmp@lut.fi>
 *  Jarkko Sonninen <sonninen@lut.fi>
 *  Jouko Valta <jopi@stekt.oulu.fi>
 *  Andr� Fachat <a.fachat@physik.tu-chemnitz.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ENABLE_NLS
#include <locale.h>
#endif

#include "archdep.h"
#include "cmdline.h"
#include "console.h"
#include "debug.h"
#include "drive.h"
#include "fullscreen.h"
#include "gfxoutput.h"
#include "init.h"
#include "initcmdline.h"
#ifdef HAS_TRANSLATION
#include "intl.h"
#endif
#include "lib.h"
#include "log.h"
#include "machine.h"
#include "maincpu.h"
#include "main.h"
#include "platform.h"
#include "resources.h"
#include "sysfile.h"
#ifdef HAS_TRANSLATION
#include "translate.h"
#endif
#include "types.h"
#include "uiapi.h"
#include "version.h"
#include "video.h"


int vsid_mode = 0; /* FIXME: remove this if all ports are updated */
#ifdef __OS2__
const
#endif
int console_mode = 0;
int video_disabled_mode = 0;
static int init_done;

/* ------------------------------------------------------------------------- */

/* This is the main program entry point.  Call this from `main()'.  */
int main_program(int argc, char **argv)
{
    int i;
    char *program_name;

    /* Check for -config and -console before initializing the user interface.
       -config  => use specified configuration file
       -console => no user interface
    */
    for (i = 0; i < argc; i++) {
#ifndef __OS2__
        if (strcmp(argv[i], "-console") == 0) {
            console_mode = 1;
            video_disabled_mode = 1;
        } else
#endif
        if (strcmp(argv[i], "-config") == 0) {
            if ((i+1) < argc) {
                vice_config_file = lib_stralloc(argv[++i]);
            }
        }
    }

#ifdef ENABLE_NLS
    /* gettext stuff, not needed in Gnome, but here I can
       overrule the default locale path */
    setlocale(LC_ALL, "");
    bindtextdomain(PACKAGE, NLS_LOCALEDIR);
    textdomain(PACKAGE);
#endif

    archdep_init(&argc, argv);

//    if (atexit(main_exit) < 0) {
//        archdep_startup_log_error("atexit");
//        return -1;
//    }

    maincpu_early_init();
    machine_setup_context();
    drive_setup_context();
    machine_early_init();

    /* Initialize system file locator.  */
    sysfile_init(machine_name);

    gfxoutput_early_init();

    if (init_resources() < 0 || init_cmdline_options() < 0) {
        return -1;
    }

    /* Set factory defaults.  */
    if (resources_set_defaults() < 0) {
        archdep_startup_log_error("Cannot set defaults.\n");
        return -1;
    }

    /* Initialize the user interface.  `ui_init()' might need to handle the
       command line somehow, so we call it before parsing the options.
       (e.g. under X11, the `-display' option is handled independently).  */
    if (!console_mode && ui_init(&argc, argv) < 0) {
        archdep_startup_log_error("Cannot initialize the UI.\n");
        return -1;
    }

#ifdef HAS_TRANSLATION
   /* set the default arch language */
    translate_arch_language_init();
#endif

    /* Load the user's default configuration file.  */
    if (resources_load(NULL) < 0) {
        /* The resource file might contain errors, and thus certain
           resources might have been initialized anyway.  */
        if (resources_set_defaults() < 0) {
            archdep_startup_log_error("Cannot set defaults.\n");
            return -1;
        }
    }

    if (log_init() < 0) {
        archdep_startup_log_error("Cannot startup logging system.\n");
    }

    if (initcmdline_check_args(argc, argv) < 0) {
        return -1;
    }

    program_name = archdep_program_name();

    /* VICE boot sequence.  */
    log_message(LOG_DEFAULT, "*** VICE Version %s ***", VERSION);
    log_message(LOG_DEFAULT, "OS compiled for: %s", platform_get_compile_time_os());
    log_message(LOG_DEFAULT, "GUI compiled for: %s", platform_get_ui());
    log_message(LOG_DEFAULT, "CPU compiled for: %s", platform_get_compile_time_cpu());
    log_message(LOG_DEFAULT, "Compiler used: %s", platform_get_compile_time_compiler());
    log_message(LOG_DEFAULT, "Current OS: %s", platform_get_runtime_os());
    log_message(LOG_DEFAULT, "Current CPU: %s", platform_get_runtime_cpu());
    log_message(LOG_DEFAULT, " ");
    log_message(LOG_DEFAULT, "Welcome to %s, the free portable %s Emulator.",
                program_name, machine_name);
    log_message(LOG_DEFAULT, " ");
    log_message(LOG_DEFAULT, "Current VICE team members:");
    log_message(LOG_DEFAULT, "D. Lem, A. Matthies, M. Pottendorfer, S. Trikaliotis, M. van den Heuvel,");
    log_message(LOG_DEFAULT, "C. Vogelgsang, F. Gennari, D. Kahlin, A. Lankila, Groepaz, I. Korb,");
    log_message(LOG_DEFAULT, "E. Smith, O. Seibert, M. Sutton, U. Schulz, S. Haubenthal, T. Giesel,");
    log_message(LOG_DEFAULT, "K. Zsolt.");

    log_message(LOG_DEFAULT, " ");
    log_message(LOG_DEFAULT, "This is free software with ABSOLUTELY NO WARRANTY.");
    log_message(LOG_DEFAULT, "See the \"About VICE\" command for more info.");
    log_message(LOG_DEFAULT, " ");

    if (program_name != NULL)
    {
        lib_free(program_name);
        program_name = NULL;
    }

    /* Complete the GUI initialization (after loading the resources and
       parsing the command-line) if necessary.  */
    if (!console_mode && ui_init_finish() < 0) {
        return -1;
    }

    if (!console_mode && video_init() < 0) {
        return -1;
    }

    if (initcmdline_check_psid() < 0) {
        return -1;
    }

    if (init_main() < 0) {
        return -1;
    }

    initcmdline_check_attach();

    init_done = 1;

    /* Let's go...  */
    log_message(LOG_DEFAULT, "Main CPU: starting at ($FFFC).");
    maincpu_mainloop();

    //log_error(LOG_DEFAULT, "perkele!");

    return 0;
}

