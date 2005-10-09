/***************************************
  $Header: /cvs/src/dfasyn/n2d.c,v 1.4 2003/03/03 00:07:19 richard Exp $

  Main program for NFA to DFA table builder program.
  ***************************************/

/* 
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2000-2003
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

/* {{{ General comments
  Convert a nondeterminstic finite automaton (NFA) into a deterministic finite
  automaton (DFA).

  The NFA is defined in terms of a set of states, with transitions between the
  states.  The transitions may occur on any one of a set of symbols (specified
  with | characters between the options), or may be 'epsilon' transitions, i.e.
  occurring without consumption of any input.  A state may have multiple
  transitions for the same input symbol (hence 'nondeterministic').  The final
  state encountered within the final block defined in the input file is taken
  to be the start state of the whole NFA.  A state may be entered more than
  once in the file; the transitions in the multiple definitions are combined to
  give the complete transition set.  A state may have an exit value assigned
  (with =); this is the return value of the automaton if the end of string is
  encountered when in that state.  (If the resulting DFA can be in multiple
  exiting NFA states when the end of string is reached, the result is all the
  associated NFA exit values or'd together, so it is best to use distinct bits
  for NFA exit values unless it is known that is safe not to in a particular
  case.) The input grammar allows a BLOCK <name> ... ENDBLOCK construction +
  block instantiation.  This allows common parts of the NFA state machine to be
  reused in multiple places as well as aiding structuring and readability.  See
  morf_nfa.in for an example of the input grammar, and morf.c for a
  (non-trivial) example of how to build the automaton around the tables that
  this script generates.
  }}} */

#include <ctype.h>
#include "n2d.h"

/* Globally visible options to control reporting */
FILE *report;
FILE *input;
FILE *output;
extern FILE *yyin;
extern FILE *yyout;
int verbose;

static Block **blocks = NULL;
static int nblocks = 0;
static int maxblocks = 0;

static char **toktable=NULL;
static int ntokens = 0;
static int maxtokens = 0;

struct Abbrev {
  char *lhs; /* Defined name */
  char **rhs; /* Token/define */
  int nrhs;
  int maxrhs;
};

static struct Abbrev *abbrevtable=NULL;
static int nabbrevs = 0;
static int maxabbrevs = 0;

extern int yyparse(void);

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
/* ================================================================= */
static void grow_abbrevs(void)/*{{{*/
{
  maxabbrevs += 32;
  abbrevtable = resize_array(struct Abbrev, abbrevtable, maxabbrevs);
}
/*}}}*/
struct Abbrev * create_abbrev(char *name)/*{{{*/
{
  struct Abbrev *result;
  if (nabbrevs == maxabbrevs) {
    grow_abbrevs();
  }
  result = abbrevtable + (nabbrevs++);
  result->lhs = new_string(name);
  result->nrhs = result->maxrhs = 0;
  result->rhs = 0;
  return result;
}
/*}}}*/
void add_tok_to_abbrev(struct Abbrev *abbrev, char *tok)/*{{{*/
{
  if (abbrev->nrhs == abbrev->maxrhs) {
    abbrev->maxrhs += 8;
    abbrev->rhs = resize_array(char *, abbrev->rhs, abbrev->maxrhs);
  }

  abbrev->rhs[abbrev->nrhs++] = new_string(tok);
}
/*}}}*/
static struct Abbrev * lookup_abbrev(char *name, int create)/*{{{*/
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

  switch (create) {
    case CREATE_MUST_NOT_EXIST:
      if (found >= 0) {
        fprintf(stderr, "Abbreviation '%s' already declared\n", name);
        exit(1);
      } else {
        result = create_abbrev(name);
      }
      break;
    case CREATE_OR_USE_OLD:
      if (found < 0) {
        result = create_abbrev(name);
      }
      break;
  }
  
  return result;
}
/*}}}*/
/* ================================================================= */

struct Attribute {
  char *name;
};

#if 0
static struct Attribute *attributes = NULL;
static int n_attributes = 0;
static int max_attributes = 0;
#endif

