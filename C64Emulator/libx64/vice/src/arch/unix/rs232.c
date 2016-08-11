/*
 * rs232.c - RS232 emulation.
 *
 * Written by
 *  Andr� Fachat <a.fachat@physik.tu-chemnitz.de>
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

/*
 * The RS232 emulation captures the bytes sent to the RS232 interfaces
 * available (currently ACIA 6551, std C64 and Daniel Dallmanns fast RS232
 * with 9600 Baud).
 * The characters captured are sent to a file or an attached process.
 * Characters sent from a process are sent back to the
 * chip emulations.
 *
 */

#undef DEBUG

#include "vice.h"

#ifdef HAVE_RS232

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef VMS
#include <termios.h>
#else
#include "vmstermios.h"
#endif

#include <unistd.h>

#if defined(MINIX_SUPPORT) || defined(OPENSERVER6_COMPILE)
#include <sys/select.h>
#endif

#if defined(__QNX__) && !defined(__QNXNTO__)
#include <sys/select.h>
#endif

#if defined(OPENSTEP_COMPILE) || defined(NEXTSTEP_COMPILE)
#define ssize_t int
#endif

#include "cmdline.h"
#include "coproc.h"
#include "log.h"
#include "resources.h"
#include "rs232.h"
#include "translate.h"
#include "types.h"

#if defined(NEXTSTEP_COMPILE) || defined(OPENSTEP_COMPILE)
int cfsetispeed(struct termios *t, int speed)
{ 
    t->c_ispeed = speed; 
    return 0; 
}

int cfsetospeed(struct termios *t, int speed)
{
    t->c_ispeed = speed;
    return 0;
}

int tcgetattr(int fildes, struct termios *tp)
{
    return ioctl(fildes, TIOCGETA, tp);
}

int tcsetattr(int fd, int opt, const struct termios *t)
{
    int st;

    switch(opt) {
        case TCSANOW:
            st = ioctl(fd, TIOCSETA, t);
            break;
        case TCSADRAIN:
            st = ioctl(fd, TIOCSETAW, t);
            break;
        case TCSAFLUSH:
            st = ioctl(fd, TIOCSETAF, t);
            break;
        default:
            st = -1;
            errno = EINVAL;
            break;
    }
    return st;
}
#endif

/* ------------------------------------------------------------------------- */

/* resource handling */

static int devbaud[RS232_NUM_DEVICES];

static int set_devbaud(int val, void *param)
{
    devbaud[vice_ptr_to_int(param)] = val;
    return 0;
}

/* ------------------------------------------------------------------------- */

static const resource_int_t resources_int[] = {
    { "RsDevice1Baud", 9600, RES_EVENT_NO, NULL,
      &devbaud[0], set_devbaud, (void *)0 },
    { "RsDevice2Baud", 9600, RES_EVENT_NO, NULL,
      &devbaud[1], set_devbaud, (void *)1 },
    { "RsDevice3Baud", 9600, RES_EVENT_NO, NULL,
      &devbaud[2], set_devbaud, (void *)2 },
    { "RsDevice4Baud", 9600, RES_EVENT_NO, NULL,
      &devbaud[3], set_devbaud, (void *)3 },
    { NULL }
};

int rs232_resources_init(void)
{
    return resources_register_int(resources_int);
}

void rs232_resources_shutdown(void)
{
}

static const cmdline_option_t cmdline_options[] = {
    { "-rsdev1baud", SET_RESOURCE, -1,
      NULL, NULL, "RsDevice1Baud", NULL,
      USE_PARAM_STRING, USE_DESCRIPTION_STRING,
      IDCLS_UNUSED, IDCLS_UNUSED,
      N_("baudrate"), N_("Specify baudrate of first RS232 device") },
    { "-rsdev2baud", SET_RESOURCE, -1,
      NULL, NULL, "RsDevice2Baud", NULL,
      USE_PARAM_STRING, USE_DESCRIPTION_STRING,
      IDCLS_UNUSED, IDCLS_UNUSED,
      N_("baudrate"), N_("Specify baudrate of second RS232 device") },
    { "-rsdev3baud", SET_RESOURCE, -1,
      NULL, NULL, "RsDevice3Baud", NULL,
      USE_PARAM_STRING, USE_DESCRIPTION_STRING,
      IDCLS_UNUSED, IDCLS_UNUSED,
      N_("baudrate"), N_("Specify baudrate of third RS232 device") },
    { "-rsdev4baud", SET_RESOURCE, -1,
      NULL, NULL, "RsDevice4Baud", NULL,
      USE_PARAM_STRING, USE_DESCRIPTION_STRING,
      IDCLS_UNUSED, IDCLS_UNUSED,
      N_("baudrate"), N_("Specify baudrate of 4th RS232 device") },
    { NULL }
};

