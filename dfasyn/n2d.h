/***************************************
  $Header: /cvs/src/dfasyn/n2d.h,v 1.2 2003/03/02 23:42:11 richard Exp $

  Header file for NFA->DFA conversion utility.
  ***************************************/

/* 
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2001-2003,2005
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

#ifndef N2D_H
#define N2D_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define new(T) ((T *) malloc(sizeof(T)))
#define new_array(T,N) ((T *) malloc((N) * sizeof(T)))
#define resize_array(T,arr,newN) ((T *) ((arr) ? realloc(arr,(newN)*sizeof(T)) : malloc((newN)*sizeof(T))))
#define new_string(s) strcpy((char *)malloc((strlen(s)+1)*sizeof(char)),s)

/* For typecasting, especially useful for declarations of local ptrs to args
   of a qsort comparison fn */
#define Castdecl(x, T, nx) T nx = (T) x

#define Castderef(x, T, nx) T nx = *(T*) x

/* Globally visible options to control reporting */
extern FILE *report;
extern int verbose;

struct State;
struct Block;

typedef struct Translist {
  struct Translist *next;
  int token;
  char *ds_name;
  struct State *ds_ref;
} Translist;

typedef struct Stringlist {
  struct Stringlist *next;
  char *string;
} Stringlist;

typedef struct InlineBlock {
  char *type; /* Block type */
  char *in;   /* Name of input node */
  char *out;  /* Name of output node */
} InlineBlock;

typedef struct InlineBlockList {
  struct InlineBlockList *next;
  InlineBlock *ib;
} InlineBlockList;
  
typedef struct State {
  char *name;
  int index; /* Array index in containing block */
  struct Block *parent;
  Translist *transitions;
  Stringlist *exitvals;
  Stringlist *attributes;

  /* Pointers to the nodes in the 'transitions' list, sorted into canonical order */
  Translist **ordered_trans;
  int n_transitions;

  unsigned char removed; /* Flag indicating state has been pruned by compression stage */
} State;

typedef struct S_Stateset {
  State **states;
  int nstates;
  int maxstates;
} Stateset;

#define HASH_BUCKETS 64
#define HASH_MASK (HASH_BUCKETS-1)

typedef struct Block {
  char *name;

  /* The master table of states within this block.  This has to be in a flat
     array because we have to work with respect to state indices when doing the
     2D bitmap stuff for the subset construction. */
  State **states;
  int nstates;
  int maxstates;
  
  /* Hash table for getting rapid access to a state within the block, given
     its name */
  Stateset state_hash[HASH_BUCKETS];
  
  int subcount; /* Number for generating substates */
  int subblockcount; /* Number for generating inline subblocks */
} Block;

typedef struct {
  unsigned long *nfas;
  unsigned long signature; /* All the longwords in the nfas array xor'ed together */
  int index; /* Entry's own index in the array */
  int *map; /* index by token code */
  int from_state; /* the state which provided the first transition to this one (leading to its creation) */
  int via_token; /* the token through which we got to this state the first time. */
  Stringlist *nfa_exit_sl; /* NFA exit values */
  Stringlist *nfa_attr_sl; /* NFA exit values */
  char *result;    /* Result token, computed by boolean expressions defined in input text */
  int result_early; /* If !=0, the scanner is expected to exit immediately this DFA state is entered.
                       It means that no out-bound transitions have to be created. */
  char *attribute; /* Attribute token, computed by boolean expressions defined in input text */

  /* Fields calculated in compdfa.c */
  
  /* The equivalence class the state is in. */
  int eq_class;

  /* Temp. storage for the new eq. class within a single pass of the splitting alg. */
  int new_eq_class; 

  /* Signature field from above is also re-used. */

  int is_rep; /* Set if state is chosen as the representative of its equivalence class. */
  int new_index; /* New index assigned to the state. */

  /* Fields calculated in tabcompr.c */
  
  unsigned long transition_sig;

  /* Default state, i.e. the one that supplies transitions for tokens not
     explicitly listed for this one. */
  int defstate; 

  /* Number of transitions that this state has different to those in the
     default state. */
  int best_diff; 

} DFANode;


void yyerror(const char *s);
extern int yylex(void);

/* Constants for 'create' args */  
#define USE_OLD_MUST_EXIST 0
#define CREATE_MUST_NOT_EXIST 1
#define CREATE_OR_USE_OLD 2

State *get_curstate(void);

struct Abbrev;
extern struct Abbrev * create_abbrev(char *name);
extern void add_tok_to_abbrev(struct Abbrev *abbrev, char *tok);

int lookup_token(char *name, int create);
Block *lookup_block(char *name, int create);
State *lookup_state(Block *in_block, char *name, int create);
Stringlist * add_token(Stringlist *existing, char *token);
void add_transitions(State *curstate, Stringlist *tokens, char *destination);
State * add_transitions_to_internal(Block *curblock, State *addtostate, Stringlist *tokens);
void add_exit_value(State *curstate, char *value);
void set_state_attribute(State *curstate, char *name);
InlineBlock *create_inline_block(char *type, char *in, char *out);
InlineBlockList *add_inline_block(InlineBlockList *existing, InlineBlock *nib);
State * add_inline_block_transitions(Block *curblock, State *addtostate, InlineBlockList *ibl);
void instantiate_block(Block *curblock, char *block_name, char *instance_name);
void fixup_state_refs(Block *b);

void compress_nfa(Block *b);

/* In expr.c */
typedef struct Expr Expr;

typedef struct evaluator Evaluator;
extern Evaluator *exit_evaluator;
extern Evaluator *attr_evaluator;

Expr * new_wild_expr(void);
Expr * new_not_expr(Expr *c);
Expr * new_and_expr(Expr *c1, Expr *c2);
Expr * new_or_expr(Expr *c1, Expr *c2);
Expr * new_xor_expr(Expr *c1, Expr *c2);
Expr * new_cond_expr(Expr *c1, Expr *c2, Expr *c3);
Expr * new_sym_expr(char *sym_name);

void define_symbol(Evaluator *x, char *name, Expr *e);
void define_result(Evaluator *x, char *string, Expr *e, int early);
void define_symresult(Evaluator *x, char *string, Expr *e, int early);
void define_defresult(Evaluator *x, char *string);
void clear_symbol_values(Evaluator *x);
void set_symbol_value(Evaluator *x, char *sym_name);
int evaluate_result(Evaluator *x, char **, int *);
int evaluator_is_used(Evaluator *x);
void define_defresult(Evaluator *x, char *text);
void define_type(Evaluator *x, char *text);
char* get_defresult(Evaluator *x);
char* get_result_type(Evaluator *x);
void eval_initialise(void);

void compress_transition_table(DFANode **dfas, int ndfas, int ntokens);
unsigned long increment(unsigned long x, int field);
unsigned long count_bits_set(unsigned long x);

/* Return new number of DFA states */
int compress_dfa(DFANode **dfas, int ndfas, int ntokens);

#endif /* N2D_H */