#if 0
static void grow_attributes(void)/*{{{*/
{
  max_attributes += 16;
  attributes = resize_array(struct Attribute, attributes, max_attributes);
}
/*}}}*/
#endif
#if 0
static int create_attribute(char *name)/*{{{*/
{
  int n;
  if (n_attributes == max_attributes) {
    grow_attributes();
  }
  
  n = n_attributes;
  attributes[n].name = new_string(name);
  n_attributes++;
  return n;
}
/*}}}*/
#endif
#if 0
static int lookup_attribute(char *name)/*{{{*/
  /* Always create if not found */
{
  int i;
  for (i=0; i<n_attributes; i++) {
    if (!strcmp(name, attributes[i].name)) {
      return i;
    }
  }
  return create_attribute(name);
}
/*}}}*/
#endif
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
/* ================================================================= */
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
  result->exitvals = NULL;
  result->attributes = NULL;
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
/* ================================================================= */
Stringlist * add_token(Stringlist *existing, char *token)/*{{{*/
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
static void add_transition(State *curstate, char *str, char *destination)/*{{{*/
/* Add a single transition to the state.  Allow definitions to be
   recursive */
{
  struct Abbrev *abbrev;
  abbrev = (str) ? lookup_abbrev(str, USE_OLD_MUST_EXIST) : NULL;
  if (abbrev) {
    int i;
    for (i=0; i<abbrev->nrhs; i++) {
      add_transition(curstate, abbrev->rhs[i], destination);
    }
  } else {
    Translist *tl;
    tl = new(Translist);
    tl->next = curstate->transitions;
    /* No problem with aliasing, these strings are read-only and have
       lifetime = until end of program */
    tl->token = (str) ? lookup_token(str, USE_OLD_MUST_EXIST) : -1;
    tl->ds_name = destination;
    curstate->transitions = tl;
  }
}
/*}}}*/
void add_transitions(State *curstate, Stringlist *tokens, char *destination)/*{{{*/
{
  Stringlist *sl;
  for (sl=tokens; sl; sl=sl->next) {
    add_transition(curstate, sl->string, destination);
  }
}
/*}}}*/
State * add_transitions_to_internal(Block *curblock, State *addtostate, Stringlist *tokens)/*{{{*/
{
  char buffer[1024];
  State *result;
  sprintf(buffer, "#%d", curblock->subcount++);
  result = lookup_state(curblock, buffer, CREATE_MUST_NOT_EXIST);
  add_transitions(addtostate, tokens, result->name);
  return result;
}
/*}}}*/
void add_exit_value(State *curstate, char *value)/*{{{*/
{
  Stringlist *sl;
  sl = new(Stringlist);
  sl->string = value;
  sl->next = curstate->exitvals;
  curstate->exitvals = sl;
}
/*}}}*/
void set_state_attribute(State *curstate, char *name)/*{{{*/
{
  Stringlist *sl;
  sl = new(Stringlist);
  sl->string = name;
  sl->next = curstate->attributes;
  curstate->attributes = sl;
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
InlineBlockList *add_inline_block(InlineBlockList *existing, InlineBlock *nib)/*{{{*/
{
  InlineBlockList *result;
  result = new(InlineBlockList);
  result->next = existing;
  result->ib = nib;
  return result;
}
/*}}}*/
State * add_inline_block_transitions(Block *curblock, State *addtostate, InlineBlockList *ibl)/*{{{*/
{
  char result_name[1024];
  State *result_state;

  /* Construct output state */
  sprintf(result_name, "#%d", curblock->subcount++);
  result_state = lookup_state(curblock, result_name, CREATE_MUST_NOT_EXIST);
  
  while (ibl) {
    InlineBlock *ib;
    char block_name[1024];
    char input_name[1024];
    char output_name[1024];
    State *output_state;
    
    ib = ibl->ib;
    if (ib) {
      sprintf(block_name, "%s#%d", ib->type, curblock->subblockcount++);
      instantiate_block(curblock, ib->type, block_name);
      sprintf(input_name, "%s.%s", block_name, ib->in);
      sprintf(output_name, "%s.%s", block_name, ib->out);
      output_state = lookup_state(curblock, output_name, CREATE_OR_USE_OLD);

      /* Now plumb in the input and output transitions */
      add_transition(addtostate, NULL, new_string(input_name));
      add_transition(output_state, NULL, new_string(result_name));
    } else {
      /* Epsilon transition between input and output */
      add_transition(addtostate, NULL, new_string(result_name));
    }

    ibl = ibl->next;
  }

  return result_state;
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
    Translist *tl;
    Stringlist *sl, *ex;
    
    strcpy(namebuf, instance_name);
    strcat(namebuf, ".");
    strcat(namebuf, s->name);
    
    /* In perverse circumstances, we might already have a state called this */
    new_state = lookup_state(curblock, namebuf, CREATE_OR_USE_OLD);
    
    for (tl=s->transitions; tl; tl=tl->next) {
      Translist *new_tl = new(Translist);
      new_tl->token = tl->token;
      strcpy(namebuf, instance_name);
      strcat(namebuf, ".");
      strcat(namebuf, tl->ds_name);
      new_tl->ds_name = new_string(namebuf);
      new_tl->ds_ref = NULL;
      new_tl->next = new_state->transitions;
      new_state->transitions = new_tl;
    }
    
    /*{{{  Copy state exit values*/
    ex = NULL;
    for (sl=s->exitvals; sl; sl=sl->next) {
      Stringlist *new_sl = new(Stringlist);
      new_sl->string = sl->string;
      new_sl->next = ex;
      ex = new_sl;
    }
    new_state->exitvals = ex;
    /*}}}*/
    /*{{{  Copy state attributes */
    ex = NULL;
    for (sl=s->attributes; sl; sl=sl->next) {
      Stringlist *new_sl = new(Stringlist);
      new_sl->string = sl->string;
      new_sl->next = ex;
      ex = new_sl;
    }
    new_state->attributes = ex;
    /*}}}*/
    
  }
}
/*}}}*/
void fixup_state_refs(Block *b)/*{{{*/
{
  int i;
  for (i=0; i<b->nstates; i++) {
    State *s = b->states[i];
    Translist *tl;
    for (tl=s->transitions; tl; tl=tl->next) {
      tl->ds_ref = lookup_state(b, tl->ds_name, CREATE_OR_USE_OLD);
    }
  }
}
/*}}}*/
/* ================================================================= */

/* Bitmap to contain epsilon closure for NFA */

