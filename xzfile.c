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
#include <stdlib.h>
#include <string.h>

#include "mairix.h"
#include "memmac.h"

#include "xzfile.h"

static const lzma_stream s_lzma_init = LZMA_STREAM_INIT;

struct xzFile_s {
  lzma_stream m_lzma;
};

xzFile *
xzopen(const char *filename, const char *mode)
{
  xzFile      *xzf;
  lzma_stream *lzs;
  int          len;

  if(filename == NULL || strcmp(mode, "rb") != 0)
    goto fail;
  if((xzf = new(xzFile)) == NULL)
    goto fail;

  lzs = &xzf->m_lzma;
  memcpy(lzs, &s_lzma_init, sizeof(*lzs));
  if(lzma_stream_decoder(lzs, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK)
    goto fail_free;

  create_ro_mapping(filename, (unsigned char **) &lzs->next_in, &len,
      MAP_NO_DECOMPRESSION);
  if(lzs->next_in != NULL) {
    lzs->avail_in = len;
    return xzf;
  }

  lzma_end(&xzf->m_lzma);
fail_free:
  free(xzf);
fail:
  return (xzFile *) NULL;
}

int
xzread(xzFile *xzf, void *buf, int len)
{
  lzma_stream *lzs;

  if(xzf == NULL || buf == NULL || len <= 0)
    return -1;

  lzs = &xzf->m_lzma;
  lzs->next_out = buf;
  lzs->avail_out = len;
  if(lzma_code(lzs, LZMA_RUN) != LZMA_OK) /* Argh! */
    return -1;

  return lzs->next_out - (uint8_t *) buf;
}

void
xzclose(xzFile *xzf)
{
  lzma_stream *lzs;

  if(xzf == NULL)
    return;
  lzs = &xzf->m_lzma;
  free_ro_mapping((unsigned char *) lzs->next_in - lzs->total_in,
      lzs->total_in + lzs->avail_in);
  lzma_end(&xzf->m_lzma);
  free(xzf);
}

#endif /* USE_XZ_MBOX */
