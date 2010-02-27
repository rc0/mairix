/***************************************
  Handle blocks
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


static Block **blocks = NULL;
static int nblocks = 0;
static int maxblocks = 0;

/* ================================================================= */

static void grow_blocks(void)/*{{{*/
{
  maxblocks += 32;
  blocks = resize_array(Block*, blocks, maxblocks);
}
/*}}}*/
static Block * create_block(char *name)/*{{{*/
{
  Block *result;
  int i;

  if (nblocks == maxblocks) {
    grow_blocks();
  }

#if 0
  /* Not especially useful to show this */
  if (verbose) {
    fprintf(stderr, " %s", name);
  }
#endif

  result = blocks[nblocks++] = new(Block);
  result->name = new_string(name);
  for (i=0; i<HASH_BUCKETS; i++) {
    result->state_hash[i].states = NULL;
    result->state_hash[i].nstates = 0;
    result->state_hash[i].maxstates = 0;
  }
  result->states = NULL;
  result->nstates = result->maxstates = 0;
  result->eclo = NULL;

  result->subcount = 1;
  result->subblockcount = 1;
  return result;
}
/*}}}*/
Block * lookup_block(char *name, int create)/*{{{*/
{
  Block *found = NULL;
  int i;
  for (i=0; i<nblocks; i++) {
    if (!strcmp(blocks[i]->name, name)) {
      found = blocks[i];
      break;
    }
  }

  switch (create) {
    case USE_OLD_MUST_EXIST:
      if (!found) {
        fprintf(stderr, "Could not find block '%s' to instantiate\n", name);
        exit(1);
      }
      break;
    case CREATE_MUST_NOT_EXIST:
      if (found) {
        fprintf(stderr, "Already have a block called '%s', cannot redefine\n", name);
        exit(1);
      } else {
        found = create_block(name);
      }
      break;
    case CREATE_OR_USE_OLD:
      if (!found) {
        found = create_block(name);
      }
      break;
  }

  return found;
}
/*}}}*/
/* ================================================================= */
void instantiate_block(Block *curblock, char *block_name, char *instance_name)/*{{{*/
{
  Block *master = lookup_block(block_name, USE_OLD_MUST_EXIST);
  char namebuf[1024];
  int i;
  for (i=0; i<master->nstates; i++) {
    State *s = master->states[i];
    State *new_state;
    TransList *tl;
    Stringlist *sl, *ex;

    strcpy(namebuf, instance_name);
    strcat(namebuf, ".");
    strcat(namebuf, s->name);

    /* In perverse circumstances, we might already have a state called this */
    new_state = lookup_state(curblock, namebuf, CREATE_OR_USE_OLD);

    for (tl=s->transitions; tl; tl=tl->next) {
      TransList *new_tl = new(TransList);
      new_tl->type = tl->type;
      /* Might cause some dangling ref problem later... */
      new_tl->x = tl->x;
      strcpy(namebuf, instance_name);
      strcat(namebuf, ".");
      strcat(namebuf, tl->ds_name);
      new_tl->ds_name = new_string(namebuf);
      new_tl->ds_ref = NULL;
      new_tl->next = new_state->transitions;
      new_state->transitions = new_tl;
    }

    /*{{{  Copy state tags */
    ex = NULL;
    for (sl=s->tags; sl; sl=sl->next) {
      Stringlist *new_sl = new(Stringlist);
      new_sl->string = sl->string;
      new_sl->next = ex;
      ex = new_sl;
    }
    new_state->tags = ex;
    /*}}}*/

    /* **DON'T** COPY ENTRIES : these are deliberately dropped if they occur
     * in a block that gets instantiated elsewhere. */

  }
}
/*}}}*/
/* ================================================================= */
InlineBlock *create_inline_block(char *type, char *in, char *out)/*{{{*/
{
  InlineBlock *result;
  result = new(InlineBlock);
  result->type = new_string(type);
  result->in = new_string(in);
  result->out = new_string(out);
  return result;
}
/*}}}*/