static unsigned long **eclo;

/* ================================================================= */
static inline const int round_up(const int x) {/*{{{*/
  return (x+31)>>5;
}
/*}}}*/
static inline void set_bit(unsigned long *x, int n)/*{{{*/
{
  int r = n>>5;
  unsigned long m = 1UL<<(n&31);
  x[r] |= m;
}
/*}}}*/
static inline int is_set(unsigned long *x, int n)/*{{{*/
{
  int r = n>>5;
  unsigned long m = 1UL<<(n&31);
  return !!(x[r] & m);
}
/*}}}*/
/* ================================================================= */
static void transitively_close_eclo(int N)/*{{{*/
{
  int from;
  unsigned long *from_row;
  unsigned long *todo, this_todo;
  int Nru;
  int i, i32, j, k, merge_idx;
  int j_limit;
  int any_changes;

  Nru = round_up(N);
  todo = new_array(unsigned long, Nru);

  for (from=0; from<N; from++) {
    from_row = eclo[from];
    for (i=0; i<Nru; i++) {
      todo[i] = from_row[i];
    }
    any_changes = 1;
    while (any_changes) {
      any_changes = 0;
      for (i=0; i<Nru; i++) { /* loop over words in bitvector */
        i32 = i<<5;
        this_todo = todo[i];
        todo[i] = 0UL; /* reset to avoid oo-loop */
        if (!this_todo) continue; /* none to do in this block */
        j_limit = N - i32;
        if (j_limit > 32) j_limit = 32;

        for (j=0; j<j_limit;) { /* loop over bits in this word */
          if (this_todo & 1) {
            /* Merge in */
            merge_idx = i32 + j;
            for (k=0; k<Nru; k++) {
              unsigned long to_merge = eclo[merge_idx][k];
              unsigned long orig = from_row[k];
              unsigned long diffs = to_merge & (~orig);
              from_row[k] |= to_merge;
              if (diffs) any_changes = 1;
              todo[k] |= diffs;
            }
          }
          this_todo >>= 1;
          if (!this_todo) break; /* Workload reduction at end */
          j++;
        }
      }
    }
  }
}
/*}}}*/
static void generate_epsilon_closure(Block *b)/*{{{*/
{
  int i, j, N;
  
  N = b->nstates;
  eclo = new_array(unsigned long*, N);
  for (i=0; i<N; i++) {
    eclo[i] = new_array(unsigned long, round_up(N));
    for (j=0; j<round_up(N); j++) {
      eclo[i][j] = 0;
    }
  }

  /* Determine initial immediate transitions */
  for (i=0; i<N; i++) {
    State *s = b->states[i];
    Translist *tl;
    int from_state = s->index;
    set_bit(eclo[from_state], from_state); /* Always reflexive */
    
    for (tl=s->transitions; tl; tl=tl->next) {
      if (tl->token < 0) { /* epsilon trans */
        int to_state = tl->ds_ref->index;
        set_bit(eclo[from_state], to_state);
      }
    }
  }

  transitively_close_eclo(N);

}
/*}}}*/
static void print_nfa(Block *b)/*{{{*/
{
  int i, j, N;
  N = b->nstates;
  
  if (!report) return;

  for (i=0; i<N; i++) {
    State *s = b->states[i];
    Translist *tl;
    Stringlist *sl;
    fprintf(report, "NFA state %d = %s\n", i, s->name);
    for (tl=s->transitions; tl; tl=tl->next) {
      fprintf(report, "  [%s] -> %s\n",
              (tl->token >= 0) ? toktable[tl->token] : "(epsilon)",
              tl->ds_name);
    }
    if (s->exitvals) {
      int first = 1;
      fprintf(report, "  Exit value : ");
      for (sl=s->exitvals; sl; sl=sl->next) {
        fprintf(report, "%s%s",
                first ? "" : "|",
                sl->string);
      }
      fprintf(report, "\n");
    }
    if (s->attributes) {
      int first = 1;
      fprintf(report, "  Attributes : ");
      for (sl=s->attributes; sl; sl=sl->next) {
        fprintf(report, "%s%s",
                first ? "" : "|",
                sl->string);
      }
      fprintf(report, "\n");
    }
    fprintf(report, "  Epsilon closure :\n    (self)\n");
    for (j=0; j<N; j++) {
      if (i!=j && is_set(eclo[i], j)) {
        fprintf(report, "    %s\n", b->states[j]->name);
      }
    }
    
    fprintf(report, "\n");
  }

}
/*}}}*/
/* ================================================================= */

/* Indexed [from_state][token][to_state], flag set if there is
   a transition from from_state to to_state, via token then zero or more
   epsilon transitions */

static unsigned long ***transmap;

/* Index [from_nfa_state][token], flag set if there is a transition
   to any destination nfa state for that token. */
static unsigned long **anytrans;

