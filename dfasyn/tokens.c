/***************************************
  Handle token-related stuff
  ***************************************/

/*
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2000-2003,2005,2006
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

#include "dfasyn.h"

char **toktable=NULL;
int ntokens = 0;
static int maxtokens = 0;
/* ================================================================= */
static void grow_tokens(void)/*{{{*/
{
  maxtokens += 32;
  toktable = resize_array(char *, toktable, maxtokens);
}
/*}}}*/
static int create_token(char *name)/*{{{*/
{
  int result;
  if (ntokens == maxtokens) {
    grow_tokens();
  }
  result = ntokens++;
  toktable[result] = new_string(name);
  return result;
}
/*}}}*/
int lookup_token(char *name, int create)/*{{{*/
{
  int found = -1;
  int i;
  for (i=0; i<ntokens; i++) {
    if (!strcmp(toktable[i], name)) {
      found = i;
      break;
    }
  }

  switch (create) {
    case USE_OLD_MUST_EXIST:
      if (found < 0) {
        fprintf(stderr, "Token '%s' was never declared\n", name);
        exit(1);
      }
      break;
    case CREATE_MUST_NOT_EXIST:
      if (found >= 0) {
        fprintf(stderr, "Token '%s' already declared\n", name);
        exit(1);
      } else {
        found = create_token(name);
      }
      break;
    case CREATE_OR_USE_OLD:
      if (found < 0) {
        found = create_token(name);
      }
      break;
  }

  return found;
}
/*}}}*/


