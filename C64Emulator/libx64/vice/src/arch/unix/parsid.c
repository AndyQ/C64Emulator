/*
 * parsid.c - PARallel port SID support for UNIX.
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

#ifdef HAVE_PARSID
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/ioctl.h>

#ifdef HAVE_IEEE1284_H
#undef HAVE_IEEE1284_H
#include <ieee1284.h>
#endif

#if defined(HAVE_SYS_MMAN_H) && defined(HAVE_HW_INOUT_H)
#include <sys/mman.h>
#include <hw/inout.h>
#endif

#ifdef HAVE_MACHINE_SYSARCH_H
#include <machine/sysarch.h>
#endif

#ifdef HAVE_SYS_IO_H
#include <sys/io.h>
#endif

#ifdef HAVE_MACHINE_PIO_H
#include <machine/pio.h>
#endif

#ifdef HAVE_MACHINE_CPUFUNC_H
#include <machine/cpufunc.h>
#endif

#include "alarm.h"
#include "parsid.h"
#include "log.h"
#include "sid-resources.h"
#include "types.h"

/* control register bits */
#define parsid_STROBE   0x01
#define parsid_AUTOFEED 0x02
#define parsid_nINIT    0x04
#define parsid_SELECTIN 0x08
#define parsid_PCD      0x20

static unsigned char parsid_ctrport;

static int parsid_port_address[4];

static int ports;
static int old_parsid_port = 0;
static int parsid_open_status = 0;

#ifdef HAVE_LIBIEEE1284
    struct parport_list parlist;

void parsid_ieee1284_outb(struct parport_list *pl, int addr, BYTE value)
{
    if (addr == parsid_port_address[old_parsid_port]) {
        ieee1284_write_data(pl->portv[old_parsid_port - 1], value);
    } else {
        ieee1284_write_control(pl->portv[old_parsid_port - 1], value);
    }
}

int parsid_ieee1284_inb(struct parport_list *pl, int addr)
{
    if (addr == parsid_port_address[old_parsid_port]) {
        return ieee1284_read_data(pl->portv[old_parsid_port - 1]);
    } else {
        return ieee1284_read_control(pl->portv[old_parsid_port - 1]);
    }
}

int parsid_ieee1284_open(struct parport_list *pl, int portnr)
{
    int ret_value;
    int cap;

    ret_value = ieee1284_open(pl->portv[portnr], F1284_EXCL, &cap);

    if (ret_value == E1284_OK) {
        ret_value = ieee1284_claim(pl->portv[portnr]);
        if (ret_value != E1284_OK) {
            ieee1284_close(pl->portv[portnr]);
        }
    }
    if (ret_value == E1284_OK && (cap & CAP1284_RAW) == CAP1284_RAW) {
        return 1;
    } else {
        return 0;
    }
}

void parsid_ieee1284_close(struct parport_list *pl, int portnr)
{
    ieee1284_release(pl->portv[portnr]);
    ieee1284_close(pl->portv[portnr]);
}
#endif

#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
static void setaccess(u_long * map, u_int bit, int allow)
{
    u_int word;
    u_int shift;
    u_long mask;

    word = bit / 32;
    shift = bit - (word * 32);

    mask = 0x000000001 << shift;
    if (allow) {
        map[word] &= ~mask;
    } else {
        map[word] |= mask;
    }
}
#endif
#if defined(__NetBSD__) && defined(HAVE_I386_SET_IOPERM)
static int vice_i386_set_ioperm(unsigned long *iomap)
{
    struct i386_set_ioperm_args arg;

    arg.iomap = iomap;
    return sysarch(I386_SET_IOPERM, &arg);
}

static int vice_i386_get_ioperm(unsigned long *iomap)
{
    struct i386_get_ioperm_args arg;

    arg.iomap = iomap;
    return sysarch(I386_GET_IOPERM, &arg);
}
#endif
#if defined(__NetBSD__) && defined(HAVE_LIBAMD64)
static int vice_amd64_set_ioperm(unsigned long *iomap)
{
    struct x86_64_set_ioperm_args arg;

    arg.iomap = iomap;
    return sysarch(X86_64_SET_IOPERM, &arg);
}

static int vice_amd64_get_ioperm(unsigned long *iomap)
{
    struct x86_64_get_ioperm_args arg;

    arg.iomap = iomap;
    return sysarch(X86_64_SET_IOPERM, &arg);
}
#endif
#endif

