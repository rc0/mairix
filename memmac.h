/*
  $Header: /cvs/src/mairix/memmac.h,v 1.1 2002/07/03 22:15:59 richard Exp $

  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002
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


#ifndef MEMMAC_H
#define MEMMAC_H

/*{{{  Memory macros*/
#define new_string(s) strcpy((char *) malloc(1+strlen(s)), (s))
#define extend_string(x,s) (strcat(realloc(x, (strlen(x)+strlen(s)+1)), s))
#define new(T) (T *) malloc(sizeof(T))
#define new_array(T, n) (T *) malloc(sizeof(T) * (n))
#define grow_array(T, n, oldX) (T *) ((oldX) ? realloc(oldX, (sizeof(T) * (n))) : malloc(sizeof(T) * (n)))
#define EMPTY(x) {&(x), &(x)}
/*}}}*/

#endif /* MEMMAC_H */
