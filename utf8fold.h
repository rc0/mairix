/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004, 2005
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

#ifndef UTF8FOLD_H
#define UTF8FOLD_H

/* Fold a NUL-terminated string in place, applying Unicode simple case
 * folding (see utf8fold.c for the exact rules).  The string's byte
 * length never changes.  The same folding is applied to tokens when
 * indexing and to search words when searching, so the two sides always
 * agree. */
void utf8_fold_string(char *str);

#endif
