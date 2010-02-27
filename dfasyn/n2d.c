/***************************************
  Convert NFA to DFA
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
  give the complete transition set.  A state may have 1 or more tags assigned
  (with =); this is the return value of the automaton if the end of string is
  encountered when in that state.
  }}} */

#include <ctype.h>
#include "dfasyn.h"
#include <assert.h>

/* Globally visible options to control reporting */
int verbose;

struct Entrylist *entries = NULL;

/* ================================================================= */
static inline int round_up(const int x) {/*{{{*/
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
static void transitively_close_eclo(unsigned long **eclo, int N)/*{{{*/
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
void generate_epsilon_closure(Block *b)/*{{{*/
{
  int i, j, N;

  N = b->nstates;
  b->eclo = new_array(unsigned long*, N);
  for (i=0; i<N; i++) {
    b->eclo[i] = new_array(unsigned long, round_up(N));
    for (j=0; j<round_up(N); j++) {
      b->eclo[i][j] = 0;
    }
  }

  /* Determine initial immediate transitions */
  for (i=0; i<N; i++) {
    State *s = b->states[i];
    TransList *tl;
    int from_state = s->index;
    set_bit(b->eclo[from_state], from_state); /* Always reflexive */

    for (tl=s->transitions; tl; tl=tl->next) {
      switch (tl->type) {
        case TT_EPSILON:
          {
            int to_state = tl->ds_ref->index;
            set_bit(b->eclo[from_state], to_state);
          }
          break;
        case TT_TOKEN:
          /* smoke out old method of indicating an epsilon trans */
          assert(tl->x.token >= 0);
          break;
        default:
          assert(0);
          break;
      }
    }
  }

  transitively_close_eclo(b->eclo, N);

}
/*}}}*/
void print_nfa(Block *b)/*{{{*/
{
  int i, j, N;
  N = b->nstates;

  if (!report) return;

  for (i=0; i<N; i++) {
    State *s = b->states[i];
    TransList *tl;
    Stringlist *sl;
    fprintf(report, "NFA state %d = %s", i, s->name);
    if (s->entries) {
      int first = 1;
      Stringlist *e = s->entries;
      fputs(" [Entries: ", report);
      while (e) {
        if (!first) {
          fputc(',', report);
        }
        first = 0;
        fputs(e->string, report);
        e = e->next;
      }
      fputc(']', report);
    }
    fputc('\n', report);
    for (tl=s->transitions; tl; tl=tl->next) {
      switch (tl->type) {
        case TT_EPSILON:
          fprintf(report, "  [(epsilon)] -> ");
          break;
        case TT_TOKEN:
          assert(tl->x.token >= 0);
          if (tl->x.token >= ntokens) {
            fprintf(report, "   ");
            print_charclass(report, tl->x.token - ntokens);
            fprintf(report, " -> ");
          } else {
            fprintf(report, "  %s -> ", toktable[tl->x.token]);
          }
          break;
        default:
          assert(0);
          break;
      }
      fprintf(report, "%s\n", tl->ds_name);
    }
    if (s->tags) {
      int first = 1;
      fprintf(report, "  Tags : ");
      for (sl=s->tags; sl; sl=sl->next) {
        fprintf(report, "%s%s",
                first ? "" : "|",
                sl->string);
      }
      fprintf(report, "\n");
    }
    fprintf(report, "  Epsilon closure :\n    (self)\n");
    for (j=0; j<N; j++) {
      if (i!=j && is_set(b->eclo[i], j)) {
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
void build_transmap(Block *b)/*{{{*/
{
  int N = b->nstates;
  int Nt = ntokens + n_charclasses;
  int i, j, k, m, dest;

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
    TransList *tl;
    for (tl=s->transitions; tl; tl=tl->next) {
      switch (tl->type) {
        case TT_EPSILON:
          break;
        case TT_TOKEN:
          {
            assert(tl->x.token >= 0);
            dest = tl->ds_ref->index;
            for (m=0; m<round_up(N); m++) {
              unsigned long x = b->eclo[dest][m];
              transmap[i][tl->x.token][m] |= x;
              if (!!x) set_bit(anytrans[i], tl->x.token);
            }
          }
          break;
        default:
          assert(0);
          break;
      }
    }
  }
}
/*}}}*/
/* ================================================================= */

int had_ambiguous_result = 0;

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

int n_dfa_entries;
struct DFAEntry *dfa_entries = NULL;

/* ================================================================= */
static void grow_dfa(struct DFA *dfa)/*{{{*/
{
  dfa->max += 32;
  dfa->s = resize_array(DFANode*, dfa->s, dfa->max);
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

/*{{{ add_dfa() */
static int add_dfa(Block *b, struct DFA *dfa, unsigned long *nfas, int N, int Nt, int from_state, int via_token)
{
  int j;
  int result = dfa->n;
  int this_result_unambiguous;

  Stringlist *ex;
  unsigned long signature = 0UL, folded_signature;
  struct DFAList *dfal;

  if (verbose) {
    fprintf(stderr, "Adding DFA state %d\r", dfa->n);
    fflush(stderr);
  }

  if (dfa->max == dfa->n) {
    grow_dfa(dfa);
  }

  dfa->s[dfa->n] = new(DFANode);
  dfa->s[dfa->n]->nfas = new_array(unsigned long, round_up(N));
  dfa->s[dfa->n]->map = new_array(int, Nt);
  for (j=0; j<Nt; j++) dfa->s[dfa->n]->map[j] = -1;
  dfa->s[dfa->n]->index = dfa->n;
  dfa->s[dfa->n]->defstate = -1;

  dfa->s[dfa->n]->from_state = from_state;
  dfa->s[dfa->n]->via_token = via_token;

  for (j=0; j<round_up(N); j++) {
    unsigned long x = nfas[j];
    signature ^= x;
    dfa->s[dfa->n]->nfas[j] = x;
  }
  dfa->s[dfa->n]->signature = signature;

  folded_signature = fold_signature(signature);
  dfal = new(struct DFAList);
  dfal->dfa = dfa->s[dfa->n];
  dfal->next = dfa_hashtable[folded_signature];
  dfa_hashtable[folded_signature] = dfal;

  /* {{{ Boolean reductions to get attributes */
  ex = NULL;
  clear_tag_values();
  for (j=0; j<N; j++) {
    if (is_set(dfa->s[dfa->n]->nfas, j)) {
      Stringlist *sl;
      State *s = b->states[j];
      for (sl = s->tags; sl; sl = sl->next) {
        Stringlist *new_sl;
        new_sl = new(Stringlist);
        new_sl->string = sl->string;
        new_sl->next = ex;
        ex = new_sl;

        set_tag_value(sl->string);
      }
    }
  }

  dfa->s[dfa->n]->nfa_exit_sl = ex;

  this_result_unambiguous =
    evaluate_attrs(&dfa->s[dfa->n]->attrs, &dfa->s[dfa->n]->has_early_exit);

  if (!this_result_unambiguous) {
    Stringlist *sl;
    fprintf(stderr, "WARNING : Ambiguous exit state abandoned for DFA state %d\n", dfa->n);
    fprintf(stderr, "NFA exit tags applying in this stage :\n");
    for (sl = ex; sl; sl = sl->next) {
      fprintf(stderr, "  %s\n", sl->string);
    }
    had_ambiguous_result = 1;
  }
  /*}}}*/

  ++dfa->n;
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
struct DFA *build_dfa(Block *b)/*{{{*/
{
  unsigned long **nfas;
  int i;
  int j;
  int N, Nt;
  int next_to_do;
  int *found_any;
  int rup_N;
  struct DFA *dfa;

  dfa = new(struct DFA);
  dfa->n = 0;
  dfa->max = 0;
  dfa->s = NULL;
  dfa->b = b;

  for (i=0; i<DFA_HASHSIZE; i++) dfa_hashtable[i] = NULL;

  N = b->nstates;
  rup_N = round_up(N);
  Nt = ntokens + n_charclasses;

  nfas = new_array(unsigned long *, Nt);
  for (i=0; i<Nt; i++) {
    nfas[i] = new_array(unsigned long, round_up(N));
  }

  /* Add initial states */
  for (j=0; j<n_dfa_entries; j++) {
    int idx;
    clear_nfas(nfas[0], N);
    for (i=0; i<round_up(N); i++) {
      nfas[0][i] |= b->eclo[dfa_entries[j].state_number][i];
    }
    /* Must handle the case where >=2 of the start states are actually identical;
     * nothing in the input language prevents this. */
    idx = find_dfa(nfas[0], N);
    if (idx < 0) {
      idx = dfa->n;
      add_dfa(b, dfa, nfas[0], N, Nt, -1, -1);
    }
    dfa_entries[j].state_number = idx;
  }

  next_to_do = 0;
  found_any = new_array(int, Nt);

  /* Now the heart of the program : the subset construction to turn the NFA
     into a DFA.  This is a major performance hog in the program, so there are
     lots of tricks to speed this up (particularly, hoisting intermediate
     pointer computations out of the loop to assert the fact that there is no
     aliasing between the arrays.) */

  while (next_to_do < dfa->n) {

    int t; /* token index */
    int j0, j0_5, j1, j, mask, k;
    int idx;
    unsigned long *current_nfas;
    unsigned long block_bitmap;

    /* If the next DFA state has the result_early flag set, it means that the scanner will
     * always exit straight away when that state is reached, so there's no need to compute
     * any transitions out of it. */

    if (dfa->s[next_to_do]->has_early_exit) {
      next_to_do++;
      continue;
    }

    for (j=0; j<Nt; j++) {
      clear_nfas(nfas[j], N);
      found_any[j] = 0;
    }

    current_nfas = dfa->s[next_to_do]->nfas;
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
          idx = add_dfa(b, dfa, nfas[t], N, Nt, next_to_do, t);
        }
      } else {
        idx = -1;
      }
      dfa->s[next_to_do]->map[t] = idx;
    }

    next_to_do++;
  }

  free(found_any);
  for (i=0; i<Nt; i++) free(nfas[i]);
  free(nfas);
  return dfa;
}
/*}}}*/
/* ================================================================= */
static void display_route(struct DFA *dfa, int idx, FILE *out)/*{{{*/
{
  int from_state, via_token;
  from_state = dfa->s[idx]->from_state;
  if (from_state >= 0) {
    display_route(dfa, from_state, out);
    fputs("->", out);
  }

  via_token = dfa->s[idx]->via_token;
  if (via_token >= ntokens) {
    print_charclass(out, via_token - ntokens);
  } else if (via_token >= 0) {
    fprintf(out, "%s", toktable[via_token]);
  }
}
/*}}}*/
void print_dfa(struct DFA *dfa)/*{{{*/
{
  int N = dfa->b->nstates;
  int Nt = ntokens + n_charclasses;

  int i, j0, j0_5, j1, t;
  unsigned long mask;
  unsigned long current_nfas;
  int rup_N = round_up(N);
  int from_state, this_state;

  if (!report) return;

  for (i=0; i<dfa->n; i++) {
    fprintf(report, "DFA state %d\n", i);
    if (dfa->s[i]->nfas) {
      fprintf(report, "  NFA states :\n");
      for (j0=0; j0<rup_N; j0++) {
        current_nfas = dfa->s[i]->nfas[j0];
        if (!current_nfas) continue;
        j0_5 = j0<<5;
        for (j1=0, mask=1UL; j1<32; mask<<=1, j1++) {
          if (current_nfas & mask) {
            fprintf(report, "    %s\n", dfa->b->states[j0_5 + j1]->name);
          }
        }
      }
      fprintf(report, "\n");
    }
    fprintf(report, "  Forward route :");
    this_state = i;
    from_state = dfa->s[i]->from_state;
    if (from_state >= 0) {
      fprintf(report, " (from state %d)", from_state);
    }
    fputs("\n   (START)", report);
    display_route(dfa, i, report);
    fputs("->(HERE)", report);
    fprintf(report, "\n");

    fprintf(report, "  Transitions :\n");
    for (t=0; t<Nt; t++) {
      int dest = dfa->s[i]->map[t];
      if (dest >= 0) {
        if (t >= ntokens) {
          fprintf(report, "    ");
          print_charclass(report, t - ntokens);
          fprintf(report, " -> %d\n", dest);
        } else {
          fprintf(report, "    %s -> %d\n", toktable[t], dest);
        }
      }
    }
    if (dfa->s[i]->defstate >= 0) {
      fprintf(report, "  Use state %d as basis (%d fixups)\n",
              dfa->s[i]->defstate, dfa->s[i]->best_diff);
    }
    if (dfa->s[i]->nfa_exit_sl) {
      Stringlist *sl;
      fprintf(report, "  NFA exit tags applying :\n");
      for (sl=dfa->s[i]->nfa_exit_sl; sl; sl = sl->next) {
        fprintf(report, "    %s\n", sl->string);
      }
    }

    emit_dfa_attr_report(dfa->s[i]->attrs, report);
    fprintf(report, "\n");
  }
  fprintf(report, "\nEntry states in DFA:\n");
  for (i=0; i<n_dfa_entries; i++) {
    fprintf(report, "Entry <%s> : %d\n",
        dfa_entries[i].entry_name,
        dfa_entries[i].state_number);
  }

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

