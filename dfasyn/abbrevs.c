/***************************************
  Handle state-related stuff
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

static struct Abbrev *abbrevtable=NULL;
static int nabbrevs = 0;
static int maxabbrevs = 0;

static void grow_abbrevs(void)/*{{{*/
{
  maxabbrevs += 32;
  abbrevtable = resize_array(struct Abbrev, abbrevtable, maxabbrevs);
}
/*}}}*/
struct Abbrev * create_abbrev(const char *name, struct StimulusList *stimuli)/*{{{*/
{
  struct Abbrev *result;
  if (nabbrevs == maxabbrevs) {
    grow_abbrevs();
  }
  result = abbrevtable + (nabbrevs++);
  result->lhs = new_string(name);
  result->stimuli = stimuli;
  return result;
}
/*}}}*/
struct Abbrev * lookup_abbrev(char *name)/*{{{*/
{
  int found = -1;
  int i;
  struct Abbrev *result = NULL;
  /* Scan table in reverse order.  If a name has been redefined,
     make sure the most recent definition is picked up. */
  for (i=nabbrevs-1; i>=0; i--) {
    if (!strcmp(abbrevtable[i].lhs, name)) {
      found = i;
      result = abbrevtable + found;
      break;
    }
  }

  return result;
}
/*}}}*/