#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifdef __NetBSD__
static inline void vice_outb(WORD port, BYTE val)
{
    asm volatile("outb %0, %1"
                 : : "a"(val), "Nd"(port));
}

static inline BYTE vice_inb(WORD port)
{
    BYTE ret;

    asm volatile("inb %1, %0"
                 : "=a"(ret) : "Nd"(port));
    return ret;
}
#endif
#endif

void parsid_outb(int addr, BYTE value)
{
#ifdef HAVE_LIBIEEE1284
    parsid_ieee1284_outb(&parlist, addr, value);
#endif
#ifdef HAVE_MMAP_DEVICE_IO
    out8(addr, value);
#endif
#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifdef __NetBSD__
    vice_outb(addr, value);
#else
    outb(addr, value);
#endif
#endif
#ifdef HAVE_IOPERM
    outb_p(value, addr);
#endif
}

BYTE parsid_inb(int addr)
{
#ifdef HAVE_LIBIEEE1284
    return parsid_ieee1284_inb(&parlist, addr);
#endif
#ifdef HAVE_MMAP_DEVICE_IO
    return in8(addr);
#endif
#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifdef __NetBSD__
    return vice_inb((WORD)addr);
#else
    return inb((unsigned short)addr);
#endif
#endif
#ifdef HAVE_IOPERM
    return inb_p((unsigned short)addr);
#endif
}

