/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2006,2007
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

#ifndef NVPTYPES_H
#define NVPTYPES_H

enum nvp_action {
  GOT_NAMEVALUE,
  GOT_NAMEVALUE_CONT,
  GOT_NAME,
  GOT_NAME_TRAILING_SPACE,
  GOT_MAJORMINOR,
  GOT_TERMINATOR,
  GOT_NOTHING
};

enum nvp_copier {
  COPY_TO_NAME,
  COPY_TO_MINOR,
  COPY_TO_VALUE,
  COPY_NOWHERE
};

#endif