/* ================================================================= */
static void build_transmap(Block *b)/*{{{*/
{
  int N = b->nstates;
  int Nt = ntokens;
  int i, j, k, m;
  
  transmap = new_array(unsigned long **, N);
  anytrans = new_array(unsigned long *, N);
  for (i=0; i<N; i++) {
    transmap[i] = new_array(unsigned long *, Nt);
    anytrans[i] = new_array(unsigned long, round_up(Nt));
    for (j=0; j<round_up(Nt); j++) {
      anytrans[i][j] = 0UL;
    }
    for (j=0; j<Nt; j++) {
      transmap[i][j] = new_array(unsigned long, round_up(N));
      for (k=0; k<round_up(N); k++) {
        transmap[i][j][k] = 0UL;
      }
    }
  }

  for (i=0; i<N; i++) {
    State *s = b->states[i];
    Translist *tl;
    for (tl=s->transitions; tl; tl=tl->next) {
      if (tl->token >= 0) {
        int dest = tl->ds_ref->index;
        for (m=0; m<round_up(N); m++) {
          unsigned long x = eclo[dest][m];
          transmap[i][tl->token][m] |= x;
          if (!!x) set_bit(anytrans[i], tl->token);
        }
      }
    }
  }

  
}
/*}}}*/
/* ================================================================= */

static DFANode **dfas;
static int ndfa=0;
static int maxdfa=0;

static int had_ambiguous_result = 0;

/* ================================================================= */

/* Implement an array of linked lists to access DFA states directly.  The
 * hashes are given by folding the signatures down to single bytes. */

struct DFAList {
  struct DFAList *next;
  DFANode *dfa;
};

#define DFA_HASHSIZE 256
static struct DFAList *dfa_hashtable[DFA_HASHSIZE];

/* ================================================================= */
static void grow_dfa(void)/*{{{*/
{ 
  maxdfa += 32;
  dfas = resize_array(DFANode*, dfas, maxdfa);
}
/*}}}*/
static unsigned long fold_signature(unsigned long sig)/*{{{*/
{
  unsigned long folded;
  folded = sig ^ (sig >> 16);
  folded ^= (folded >> 8);
  folded &= 0xff;
  return folded;
}
/*}}}*/
/* ================================================================= */
static int find_dfa(unsigned long *nfas, int N)/*{{{*/
/* Simple linear search.  Use 'signatures' to get rapid rejection
   of any DFA state that can't possibly match */
{
  int j;
  unsigned long signature = 0UL;
  unsigned long folded_signature;
  struct DFAList *dfal;

  for (j=0; j<round_up(N); j++) {
    signature ^= nfas[j];
  }
  folded_signature = fold_signature(signature);
  
  for(dfal=dfa_hashtable[folded_signature]; dfal; dfal = dfal->next) {
    DFANode *dfa = dfal->dfa;
    int matched;

    if (signature != dfa->signature) continue;
    
    matched=1;

    for (j=0; j<round_up(N); j++) {
      if (nfas[j] != dfa->nfas[j]) {
        matched = 0;
        break;
      }
    }
    if (matched) {
      return dfa->index;
    }
  }
  return -1;
}
/*}}}*/