/* chip control pin assignments */
static void parsid_chip_select(void)
{
    parsid_ctrport |= parsid_STROBE;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_chip_deselect(void)
{
    parsid_ctrport &= ~parsid_STROBE;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_reset_start(void)
{
    parsid_ctrport |= parsid_SELECTIN;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_reset_end(void)
{
    parsid_ctrport &= ~parsid_SELECTIN;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_latch_open(void)
{
    parsid_ctrport &= ~parsid_AUTOFEED;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_latch_lock(void)
{
    parsid_ctrport |= parsid_AUTOFEED;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_RW_write(void)
{
    parsid_ctrport &= ~parsid_nINIT;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_RW_read(void)
{
    parsid_ctrport |= parsid_nINIT;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

/* parallel port direction control */
static void parsid_port_write(void)
{
    parsid_ctrport &= ~parsid_PCD;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_port_read(void)
{
    parsid_ctrport |= parsid_PCD;
    parsid_outb(parsid_port_address[old_parsid_port] + 2, parsid_ctrport);
}

static void parsid_sidwait(void)
{
}

int parsid_check_port(int port)
{
    int real_port = 0;
    int count_port = 0;
#if defined(HAVE_MMAP_DEVICE_IO) || defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM) || defined(HAVE_IOPERM)
    int old_port_addr = 0;
    int port_addr = 0;
#endif
#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
    u_long iomap[32];
#endif
#endif

    if (port < 0 || port > 3) {
        return -1;
    }

    if (port > ports) {
        return -1;
    }

    while (count_port != port) {
        if (parsid_port_address[port - 1] != 0) {
            count_port++;
        }
        real_port++;
    }

#ifdef HAVE_LIBIEEE1284
    if (parsid_ieee1284_open(&parlist, real_port - 1) == 0) {
        return -1;
    } else {
        if (old_parsid_port != 0) {
            parsid_ieee1284_close(&parlist, old_parsid_port - 1);
        }
        old_parsid_port=real_port;
    }
#endif

#if defined(HAVE_MMAP_DEVICE_IO) || defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM) || defined(HAVE_IOPERM)
    if (real_port == 1) {
        port_addr = 0x3bc;
    }
    if (real_port == 2) {
        port_addr = 0x378;
    }
    if (real_port == 3) {
        port_addr = 0x278;
    }

    if (old_parsid_port == 1) {
        old_port_addr=0x3bc;
    }
    if (old_parsid_port == 2) {
        old_port_addr = 0x378;
    }
    if (old_parsid_port == 3) {
        old_port_addr = 0x278;
    }
#endif

#ifdef HAVE_MMAP_DEVICE_IO
    if (mmap_device_io(4, port_addr) == MAP_FAILED) {
        return -1;
    } else {
        if (old_parsid_port != 0) {
            mmunmap_device_io(4, old_port_addr);
        }
    }
    old_parsid_port = real_port;
#endif

#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
#ifdef HAVE_LIBAMD64
    if (vice_amd64_get_ioperm(iomap) != -1) {
#else
    if (vice_i386_get_ioperm(iomap) != -1) {
#endif
        setaccess(iomap, port_addr, 1);
        setaccess(iomap, port_addr + 2, 1);
#ifdef HAVE_LIBAMD64
        if (vice_amd64_set_ioperm(iomap) != -1) {
#else
        if (vice_i386_set_ioperm(iomap) != -1) {
#endif
            if (old_parsid_port != 0) {
                setaccess(iomap, old_port_addr, 0);
                setaccess(iomap, old_port_addr + 2, 0);
#ifdef HAVE_LIBAMD64
                vice_amd64_set_ioperm(iomap);
#else
                vice_i386_set_ioperm(iomap);
#endif
            }
        } else {
            return -1;
        }
    } else {
        return -1;
    }
    old_parsid_port = real_port;
#else
    if (vice_i386_set_ioperm(port_addr, 3, 1) != 0) {
        return -1;
    }
    if (old_parsid_port != 0) {
        vice_i386_set_ioperm(old_port_addr, 3, 0);
    }
    old_parsid_port = real_port;
#endif
#endif

#ifdef HAVE_IOPERM
    if (ioperm(0x80, 1, 1) != 0) {
        return -1;
    }
    if (ioperm(port_addr, 3, 1) != 0) {
        return -1;
    }
    if (old_parsid_port != 0) {
        ioperm(old_port_addr, 3, 0);
    }
    old_parsid_port = real_port;
#endif

    parsid_ctrport = parsid_inb(parsid_port_address[old_parsid_port] + 2);
    return 0;
}

static int parsid_init(void)
{
    int j;
#if defined(HAVE_MMAP_DEVICE_IO) || defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM) || defined(HAVE_IOPERM)
    int port_addr = 0;
#endif
#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
    u_long iomap[32];
#endif
#endif

    ports = 0;

#ifdef HAVE_LIBIEEE1284
    ieee1284_find_ports(&parlist, 0);
#endif

    for (j = 0; j < 3; j++) {
#ifdef HAVE_LIBIEEE1284
        parsid_port_address[j] = parsid_ieee1284_open(&parlist, j);
#endif

#if defined(HAVE_MMAP_DEVICE_IO) || defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM) || defined(HAVE_IOPERM)
        if (j == 0) {
            port_addr = 0x3bc;
        }
        if (j == 1) {
            port_addr = 0x378;
        }
        if (j == 2) {
            port_addr = 0x278;
        }
#endif

#ifdef HAVE_MMAP_DEVICE_IO
        parsid_port_address[j] = mmap_device_io(4, port_addr);
        if (parsid_port_address[j] != MAP_FAILED) {
            mmunmap_device_io(4, port_addr);
            parsid_port_address[j] = port_addr;
        } else {
            parsid_port_address[j] = 0;
        }
#endif

#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
#ifdef HAVE_LIBAMD64
        if (vice_amd64_get_ioperm(iomap) != -1)
#else
        if (vice_i386_get_ioperm(iomap) != -1)
#endif
        {
            setaccess(iomap, port_addr, 1);
            setaccess(iomap, port_addr + 2, 1);
#ifdef HAVE_LIBAMD64
            if (vice_amd64_set_ioperm(iomap) != -1)
#else
            if (vice_i386_set_ioperm(iomap) != -1)
#endif
            {
                parsid_port_address[j] = port_addr;
                setaccess(iomap, port_addr, 0);
                setaccess(iomap, port_addr + 2, 0);
#ifdef HAVE_LIBAMD64
                vice_amd64_set_ioperm(iomap);
#else
                vice_i386_set_ioperm(iomap);
#endif
            } else {
                parsid_port_address[j] = 0;
            }
        } else {
            parsid_port_address[j] = 0;
        }
#else
        parsid_port_address[j] = vice_i386_set_ioperm(port_addr, 3, 1);
        if (parsid_port_address[j] == 0) {
            vice_i386_set_ioperm(port_addr, 3, 0);
            parsid_port_address[j] = port_addr;
        } else {
            parsid_port_address[j] = 0;
        }
#endif
#endif

#ifdef HAVE_IOPERM
        parsid_port_address[j] = ioperm(port_addr, 3, 1);
        if (parsid_port_address[j] == 0) {
            ioperm(port_addr, 3, 0);
            parsid_port_address[j] = port_addr;
        } else {
            parsid_port_address[j] = 0;
        }
#endif
        if (parsid_port_address[j] != 0) {
            ports++;
        }
    }

    if (ports == 0) {
        return -1;
    }
    return 0;
}

void parsid_reset(void)
{
    if (parsid_open_status == 1) {
        parsid_RW_write();
        parsid_port_write();
        parsid_chip_select();
        parsid_latch_open();
        parsid_outb(parsid_port_address[old_parsid_port], 0);
        parsid_reset_start();
        sleep(1);
        parsid_reset_end();
        parsid_latch_lock();
        parsid_chip_deselect();
    }
}

int parsid_open(int port)
{
    if (parsid_open_status == 0) {
        if (parsid_init() < 0) {
            return -1;
        }
        if (parsid_check_port(port) < 0) {
            return -1;
        }
        parsid_reset();
        parsid_open_status = 1;
    }
    return 0;
}

int parsid_close(void)
{
#if defined(HAVE_MMAP_DEVICE_IO) || defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM) || defined(HAVE_IOPERM)
    int old_port_addr = 0;
#endif
#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
    u_long iomap[32];
#endif
#endif
    if (parsid_open_status == 1) {
        parsid_reset();
#ifdef HAVE_LIBIEEE1284
        if (old_parsid_port != 0) {
            parsid_ieee1284_close(&parlist, old_parsid_port - 1);
            old_parsid_port = 0;
        }
#endif

#if defined(HAVE_MMAP_DEVICE_IO) || defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM) || defined(HAVE_IOPERM)
        if (old_parsid_port == 1) {
            old_port_addr = 0x3bc;
        }
        if (old_parsid_port == 2) {
            old_port_addr = 0x378;
        }
        if (old_parsid_port == 3) {
            old_port_addr = 0x278;
        }
#endif

#ifdef HAVE_MMAP_DEVICE_IO
        if (old_parsid_port != 0) {
            mmunmap_device_io(4, old_port_addr);
            old_parsid_port = 0;
        }
#endif

#if defined(HAVE_LIBAMD64) || defined(HAVE_I386_SET_IOPERM)
#ifndef __FreeBSD__
#ifdef HAVE_LIBAMD64
        if (vice_amd64_get_ioperm(iomap) != -1)
#else
        if (vice_i386_get_ioperm(iomap) != -1)
#endif
        {
            if (old_parsid_port != 0) {
                setaccess(iomap, old_port_addr, 0);
                setaccess(iomap, old_port_addr + 2, 0);
#ifdef HAVE_LIBAMD64
                vice_amd64_set_ioperm(iomap);
#else
                vice_i386_set_ioperm(iomap);
#endif
            }
        }
        old_parsid_port = 0;
#else
        if (old_parsid_port != 0) {
            vice_i386_set_ioperm(old_port_addr, 3, 0);
            old_parsid_port = 0;
        }
#endif
#endif

#ifdef HAVE_IOPERM
        if (old_parsid_port != 0) {
            ioperm(0x80, 1, 0);
            ioperm(old_port_addr, 3, 0);
            old_parsid_port = 0;
        }
#endif
    }
    parsid_open_status = 0;
    return 0;
}

int parsid_read(WORD addr, int chipno)
{
    int value;

    parsid_outb(parsid_port_address[old_parsid_port], addr);
    parsid_latch_open();
    parsid_sidwait();
    parsid_latch_lock();
    parsid_port_read();
    parsid_RW_read();
    parsid_chip_select();
    parsid_sidwait();
    value = parsid_inb(parsid_port_address[old_parsid_port]);
    parsid_chip_deselect();
    return value;
}

void parsid_store(WORD addr, BYTE outval, int chipno)
{
    parsid_outb(parsid_port_address[old_parsid_port], addr);
    parsid_latch_open();
    parsid_sidwait();
    parsid_latch_lock();
    parsid_outb(parsid_port_address[old_parsid_port], outval);
    parsid_chip_select();
    parsid_sidwait();
    parsid_chip_deselect();
}
#endif
