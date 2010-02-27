/***************************************
  Handle stimulus-related stuff
  ***************************************/

/*
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2005,2006
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

Stimulus *stimulus_from_epsilon(void)/*{{{*/
{
  Stimulus *result;
  result = new(Stimulus);
  result->type = T_EPSILON;
  return result;
}
/*}}}*/
Stimulus *stimulus_from_string(char *str)/*{{{*/
{
  struct Abbrev *abbrev;
  Stimulus *result;

  result = new(Stimulus);

  /* See if an abbrev exists with the name */
  abbrev = lookup_abbrev(str);

  if (abbrev) {
    result->type = T_ABBREV;
    result->x.abbrev = abbrev;
  } else {
    /* Token */
    int token;
    token = lookup_token(str, USE_OLD_MUST_EXIST);
    /* lookup_token will have bombed if it wasn't found. */
    result->type = T_TOKEN;
    result->x.token = token;
  }

  return result;

}
/*}}}*/
Stimulus *stimulus_from_inline_block(InlineBlock *block)/*{{{*/
{
  Stimulus *result;
  result = new(Stimulus);
  result->type = T_INLINEBLOCK;
  result->x.inline_block = block;
  return result;
}
/*}}}*/
Stimulus *stimulus_from_char_class(CharClass *char_class)/*{{{*/
{
  Stimulus *result;
  result = new(Stimulus);
  result->type = T_CHARCLASS;
  result->x.char_class = char_class;
  return result;
}
/*}}}*/
StimulusList *append_stimulus_to_list(StimulusList *existing, Stimulus *stim)/*{{{*/
{
  StimulusList *result;
  result = new(StimulusList);
  result->next = existing;
  result->stimulus = stim;
  return result;
}
/*}}}*/
