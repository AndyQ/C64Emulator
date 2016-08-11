/*
 * ppmdrv.c - Create a PPM file.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "archdep.h"
#include "ppmdrv.h"
#include "lib.h"
#include "log.h"
#include "gfxoutput.h"
#include "palette.h"
#include "screenshot.h"
#include "types.h"
#include "util.h"


typedef struct gfxoutputdrv_data_s
{
  FILE *fd;
  char *ext_filename;
  BYTE *data;
  unsigned int line;
} gfxoutputdrv_data_t;

#if defined(__BEOS__) && defined(WORDS_BIGENDIAN)
extern gfxoutputdrv_t ppm_drv;
#else
static gfxoutputdrv_t ppm_drv;
#endif

static int ppmdrv_write_file_header(screenshot_t *screenshot)
{
  gfxoutputdrv_data_t *sdata;

  sdata = screenshot->gfxoutputdrv_data;

  if (fprintf(sdata->fd,"P3\x0a")<0)
    return -1;
  if (fprintf(sdata->fd,"# VICE generated PPM screenshot\x0a")<0)
    return -1;
  if (fprintf(sdata->fd,"%d %d\x0a",screenshot->width,screenshot->height)<0)
    return -1;
  if (fprintf(sdata->fd,"255\x0a")<0)
    return -1;

  return 0;
}

static int ppmdrv_open(screenshot_t *screenshot, const char *filename)
{
  gfxoutputdrv_data_t *sdata;

  sdata = lib_malloc(sizeof(gfxoutputdrv_data_t));
  screenshot->gfxoutputdrv_data = sdata;
  sdata->line = 0;
  sdata->ext_filename=util_add_extension_const(filename, ppm_drv.default_extension);
  sdata->fd = fopen(sdata->ext_filename, "wb");

  if (sdata->fd==NULL)
  {
    lib_free(sdata->ext_filename);
    lib_free(sdata);
    return -1;
  }

  if (ppmdrv_write_file_header(screenshot)<0)
  {
    fclose(sdata->fd);
    lib_free(sdata->ext_filename);
    lib_free(sdata);
    return -1;
  }

  sdata->data = lib_malloc(screenshot->width*3);

  return 0;
}

static int ppmdrv_write(screenshot_t *screenshot)
{
  gfxoutputdrv_data_t *sdata;
  unsigned int i;

  sdata = screenshot->gfxoutputdrv_data;
  (screenshot->convert_line)(screenshot, sdata->data, sdata->line, SCREENSHOT_MODE_RGB24);

  for (i = 0; i<screenshot->width; i++)
  {
    if (fprintf(sdata->fd, "%3d %3d %3d\x0a",sdata->data[i*3],sdata->data[(i*3)+1],sdata->data[(i*3)+2])<0)
      return -1;
  }
  return 0;
}

static int ppmdrv_close(screenshot_t *screenshot)
{
  gfxoutputdrv_data_t *sdata;

  sdata = screenshot->gfxoutputdrv_data;

  fclose(sdata->fd);
  lib_free(sdata->data);
  lib_free(sdata->ext_filename);
  lib_free(sdata);

  return 0;
}

static int ppmdrv_save(screenshot_t *screenshot, const char *filename)
{
  if (ppmdrv_open(screenshot, filename) < 0)
    return -1;

  for (screenshot->gfxoutputdrv_data->line = 0; 
       screenshot->gfxoutputdrv_data->line < screenshot->height;
       (screenshot->gfxoutputdrv_data->line)++)
  {
    ppmdrv_write(screenshot);
  }

  if (ppmdrv_close(screenshot) < 0)
    return -1;

  return 0;
}

#ifdef FEATURE_CPUMEMHISTORY
static FILE *ppmdrv_memmap_fd;
static char *ppmdrv_memmap_ext_filename;

static int ppmdrv_close_memmap(void)
{
  fclose(ppmdrv_memmap_fd);
  lib_free(ppmdrv_memmap_ext_filename);

  return 0;
}

static int ppmdrv_write_memmap(int line, int x_size, BYTE *gfx, BYTE *palette)
{
  int i;
  BYTE pixval;

  for (i = 0; i<x_size; i++)
  {
    pixval = gfx[(line*x_size)+i];
    if (fprintf(ppmdrv_memmap_fd, "%3d %3d %3d\x0a",palette[pixval*3],palette[(pixval*3)+1],palette[(pixval*3)+2])<0)
      return -1;
  }
  return 0;
}

static int ppmdrv_write_file_header_memmap(int x_size, int y_size)
{
  if (fprintf(ppmdrv_memmap_fd,"P3\x0a")<0)
    return -1;
  if (fprintf(ppmdrv_memmap_fd,"# VICEplus generated PPM mem map grafix\x0a")<0)
    return -1;
  if (fprintf(ppmdrv_memmap_fd,"%d %d\x0a",x_size,y_size)<0)
    return -1;
  if (fprintf(ppmdrv_memmap_fd,"255\x0a")<0)
    return -1;

  return 0;
}

static int ppmdrv_open_memmap(const char *filename, int x_size, int y_size)
{
  ppmdrv_memmap_ext_filename=util_add_extension_const(filename, ppm_drv.default_extension);
  ppmdrv_memmap_fd = fopen(ppmdrv_memmap_ext_filename, "wb");

  if (ppmdrv_memmap_fd==NULL)
  {
    lib_free(ppmdrv_memmap_ext_filename);
    return -1;
  }

  if (ppmdrv_write_file_header_memmap(x_size, y_size)<0)
  {
    fclose(ppmdrv_memmap_fd);
    lib_free(ppmdrv_memmap_ext_filename);
    return -1;
  }

  return 0;
}

static int ppmdrv_save_memmap(const char *filename, int x_size, int y_size, BYTE *gfx, BYTE *palette)
{
  int line;

  if (ppmdrv_open_memmap(filename, x_size, y_size) < 0)
    return -1;

  for (line = 0; line < y_size; line++)
  {
    ppmdrv_write_memmap(line, x_size, gfx, palette);
  }

  if (ppmdrv_close_memmap() < 0)
    return -1;

  return 0;
}
#endif

static gfxoutputdrv_t ppm_drv =
{
    "PPM",
    "PPM screenshot",
    "ppm",
    NULL, /* formatlist */
    ppmdrv_open,
    ppmdrv_close,
    ppmdrv_write,
    ppmdrv_save,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
#ifdef FEATURE_CPUMEMHISTORY
    ,ppmdrv_save_memmap
#endif
};

void gfxoutput_init_ppm(void)
{
  gfxoutput_register(&ppm_drv);
}
