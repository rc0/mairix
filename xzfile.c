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

/* Input buffer size. */
#define IBSZ 65536

static const lzma_stream s_lzma_init = LZMA_STREAM_INIT;

struct xzFile_s {
  FILE        *m_fp;
  lzma_stream  m_lzma;
  uint8_t      m_inbuf[IBSZ];
};

xzFile *
xzopen(const char *filename, const char *mode)
{
  xzFile      *xzf;
  lzma_stream *lzs;

  if(filename == NULL || strcmp(mode, "rb") != 0)
    goto fail;
  if((xzf = new(xzFile)) == NULL)
    goto fail;
  if((xzf->m_fp = fopen(filename, mode)) == NULL)
    goto fail_free;

  lzs = &xzf->m_lzma;
  memcpy(lzs, &s_lzma_init, sizeof(*lzs));
  if(lzma_stream_decoder(lzs, UINT64_MAX, LZMA_CONCATENATED) == LZMA_OK) {
    lzs->next_in = xzf->m_inbuf;
    lzs->avail_in = fread(xzf->m_inbuf, sizeof(uint8_t), IBSZ, xzf->m_fp);
    return xzf;
  }

  fclose(xzf->m_fp);
fail_free:
  free(xzf);
fail:
  return (xzFile *) NULL;
}

int
xzread(xzFile *xzf, void *buf, int len)
{
  lzma_stream *lzs;

  if(xzf == NULL || xzf->m_fp == NULL || buf == NULL || len <= 0)
    return -1;

  lzs = &xzf->m_lzma;
  lzs->next_out = buf;
  lzs->avail_out = len;
  while(1) {
    if(lzma_code(lzs, LZMA_RUN) != LZMA_OK) /* Argh! */
      return -1;
    if(! lzs->avail_out || feof(xzf->m_fp))
      break;
    /* lzma_code either exhausted the input buffer or filled the output
     * buffer; we know it wasn't the latter.  We also know that we're
     * not at EOF.  So: it's time to refill the input buffer. */
    lzs->next_in = xzf->m_inbuf;
    lzs->avail_in = fread(xzf->m_inbuf, sizeof(uint8_t), IBSZ, xzf->m_fp);
  }

  return lzs->next_out - (uint8_t *) buf;
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
  lzma_end(&xzf->m_lzma);
  free(xzf);
}

#endif /* USE_XZ_MBOX */
