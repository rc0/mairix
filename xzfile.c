/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * xz support:
 * Copyright (C) Conrad Hughes 2020
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **********************************************************************
 */

#ifdef USE_XZ_MBOX

#include <lzma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memmac.h"

#include "xzfile.h"

struct xzFile_s {
  FILE *m_fp;
};

xzFile *
xzopen(const char *filename, const char *mode)
{
  xzFile *xzf;

  if(filename == NULL || strcmp(mode, "rb") != 0)
    goto fail;
  if((xzf = new(xzFile)) == NULL)
    goto fail;
  if((xzf->m_fp = fopen(filename, mode)) != NULL) {
    return xzf;
  }

  free(xzf);
fail:
  return (xzFile *) NULL;
}

int
xzread(xzFile *xzf, void *buf, int len)
{
  if(xzf == NULL || xzf->m_fp == NULL || buf == NULL || len <= 0)
    return -1;
  return fread(buf, sizeof(char), len, xzf->m_fp);
}

void
xzclose(xzFile *xzf)
{
  if(xzf == NULL)
    return;
  if(xzf->m_fp != NULL) {
    fclose(xzf->m_fp);
    xzf->m_fp = NULL;
  }
  free(xzf);
}

#endif /* USE_XZ_MBOX */