int rs232_cmdline_options_init(void)
{
    return cmdline_register_options(cmdline_options);
}

/* ------------------------------------------------------------------------- */

typedef struct rs232 {
    int inuse;
    int type;
    int fd_r;
    int fd_w;
    char *file;
    struct termios saved;
} rs232_t;

#define T_FILE 0
#define T_TTY  1
#define T_PROC 2

static rs232_t fds[RS232_NUM_DEVICES];

static log_t rs232_log = LOG_ERR;

/* ------------------------------------------------------------------------- */

void rs232_close(int fd);

/* initializes all RS232 stuff */
void rs232_init(void)
{
    int i;

    for (i = 0; i < RS232_NUM_DEVICES; i++) {
        fds[i].inuse = 0;
    }

    rs232_log = log_open("RS232");
}

/* resets terminal to old mode */
static void unset_tty(int i)
{
    tcsetattr(fds[i].fd_r, TCSAFLUSH, &fds[i].saved);
}

static struct {
    int baud;
    speed_t speed;
} speed_tab[] = {
    { 300, B300 },
    { 600, B600 },
    { 1200, B1200 },
    { 1800, B1800 },
    { 2400, B2400 },
    { 4800, B4800 },
    { 9600, B9600 },
    { 19200, B19200 },
    { 38400, B38400 },
    { 0, B9600 }                                /* fallback */
};

/* sets terminal to raw mode */
static void set_tty(int i, int baud)
{
    /*
     * set tty to raw mode as of
     * "Advanced Programming in the Unix Environment"
     * by W.R. Stevens, Addison-Wesley.
     */
    speed_t speed;
    int fd = fds[i].fd_r;
    struct termios buf;

    if (tcgetattr(fd, &fds[i].saved) < 0) {
        return /* -1 */ ;
    }
    buf = fds[i].saved;

    buf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* echho off, cononical mode off, extended input processing
     * off, signal chars off */
    buf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* no SIGINT on Break, CR-to-NL off, input parity check off,
     * don't strip 8th bit on input, output flow control off */
    buf.c_cflag &= ~(CSIZE | PARENB);

    /* clear size bits, parity checking off */
    buf.c_cflag |= CS8;

    /* set 8 bits/char */
    buf.c_oflag &= ~(OPOST);

    /* ouput processing off */
    buf.c_cc[VMIN] = 1;         /* 1 byte at a time, no timer */
    buf.c_cc[VTIME] = 0;

    for (i = 0; speed_tab[i].baud; i++) {
        if (speed_tab[i].baud >= baud) {
            break;
        }
    }
    speed = speed_tab[i].speed;

    cfsetispeed(&buf, speed);
    cfsetospeed(&buf, speed);

    tcsetattr(fd, TCSAFLUSH, &buf);
}

/* reset RS232 stuff */
void rs232_reset(void)
{
    int i;

    for (i = 0; i < RS232_NUM_DEVICES; i++) {
        if (fds[i].inuse) {
            rs232_close(i);
        }
    }
}