static int add_dfa(Block *b, unsigned long *nfas, int N, int Nt, int from_state, int via_token)/*{{{*/
{
  int j;
  int result = ndfa;
  int had_exitvals;
  int this_result_unambiguous;
 
  Stringlist *ex;
  unsigned long signature = 0UL, folded_signature;
  struct DFAList *dfal;

  if (verbose) {
    fprintf(stderr, "Adding DFA state %d\r", ndfa);
    fflush(stderr);
  }

  if (maxdfa == ndfa) {
    grow_dfa();
  }

  dfas[ndfa] = new(DFANode);
  dfas[ndfa]->nfas = new_array(unsigned long, round_up(N));
  dfas[ndfa]->map = new_array(int, Nt);
  for (j=0; j<Nt; j++) dfas[ndfa]->map[j] = -1;
  dfas[ndfa]->index = ndfa;
  dfas[ndfa]->defstate = -1;
  
  dfas[ndfa]->from_state = from_state;
  dfas[ndfa]->via_token = via_token;
  
  for (j=0; j<round_up(N); j++) {
    unsigned long x = nfas[j];
    signature ^= x;
    dfas[ndfa]->nfas[j] = x;
  }
  dfas[ndfa]->signature = signature;
  
  folded_signature = fold_signature(signature);
  dfal = new(struct DFAList);
  dfal->dfa = dfas[ndfa];
  dfal->next = dfa_hashtable[folded_signature];
  dfa_hashtable[folded_signature] = dfal;

  /* {{{ Boolean reduction for result */
  ex = NULL;
  had_exitvals = 0;
  clear_symbol_values(exit_evaluator);
  for (j=0; j<N; j++) {
    if (is_set(dfas[ndfa]->nfas, j)) {
      Stringlist *sl;
      State *s = b->states[j];
      for (sl = s->exitvals; sl; sl = sl->next) {
        Stringlist *new_sl;
        new_sl = new(Stringlist);
        new_sl->string = sl->string;
        new_sl->next = ex;
        ex = new_sl;

        set_symbol_value(exit_evaluator, sl->string);
        had_exitvals = 1;
      }
    }
  }
  
  this_result_unambiguous = evaluate_result(exit_evaluator, &dfas[ndfa]->result, &dfas[ndfa]->result_early);
  dfas[ndfa]->nfa_exit_sl = ex;

  if (!this_result_unambiguous) {
    Stringlist *sl;
    fprintf(stderr, "WARNING : Ambiguous exit state abandoned for DFA state %d\n", ndfa);
    fprintf(stderr, "NFA exit tags applying in this stage :\n");
    for (sl = ex; sl; sl = sl->next) {
      fprintf(stderr, "  %s\n", sl->string);
    }
    had_ambiguous_result = 1;
  }
  /*}}}*/
  /* {{{ Boolean reduction for attributes */
  ex = NULL;
  had_exitvals = 0;
  clear_symbol_values(attr_evaluator);
  for (j=0; j<N; j++) {
    if (is_set(dfas[ndfa]->nfas, j)) {
      Stringlist *sl;
      State *s = b->states[j];
      for (sl = s->attributes; sl; sl = sl->next) {
        Stringlist *new_sl;
        new_sl = new(Stringlist);
        new_sl->string = sl->string;
        new_sl->next = ex;
        ex = new_sl;

        set_symbol_value(attr_evaluator, sl->string);
        had_exitvals = 1;
      }
    }
  }
  this_result_unambiguous = evaluate_result(attr_evaluator, &dfas[ndfa]->attribute, NULL);
  dfas[ndfa]->nfa_attr_sl = ex;

  if (!this_result_unambiguous) {
    Stringlist *sl;
    fprintf(stderr, "WARNING : Ambiguous attribute abandoned for DFA state %d\n", ndfa);
    fprintf(stderr, "NFA attribute tags applying in this stage :\n");
    for (sl = ex; sl; sl = sl->next) {
      fprintf(stderr, "  %s\n", sl->string);
    }
    had_ambiguous_result = 1;
  }
  /*}}}*/
  
  ndfa++;
  return result;
}
/*}}}*/
static void clear_nfas(unsigned long *nfas, int N)/*{{{*/
{
  int i;
  for (i=0; i<round_up(N); i++) {
    nfas[i] = 0;
  }
}
/*}}}*/
static void build_dfa(Block *b, int start_index)/*{{{*/
{
  unsigned long **nfas;
  int i;
  int N, Nt;
  int next_to_do;
  int *found_any;
  int rup_N;

  for (i=0; i<DFA_HASHSIZE; i++) dfa_hashtable[i] = NULL;
  
  N = b->nstates;
  rup_N = round_up(N);
  Nt = ntokens;
  
  /* Add initial state */
  nfas = new_array(unsigned long *, Nt);
  for (i=0; i<Nt; i++) {
    nfas[i] = new_array(unsigned long, round_up(N));
  }
  clear_nfas(nfas[0], N);
  for (i=0; i<round_up(N); i++) {
    nfas[0][i] |= eclo[start_index][i];
  }
  add_dfa(b, nfas[0], N, Nt, -1, -1);
  next_to_do = 0;
  found_any = new_array(int, Nt);

  /* Now the heart of the program : the subset construction to turn the NFA
     into a DFA.  This is a major performance hog in the program, so there are
     lots of tricks to speed this up (particularly, hoisting intermediate
     pointer computations out of the loop to assert the fact that there is no
     aliasing between the arrays.) */

  while (next_to_do < ndfa) {

    int t; /* token index */
    int j0, j0_5, j1, j, mask, k;
    int idx;
    unsigned long *current_nfas;
    unsigned long block_bitmap;

    /* If the next DFA state has the result_early flag set, it means that the scanner will
     * always exit straight away when that state is reached, so there's no need to compute
     * any transitions out of it. */

    if (dfas[next_to_do]->result_early) {
      next_to_do++;
      continue;
    }

    for (j=0; j<Nt; j++) {
      clear_nfas(nfas[j], N);
      found_any[j] = 0;
    }

    current_nfas = dfas[next_to_do]->nfas;
    for (j0=0; j0<rup_N; j0++) { /* Loop over NFA states which may be in this DFA state */
      block_bitmap = current_nfas[j0];
      if (!block_bitmap) continue;
      j0_5 = j0 << 5;
      for (mask=1UL, j1=0; j1<32; mask<<=1, j1++) {
        j = j0_5 + j1;
        if (block_bitmap & mask) { /* Is NFA state in DFA */
          unsigned long **transmap_j = transmap[j];
          unsigned long *anytrans_j = anytrans[j];
          for (t=0; t<Nt; t++) { /* Loop over transition symbols */
            unsigned long *transmap_t;
            unsigned long *nfas_t;
            unsigned long found_any_t;
            if (!is_set(anytrans_j, t)) continue;
            transmap_t = transmap_j[t];
            nfas_t = nfas[t];
            found_any_t = found_any[t];
            for (k=0; k<rup_N; k++) { /* Loop over destination NFA states */
              unsigned long x;
              x = transmap_t[k];
              nfas_t[k] |= x;
              found_any_t |= !!x;
            }
            found_any[t] = found_any_t;
          }
        }
      }
    }
          
    for (t=0; t<Nt; t++) {
      if (found_any[t]) {
        idx = find_dfa(nfas[t], N);
        if (idx < 0) {
          idx = add_dfa(b, nfas[t], N, Nt, next_to_do, t);
        }
      } else {
        idx = -1;
      }
      dfas[next_to_do]->map[t] = idx;
    }

    next_to_do++;
  }

  free(found_any);
  for (i=0; i<Nt; i++) free(nfas[i]);
  free(nfas);
}
/*}}}*/
/* ================================================================= */
static void print_dfa(Block *b)/*{{{*/
{
  int N = b->nstates;
  int Nt = ntokens;
  
  int i, j0, j0_5, j1, t;
  unsigned long mask;
  unsigned long current_nfas;
  int rup_N = round_up(N);
  int from_state, this_state, via_token, maxtrace;

  if (!report) return;
  
  for (i=0; i<ndfa; i++) {
    fprintf(report, "DFA state %d\n", i);
    if (dfas[i]->nfas) {
      fprintf(report, "  NFA states :\n");
      for (j0=0; j0<rup_N; j0++) {
        current_nfas = dfas[i]->nfas[j0];
        if (!current_nfas) continue;
        j0_5 = j0<<5;
        for (j1=0, mask=1UL; j1<32; mask<<=1, j1++) {
          if (current_nfas & mask) {
            fprintf(report, "    %s\n", b->states[j0_5 + j1]->name);
          }
        }
      }
      fprintf(report, "\n");
    }
    fprintf(report, "  Reverse route :\n    HERE");
    this_state = i;
    from_state = dfas[i]->from_state;
    maxtrace=0;
    while (from_state >= 0) {
      via_token = dfas[this_state]->via_token;
      fprintf(report, "<-%s", toktable[via_token]);
      this_state = from_state;
      from_state = dfas[this_state]->from_state;
      maxtrace++;
      if (maxtrace>100) break;
    }
    fprintf(report, "\n");
    
    fprintf(report, "  Transitions :\n");
    for (t=0; t<Nt; t++) {
      int dest = dfas[i]->map[t];
      if (dest >= 0) {
        fprintf(report, "    %s -> %d\n", toktable[t], dest);
      }
    }
    if (dfas[i]->defstate >= 0) {
      fprintf(report, "  Use state %d as basis (%d fixups)\n",
              dfas[i]->defstate, dfas[i]->best_diff);
    }
    if (dfas[i]->nfa_exit_sl) {
      Stringlist *sl;
      fprintf(report, "  NFA exit tags applying :\n");
      for (sl=dfas[i]->nfa_exit_sl; sl; sl = sl->next) {
        fprintf(report, "    %s\n", sl->string);
      }
    }
    if (dfas[i]->result) {
      fprintf(report, "  Exit value : %s\n", dfas[i]->result);
    }
    if (dfas[i]->attribute) {
      fprintf(report, "  Attribute : %s\n", dfas[i]->attribute);
    }

    fprintf(report, "\n");
  }
}
/*}}}*/
/* ================================================================= */
static void print_exitval_table(Block *b)/*{{{*/
{
  int i;
  extern char *prefix;
  char *defresult = get_defresult(exit_evaluator);

  if (prefix) {
    fprintf(output, "%s %s_exitval[] = {\n", get_result_type(exit_evaluator), prefix);
  } else {
    fprintf(output, "%s exitval[] = {\n", get_result_type(exit_evaluator));
  }
  for (i=0; i<ndfa; i++) {
    fprintf(output, "%s", (dfas[i]->result) ? dfas[i]->result : defresult);
    fputc ((i<(ndfa-1)) ? ',' : ' ', output);
    fprintf(output, " /* State %d */\n", i);
  }
  fprintf(output, "};\n\n");
}
/*}}}*/
static void print_attribute_table(void)/*{{{*/
{
  int i;
  extern char *prefix;
  char *defattr;

  if (evaluator_is_used(attr_evaluator)) {
    defattr = get_defresult(attr_evaluator);
    if (prefix) {
      fprintf(output, "%s %s_attribute[] = {\n", get_result_type(attr_evaluator), prefix);
    } else {
      fprintf(output, "%s attribute[] = {\n", get_result_type(attr_evaluator));
    }
    for (i=0; i<ndfa; i++) {
      char *av = dfas[i]->attribute;
      fprintf(output, "%s", av ? av : defattr);
      fputc ((i<(ndfa-1)) ? ',' : ' ', output);
      fprintf(output, " /* State %d */\n", i);
    }
    fprintf(output, "};\n\n");
  }

}
/*}}}*/
static void write_next_state_function_uncompressed(int Nt)/*{{{*/
{
  extern char *prefix;
  if (prefix) {
    fprintf(output, "int %s_next_state(int current_state, int next_token) {\n", prefix);
    fprintf(output, "  if (next_token < 0 || next_token >= %d) return -1;\n", Nt);
    fprintf(output, "  return %s_trans[%d*current_state + next_token];\n", prefix, Nt); 
    fprintf(output, "}\n");
  } else {
    fprintf(output, "int next_state(int current_state, int token) {\n");
    fprintf(output, "  if (next_token < 0 || next_token >= %d) return -1;\n", Nt);
    fprintf(output, "  return trans[%d*current_state + next_token];\n", Nt); 
    fprintf(output, "}\n");
  }
}
/*}}}*/
static void print_uncompressed_tables(Block *b)/*{{{*/
/* Print out the state/transition table uncompressed, i.e. every
   token has an array entry in every state.  This is fast to access
   but quite wasteful on memory with many states and many tokens. */
{
  int Nt = ntokens;
  int n, i, j;
  extern char *prefix;

  n = 0;
  if (prefix) {
    fprintf(output, "static short %s_trans[] = {", prefix);
  } else {
    fprintf(output, "static short trans[] = {");
  }
  for (i=0; i<ndfa; i++) {
    for (j=0; j<Nt; j++) {
      if (n>0) fputc (',', output);
      if (n%8 == 0) {
        fprintf(output, "\n  ");
      } else {
        fputc(' ', output);
      }
      n++;
      fprintf(output, "%4d", dfas[i]->map[j]);
    }
  }

  fprintf(output, "\n};\n\n");

  write_next_state_function_uncompressed(Nt);
  
}
/*}}}*/
static int check_include_char(int this_state, int token)/*{{{*/
{
  if (dfas[this_state]->defstate >= 0) {
    return (dfas[this_state]->map[token] !=
            dfas[dfas[this_state]->defstate]->map[token]);
  } else {
    return (dfas[this_state]->map[token] >= 0);
  }
}
/*}}}*/
static void write_next_state_function_compressed(void)/*{{{*/
/* Write the next_state function for traversing compressed tables into the
   output file. */
{
  extern char *prefix;
  if (prefix) {
    fprintf(output, "int %s_next_state(int current_state, int next_token) {\n", prefix);
    fprintf(output, "int h, l, m, xm;\n");
    fprintf(output, "while (current_state >= 0) {\n");
    fprintf(output, "  l = %s_base[current_state], h = %s_base[current_state+1];\n", prefix, prefix);
    fprintf(output, "  while (h > l) {\n");
    fprintf(output, "    m = (h + l) >> 1; xm = %s_token[m];\n", prefix);
    fprintf(output, "    if (xm == next_token) goto done;\n");
    fprintf(output, "    if (m == l) break;\n");
    fprintf(output, "    if (xm > next_token) h = m;\n");
    fprintf(output, "    else                 l = m;\n");
    fprintf(output, "  }\n");
    fprintf(output, "  current_state = %s_defstate[current_state];\n", prefix);
    fprintf(output, "}\n");
    fprintf(output, "return -1;\n");
    fprintf(output, "done:\n");
    fprintf(output, "return %s_nextstate[m];\n", prefix);
    fprintf(output, "}\n");
  } else {
    fprintf(output, "int next_state(int current_state, int token) {\n");
    fprintf(output, "int h, l, m, xm;\n");
    fprintf(output, "while (current_state >= 0) {\n");
    fprintf(output, "  l = base[current_state], h = base[current_state+1];\n");
    fprintf(output, "  while (h > l) {\n");
    fprintf(output, "    m = (h + l) >> 1; xm = token[m];\n");
    fprintf(output, "    if (xm == next_token) goto done;\n");
    fprintf(output, "    if (m == l) break;\n");
    fprintf(output, "    if (xm > next_token) h = m;\n");
    fprintf(output, "    else                 l = m;\n");
    fprintf(output, "  }\n");
    fprintf(output, "  current_state = defstate[current_state];\n");
    fprintf(output, "}\n");
    fprintf(output, "return -1;\n");
    fprintf(output, "done:\n");
    fprintf(output, "return nextstate[m];\n");
    fprintf(output, "}\n");
  }


}
/*}}}*/
static void print_compressed_tables(Block *b)/*{{{*/
/* Print state/transition table in compressed form.  This is more
   economical on storage, but requires a bisection search to find
   the next state for a given current state & token */
{
  int *basetab = new_array(int, ndfa+1);
  int Nt = ntokens;
  int n, i, j;
  extern char *prefix;


  n = 0;
  if (prefix) {
    fprintf(output, "static unsigned char %s_token[] = {", prefix);
  } else {
    fprintf(output, "static unsigned char token[] = {");
  }
  for (i=0; i<ndfa; i++) {
    for (j=0; j<Nt; j++) {
      if (check_include_char(i, j)) {
        if (n>0) fputc (',', output);
        if (n%8 == 0) {
          fprintf(output, "\n  ");
        } else {
          fputc(' ', output);
        }
        n++;
        fprintf(output, "%3d", j);
      }
    }
  }
  fprintf(output, "\n};\n\n");

  n = 0;
  if (prefix) {
    fprintf(output, "static short %s_nextstate[] = {", prefix);
  } else {
    fprintf(output, "static short nextstate[] = {");
  }
  for (i=0; i<ndfa; i++) {
    basetab[i] = n;
    for (j=0; j<Nt; j++) {
      if (check_include_char(i, j)) {
        if (n>0) fputc (',', output);
        if (n%8 == 0) {
          fprintf(output, "\n  ");
        } else {
          fputc(' ', output);
        }
        n++;
        fprintf(output, "%5d", dfas[i]->map[j]);
      }
    }
  }
  fprintf(output, "\n};\n\n");
  basetab[ndfa] = n;

  n = 0;
  if (prefix) {
    fprintf(output, "static unsigned short %s_base[] = {", prefix);
  } else {
    fprintf(output, "static unsigned short base[] = {");
  }
  for (i=0; i<=ndfa; i++) {
    if (n>0) fputc (',', output);
    if (n%8 == 0) {
      fprintf(output, "\n  ");
    } else {
      fputc(' ', output);
    }
    n++;
    fprintf(output, "%5d", basetab[i]);
  }
  fprintf(output, "\n};\n\n");
  
  n = 0;
  if (prefix) {
    fprintf(output, "static short %s_defstate[] = {", prefix);
  } else {
    fprintf(output, "static short defstate[] = {");
  }
  for (i=0; i<ndfa; i++) {
    if (n>0) fputc (',', output);
    if (n%8 == 0) {
      fprintf(output, "\n  ");
    } else {
      fputc(' ', output);
    }
    n++;
    fprintf(output, "%5d", dfas[i]->defstate);
  }
  fprintf(output, "\n};\n\n");
  
  free(basetab);

  write_next_state_function_compressed();
}
/*}}}*/
/* ================================================================= */
void yyerror (const char *s)/*{{{*/
{
  extern int lineno;
  fprintf(stderr, "%s at line %d\n", s, lineno);
}
/*}}}*/
int yywrap(void) /*{{{*/
{ 
  return -1;
}
/*}}}*/
/* ================================================================= */

