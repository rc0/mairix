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
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * 
 **********************************************************************
 */

#ifndef _FROMCHECK_H
#define _FROMCHECK_H

enum fromcheck_result {
  FROMCHECK_PASS,
  FROMCHECK_FAIL
};

extern int fromcheck_next_state(int, int);
extern enum fromcheck_result fromcheck_exitval[];

/* Tokens, keep in the same sequence as the list in the fromcheck.nfa file */
#define FS_LF 0
#define FS_CR 1
#define FS_DIGIT 2
#define FS_AT 3
#define FS_COLON 4
#define FS_WHITE 5
#define FS_LOWER 6
#define FS_UPPER 7
#define FS_PLUSMINUS 8
#define FS_OTHEREMAIL 9

#endif

