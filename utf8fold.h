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

/* Return a freshly allocated copy of src with Unicode simple case folding
 * applied: every cased code point is replaced by its lowercase equivalent,
 * ASCII and non-ASCII alike.  Bytes that do not form a valid UTF-8 sequence
 * are passed through unchanged, so strings in other encodings are not
 * corrupted (only their ASCII letters are folded). */
char *utf8_tolower_string(const char *src);

#endif
