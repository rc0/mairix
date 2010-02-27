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

static void maybe_grow_states(Block *b, int hash)/*{{{*/
{
  Stateset *ss = b->state_hash + hash;
  if (ss->nstates == ss->maxstates) {
    ss->maxstates += 8;
    ss->states = resize_array(State*, ss->states, ss->maxstates);
  }
  if (b->nstates == b->maxstates) {
    b->maxstates += 32;
    b->states = resize_array(State*, b->states, b->maxstates);
  }

}
/*}}}*/
static unsigned long hashfn(const char *s)/*{{{*/
{
  unsigned long y = 0UL, v, w, x, k;
  const char *t = s;
  while (1) {
    k = (unsigned long) *(unsigned char *)(t++);
    if (!k) break;
    v = ~y;
    w = y<<13;
    x = v>>6;
    y = w ^ x;
    y += k;
  }
  y ^= (y>>13);
  y &= HASH_MASK;
  return y;
}
/*}}}*/
static State * create_state(Block *b, char *name)/*{{{*/
{
  State *result;
  int hash;
  Stateset *ss;
  hash = hashfn(name);
  maybe_grow_states(b, hash);
  ss = b->state_hash + hash;
  result = b->states[b->nstates++] = ss->states[ss->nstates++] = new(State);
  result->name = new_string(name);
  result->parent = b;
  result->index = b->nstates - 1;
  result->transitions = NULL;
  result->tags = NULL;
  result->entries = NULL;
  result->ordered_trans = NULL;
  result->n_transitions = 0;
  result->removed = 0;
  return result;
}
/*}}}*/
State * lookup_state(Block *b, char *name, int create)/*{{{*/
{
  State *found = NULL;
  int i;
  int hash;
  Stateset *ss;

  hash = hashfn(name);
  ss = b->state_hash + hash;

  for (i=0; i<ss->nstates; i++) {
    if (!strcmp(ss->states[i]->name, name)) {
      found = ss->states[i];
      break;
    }
  }

  switch (create) {
    case USE_OLD_MUST_EXIST:
      if (!found) {
        fprintf(stderr, "Could not find a state '%s' in block '%s' to transition to\n", name, b->name);
        exit(1);
      }
      break;
    case CREATE_MUST_NOT_EXIST:
      if (found) {
        fprintf(stderr, "Warning : already have a state '%s' in block '%s'\n", name, b->name);
      } else {
        found = create_state(b, name);
      }
      break;
    case CREATE_OR_USE_OLD:
      if (!found) {
        found = create_state(b, name);
      }
      break;
  }

  return found;
}
/*}}}*/
void add_entry_to_state(State *curstate, const char *entry_tag)/*{{{*/
{
  struct Entrylist *new_entries = new(struct Entrylist);
  new_entries->entry_name = new_string(entry_tag);
  new_entries->state = curstate;
  new_entries->next = entries;
  entries = new_entries;
  curstate->entries = add_string_to_list(curstate->entries, entry_tag);
}
/*}}}*/
/* ================================================================= */
static void add_transition(Block *curblock, State *curstate, Stimulus *stimulus, char *destination);
/* ================================================================= */
Stringlist * add_string_to_list(Stringlist *existing, const char *token)/*{{{*/
{
  Stringlist *result = new(Stringlist);
  if (token) {
    result->string = new_string(token);
  } else {
    result->string = NULL;
  }
  result->next = existing;
  return result;
}
/*}}}*/
static TransList *new_translist(struct TransList *existing, char *destination)/*{{{*/
{
  TransList *result;
  result = new(TransList);
  result->next = existing;
  result->ds_name = new_string(destination);
  return result;
}
/*}}}*/
static void add_epsilon_transition(State *curstate, char *destination)/*{{{*/
{
  TransList *tl = new_translist(curstate->transitions, destination);
  tl->type = TT_EPSILON;
  curstate->transitions = tl;
}
/*}}}*/
static void add_token_transition(State *curstate, int token, char *destination)/*{{{*/
{
  TransList *tl = new_translist(curstate->transitions, destination);
  tl->type = TT_TOKEN;
  tl->x.token = token;
  curstate->transitions = tl;
}
/*}}}*/
static void add_abbrev_transition(Block *curblock, State *curstate, struct Abbrev *abbrev, char *destination)/*{{{*/
{
  StimulusList *stimuli;
  for (stimuli = abbrev->stimuli; stimuli; stimuli = stimuli->next) {
    add_transition(curblock, curstate, stimuli->stimulus, destination);
  }
}
/*}}}*/
static void add_inline_block_transition(Block *curblock, State *curstate, InlineBlock *ib, char *destination)/*{{{*/
{
  char block_name[1024];
  char input_name[1024];
  char output_name[1024];
  State *output_state;

  sprintf(block_name, "%s#%d", ib->type, curblock->subblockcount++);
  instantiate_block(curblock, ib->type, block_name);
  sprintf(input_name, "%s.%s", block_name, ib->in);
  sprintf(output_name, "%s.%s", block_name, ib->out);
  output_state = lookup_state(curblock, output_name, CREATE_OR_USE_OLD);
  add_epsilon_transition(curstate, input_name);
  add_epsilon_transition(output_state, destination);
}
/*}}}*/
static void add_char_class_transition(State *curstate, CharClass *cc, char *destination)/*{{{*/
{
  TransList *tl = new_translist(curstate->transitions, destination);
  tl->type = TT_CHARCLASS;
  tl->x.char_class = cc;
  curstate->transitions = tl;
}
/*}}}*/
static void add_transition(Block *curblock, State *curstate, Stimulus *stimulus, char *destination)/*{{{*/
/* Add a single transition to the state.  Allow definitions to be
   recursive */
{
  switch (stimulus->type) {
    case T_EPSILON:
      add_epsilon_transition(curstate, destination);
      break;
    case T_TOKEN:
      add_token_transition(curstate, stimulus->x.token, destination);
      break;
    case T_ABBREV:
      add_abbrev_transition(curblock, curstate, stimulus->x.abbrev, destination);
      break;
    case T_INLINEBLOCK:
      add_inline_block_transition(curblock, curstate, stimulus->x.inline_block, destination);
      break;
    case T_CHARCLASS:
      add_char_class_transition(curstate, stimulus->x.char_class, destination);
      break;
  }

}
/*}}}*/
void add_transitions(Block *curblock, State *curstate, StimulusList *stimuli, char *destination)/*{{{*/
{
  StimulusList *sl;
  for (sl=stimuli; sl; sl=sl->next) {
    add_transition(curblock, curstate, sl->stimulus, destination);
  }
}
/*}}}*/
State * add_transitions_to_internal(Block *curblock, State *addtostate, StimulusList *stimuli)/*{{{*/
{
  char buffer[1024];
  State *result;
  sprintf(buffer, "#%d", curblock->subcount++);
  result = lookup_state(curblock, buffer, CREATE_MUST_NOT_EXIST);
  add_transitions(curblock, addtostate, stimuli, result->name);
  return result;
}
/*}}}*/
void add_tags(State *curstate, Stringlist *sl)/*{{{*/
{
  if (curstate->tags) {
    /* If we already have some, stick them on the end of the new list */
    Stringlist *xsl = sl;
    while (xsl->next) xsl = xsl->next;
    xsl->next = curstate->tags;
  }
  curstate->tags = sl;
}
/*}}}*/
/* ================================================================= */
void fixup_state_refs(Block *b)/*{{{*/
{
  int i;
  for (i=0; i<b->nstates; i++) {
    State *s = b->states[i];
    TransList *tl;
    for (tl=s->transitions; tl; tl=tl->next) {
      tl->ds_ref = lookup_state(b, tl->ds_name, CREATE_OR_USE_OLD);
    }
  }
}
/*}}}*/
/* ================================================================= */
void expand_charclass_transitions(Block *b)/*{{{*/
{
  int i;
  for (i=0; i<b->nstates; i++) {
    State *s = b->states[i];
    TransList *tl;
    for (tl=s->transitions; tl; tl=tl->next) {
      if (tl->type == TT_CHARCLASS) {
        int i, first;
        CharClass *cc = tl->x.char_class;
        first = 1;
        for (i=0; i<256; i++) {
          /* Insert separate transitions for each subclass of the charclass */
          if (cc_test_bit(cc->group_bitmap, i)) {
            if (first) {
              tl->type = TT_TOKEN;
              tl->x.token = ntokens + i;
            } else {
              TransList *ntl = new(TransList);
              ntl->next = tl->next;
              ntl->ds_name = new_string(tl->ds_name);
              ntl->ds_ref = tl->ds_ref;
              ntl->type = TT_TOKEN;
              ntl->x.token = ntokens + i;
              tl->next = ntl;
            }
            first = 0;
          }
        }
      }
    }
  }
}
/*}}}*/
/* ================================================================= */
