/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004
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

#ifndef DATES_H
#define DATES_H

enum DATESCAN_TYPE {
  DS_FAILURE,
  DS_D,
  DS_Y,
  DS_YYMMDD,
  DS_SCALED,
  DS_M,
  DS_DM,
  DS_MD,
  DS_YM,
  DS_MY,
  DS_YMD,
  DS_DMY,
};

extern int datescan_next_state(int current_state, int next_token);
extern enum DATESCAN_TYPE datescan_exitval[];


#endif /* DATES_H */