/* opens a rs232 window, returns handle to give to functions below. */
int rs232_open(int device)
{
    int i, fd;

    for (i = 0; i < RS232_NUM_DEVICES; i++) {
        if (!fds[i].inuse) {
            break;
        }
    }
    if (i >= RS232_NUM_DEVICES) {
        log_error(rs232_log, "No more devices available.");
        return -1;
    }

#ifdef DEBUG
    log_message(rs232_log, "rs232_open(device=%d).", device);
#endif

    if (rs232_devfile[device][0] == '|') {
#if defined(MINIX_SUPPORT) || defined(OPENSTEP_COMPILE) || defined(RHAPSODY_COMPILE) || defined(NEXTSTEP_COMPILE)
        log_error(rs232_log, "Forking not supported on this platform.");
        return -1;
#else
        if (fork_coproc(&fds[i].fd_w, &fds[i].fd_r, rs232_devfile[device] + 1) < 0) {
            log_error(rs232_log, "Cannot fork process.");
            return -1;
        }
#endif
        fds[i].type = T_PROC;
        fds[i].inuse = 1;
        fds[i].file = rs232_devfile[device];
    } else {
#if !defined(OPENSTEP_COMPILE) && !defined(NEXTSTEP_COMPILE)
        fd = open(rs232_devfile[device], O_RDWR | O_NOCTTY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
            log_error(rs232_log, "Cannot open file \"%s\": %s", rs232_devfile[device], strerror(errno));
            return -1;
        }
        fds[i].fd_r = fds[i].fd_w = fd;
        fds[i].file = rs232_devfile[device];

        if (isatty(fd)) {
            fds[i].type = T_TTY;
            set_tty(i, devbaud[device]);
        } else {
            fds[i].type = T_FILE;
        }
        fds[i].inuse = 1;
#endif
    }

    return i;
}

/* closes the rs232 window again */
void rs232_close(int fd)
{
#ifdef DEBUG
    log_debug(rs232_log, "close(fd=%d).", fd);
#endif

    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        log_error(rs232_log, "Attempt to close invalid fd %d.", fd);
        return;
    }
    if (!fds[fd].inuse) {
        log_error(rs232_log, "Attempt to close non-open fd %d.", fd);
        return;
    }

    if (fds[fd].type == T_TTY) {
        unset_tty(fd);
    }
    close(fds[fd].fd_r);
    if ((fds[fd].type == T_PROC) && (fds[fd].fd_r != fds[fd].fd_w)) {
        close(fds[fd].fd_w);
    }
    fds[fd].inuse = 0;
}

/* sends a byte to the RS232 line */
int rs232_putc(int fd, BYTE b)
{
    ssize_t n;

    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        log_error(rs232_log, "Attempt to write to invalid fd %d.", fd);
        return -1;
    }
    if (!fds[fd].inuse) {
        log_error(rs232_log, "Attempt to write to non-open fd %d.", fd);
        return -1;
    }

    /* for the beginning... */
#ifdef DEBUG
    log_message(rs232_log, "Output `%c'.", b);
#endif

    do {
        n = write(fds[fd].fd_w, &b, 1);
        if (n < 0) {
            log_error(rs232_log, "Error writing: %s.", strerror(errno));
            return -1;
        }
    } while (n != 1);

    return 0;
}

/* gets a byte to the RS232 line, returns !=0 if byte received, byte in *b. */
int rs232_getc(int fd, BYTE * b)
{
    int ret;
    size_t n;
    fd_set rdset;
    struct timeval ti;

    if (fd < 0 || fd >= RS232_NUM_DEVICES) {
        log_error(rs232_log, "Attempt to read from invalid fd %d.", fd);
        return -1;
    }
    if (!fds[fd].inuse) {
        log_error(rs232_log, "Attempt to read from non-open fd %d.", fd);
        return -1;
    }

    if (fds[fd].type == T_FILE) {
        return 0;
    }

    FD_ZERO(&rdset);
    FD_SET(fds[fd].fd_r, &rdset);
    ti.tv_sec = ti.tv_usec = 0;

#ifndef MINIXVMD
    /* for now this change will break rs232 support on Minix-vmd
       till I can implement the same functionality using the
       poll() function */

    ret = select(fds[fd].fd_r + 1, &rdset, NULL, NULL, &ti);
#endif

    if (ret && (FD_ISSET(fds[fd].fd_r, &rdset))) {
        n = read(fds[fd].fd_r, b, 1);
        if (n) {
            return 1;
        }
    }
    return 0;
}

/* set the status lines of the RS232 device */
int rs232_set_status(int fd, enum rs232handshake_out status)
{
    return -1;
}

/* get the status lines of the RS232 device */
enum rs232handshake_in rs232_get_status(int fd)
{
    /*! \todo dummy */
    return RS232_HSI_CTS | RS232_HSI_DSR;
}

/* set the bps rate of the physical device */
void rs232_set_bps(int fd, unsigned int bps)
{
}
#endif