int main (int argc, char **argv)
{
  int result;
  State *start_state;
  Block *main_block;

  char *input_name = NULL;
  char *output_name = NULL;
  char *report_name = NULL;
  int uncompressed_tables = 0;
  int uncompressed_dfa = 0; /* Useful for debug */
  verbose = 0;
  report = NULL;

  /*{{{ Parse cmd line arguments */
  while (++argv, --argc) {
    if (!strcmp(*argv, "-v") || !strcmp(*argv, "--verbose")) {
      verbose = 1;
    } else if (!strcmp(*argv, "-o") || !strcmp(*argv, "--output")) {
      ++argv, --argc;
      output_name = *argv;
    } else if (!strcmp(*argv, "-r") || !strcmp(*argv, "--report")) {
      ++argv, --argc;
      report_name = *argv;
    } else if (!strcmp(*argv, "-u") || !strcmp(*argv, "--uncompressed-tables")) {
      uncompressed_tables = 1;
    } else if (!strcmp(*argv, "-ud") || !strcmp(*argv, "--uncompressed-dfa")) {
      uncompressed_dfa = 1;
    } else if ((*argv)[0] == '-') {
      fprintf(stderr, "Unrecognized command line option %s\n", *argv);
    } else {
      input_name = *argv;
    }
  }
  /*}}}*/

  if (input_name) {/*{{{*/
    input = fopen(input_name, "r");
    if (!input) {
      fprintf(stderr, "Can't open %s for input, exiting\n", input_name);
      exit(1);
    }
  } else {
    input = stdin;
  }
  /*}}}*/
  if (output_name) {/*{{{*/
    output = fopen(output_name, "w");
    if (!output) {
      fprintf(stderr, "Can't open %s for writing, exiting\n", output_name);
      exit(1);
    }
  } else {
    output = stdout;
  }
/*}}}*/
  if (report_name) {/*{{{*/
    report = fopen(report_name, "w");
    if (!report) {
      fprintf(stderr, "Can't open %s for writing, no report will be created\n", report_name);
    }
  }
/*}}}*/

  if (verbose) {
    fprintf(stderr, "General-purpose automaton builder\n");
    fprintf(stderr, "Copyright (C) Richard P. Curnow  2000-2003\n");
  }

  eval_initialise();
  
  if (verbose) fprintf(stderr, "Parsing input...");
  yyin = input;
  
  /* Set yyout.  This means that if anything leaks from the scanner, or appears
     in a %{ .. %} block, it goes to the right place. */
  yyout = output; 
 
  result = yyparse();
  if (result > 0) exit(1);
  if (verbose) fprintf(stderr, "\n");

  start_state = get_curstate(); /* The last state to be current in the input file is the entry state of the NFA */
  main_block = start_state->parent;
  if (verbose) fprintf(stderr, "Computing epsilon closure...\n");
  generate_epsilon_closure(main_block);
  print_nfa(main_block);
#if 0
  if (verbose) fprintf(stderr, "Compressing NFA...\n");
  compress_nfa(main_block);
#endif
  build_transmap(main_block);
  if (verbose) fprintf(stderr, "Building DFA...\n");
  build_dfa(main_block, start_state->index);
  if (report) {
    fprintf(report, "--------------------------------\n"
                    "DFA structure before compression\n"
                    "--------------------------------\n");
  }
  print_dfa(main_block);
  
  if (had_ambiguous_result) {
    fprintf(stderr, "No output written, there were ambiguous exit values for accepting states\n");
    exit(2);
  }
  
  if (!uncompressed_dfa) {
    if (verbose) fprintf(stderr, "\nCompressing DFA...\n");
    ndfa = compress_dfa(dfas, ndfa, ntokens);
  }

  if (verbose) fprintf(stderr, "\nCompressing transition tables...\n");
  compress_transition_table(dfas, ndfa, ntokens);

  if (report) {
    fprintf(report, "-------------------------------\n"
                    "DFA structure after compression\n"
                    "-------------------------------\n");
  }
  if (verbose) fprintf(stderr, "Writing outputs...\n");
  print_dfa(main_block);

  print_exitval_table(main_block);
  print_attribute_table();

  if (uncompressed_tables) {
    print_uncompressed_tables(main_block);
  } else {
    print_compressed_tables(main_block);
  }

  if (report) {
    fclose(report);
    report = NULL;
  }
  
  return result;
}
