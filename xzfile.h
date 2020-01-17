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

#ifndef XZFILE_H
#define XZFILE_H

typedef struct xzFile_s xzFile;

extern xzFile *xzopen(const char *filename, const char *mode);
extern int     xzread(xzFile *xzf, void *buf, int len);
extern void    xzclose(xzFile *xzf);

#endif /* XZFILE_H */
