/***************************************
  Header file for NFA->DFA conversion utility.
  ***************************************/

/*
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2001-2003,2005,2006
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
extern FILE *report;
extern FILE *output;
extern FILE *header_output;

/* Bison interface. */
extern FILE *yyin;
extern FILE *yyout;

extern int verbose;

extern char *prefix;

/* Temporary - this will be done better when the charclass stuff is
 * added. */
extern char **toktable;
extern int ntokens;

extern int n_charclasses;

extern int had_ambiguous_result;

extern int n_dfa_entries;
extern struct DFAEntry *dfa_entries;

struct State;
struct Block;
struct StimulusList;

struct Abbrev {/*{{{*/
  char *lhs; /* Defined name */
  struct StimulusList *stimuli;
#if 0
  char **rhs; /* Token/define */
  int nrhs;
  int maxrhs;
#endif
};
/*}}}*/

typedef enum StimulusType {/*{{{*/
  T_EPSILON,
  T_TOKEN,
  T_ABBREV,
  T_INLINEBLOCK,
  T_CHARCLASS
} StimulusType;
/*}}}*/
typedef struct InlineBlock {/*{{{*/
  char *type; /* Block type */
  char *in;   /* Name of input node */
  char *out;  /* Name of output node */
} InlineBlock;
/*}}}*/

#define ULONGS_PER_CC 8

typedef struct CharClass {/*{{{*/
  int is_used;
  unsigned long char_bitmap[ULONGS_PER_CC];
  unsigned long group_bitmap[ULONGS_PER_CC];
} CharClass;
/*}}}*/
typedef struct Stimulus {/*{{{*/
  StimulusType type;
  union {
    /* TODO : token should eventually become a struct ref ? */
    int token;
    struct Abbrev *abbrev;
    /* placeholders */
    InlineBlock *inline_block;
    CharClass *char_class;
  } x;
} Stimulus;
/*}}}*/
typedef struct StimulusList {/*{{{*/
  struct StimulusList *next;
  Stimulus *stimulus;
} StimulusList;
/*}}}*/
typedef enum TransType {/*{{{*/
  TT_EPSILON,
  TT_TOKEN,
  TT_CHARCLASS
} TransType;
/*}}}*/
typedef struct TransList {/*{{{*/
  struct TransList *next;
  TransType type;
  union {
    int token;
    CharClass *char_class;
  } x;
  char *ds_name;
  struct State *ds_ref;
} TransList;
/*}}}*/
typedef struct Stringlist {/*{{{*/
  struct Stringlist *next;
  char *string;
} Stringlist;
/*}}}*/

#if 0
typedef struct InlineBlockList {/*{{{*/
  struct InlineBlockList *next;
  InlineBlock *ib;
} InlineBlockList;
/*}}}*/
#endif

typedef struct State {/*{{{*/
  char *name;
  int index; /* Array index in containing block */
  struct Block *parent;
  TransList *transitions;
  Stringlist *tags;
  Stringlist *entries;

  /* Pointers to the nodes in the 'transitions' list, sorted into canonical order */
  TransList **ordered_trans;
  int n_transitions;

  unsigned char removed; /* Flag indicating state has been pruned by compression stage */
} State;
/*}}}*/
typedef struct S_Stateset {/*{{{*/
  State **states;
  int nstates;
  int maxstates;
} Stateset;
/*}}}*/
#define HASH_BUCKETS 64
#define HASH_MASK (HASH_BUCKETS-1)

typedef struct Block {/*{{{*/
  char *name;

  /* The master table of states within this block.  This has to be in a flat
     array because we have to work with respect to state indices when doing the
     2D bitmap stuff for the subset construction. */
  State **states;
  int nstates;
  int maxstates;

  /* epsilon closure for this block (treating it as a top-level block.) */
  unsigned long **eclo;

  /* Hash table for getting rapid access to a state within the block, given
     its name */
  Stateset state_hash[HASH_BUCKETS];

  int subcount; /* Number for generating substates */
  int subblockcount; /* Number for generating inline subblocks */
} Block;
/*}}}*/
struct Entrylist {/*{{{*/
  struct Entrylist *next;
  char *entry_name;
  State *state;
};
/*}}}*/
extern struct Entrylist *entries;

typedef struct DFANode {/*{{{*/
  unsigned long *nfas;
  unsigned long signature; /* All the longwords in the nfas array xor'ed together */
  int index; /* Entry's own index in the array */
  int *map; /* index by token code */
  int from_state; /* the state which provided the first transition to this one (leading to its creation) */
  int via_token; /* the token through which we got to this state the first time. */
  Stringlist *nfa_exit_sl; /* NFA exit values */
  Stringlist *nfa_attr_sl; /* NFA exit values */
  char **attrs;       /* Attributes, computed by boolean expressions defined in input text */
  int has_early_exit; /* If !=0, the scanner is expected to exit immediately this DFA state is entered.
                       It means that no out-bound transitions have to be created. */

  /* Fields calculated in compdfa.c */

  /* The equivalence class the state is in. */
  int eq_class;

  /* Temp. storage for the new eq. class within a single pass of the splitting alg. */
  int new_eq_class;

  /* Signature field from above is also re-used. */

  int is_rep; /* Set if state is chosen as the representative of its equivalence class. */
  int is_dead; /* Set if the state has no path to a non-default result */
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
/*}}}*/
struct DFAEntry {/*{{{*/
  char *entry_name;
  /* Initially the NFA number, overwritten with DFA number by build_dfa */
  int state_number;
};
/*}}}*/
struct DFA {/*{{{*/
  DFANode **s; /* states */
  int n;
  int max;

  /* the original block that the DFA comes from. */
  Block *b;
};
/*}}}*/

void yyerror(const char *s);
extern int yylex(void);

/* Constants for 'create' args */
#define USE_OLD_MUST_EXIST 0
#define CREATE_MUST_NOT_EXIST 1
#define CREATE_OR_USE_OLD 2

State *get_curstate(void);

struct Abbrev;
extern struct Abbrev * create_abbrev(const char *name, struct StimulusList *stimuli);

int lookup_token(char *name, int create);
Block *lookup_block(char *name, int create);
State *lookup_state(Block *in_block, char *name, int create);
void add_entry_to_state(State *curstate, const char *entry);
void define_entrystruct(const char *s, const char *v);
Stringlist * add_string_to_list(Stringlist *existing, const char *token);
void add_transitions(Block *curblock, State *curstate, StimulusList *stimuli, char *destination);
State * add_transitions_to_internal(Block *curblock, State *addtostate, StimulusList *stimuli);
void add_tags(State *curstate, Stringlist *sl);
InlineBlock *create_inline_block(char *type, char *in, char *out);
void instantiate_block(Block *curblock, char *block_name, char *instance_name);
void fixup_state_refs(Block *b);
void expand_charclass_transitions(Block *b);

void compress_nfa(Block *b);

extern void generate_epsilon_closure(Block *b);
extern void print_nfa(Block *b);
extern void build_transmap(Block *b);
extern struct DFA *build_dfa(Block *b);
extern void print_dfa(struct DFA *dfa);

/* In expr.c */
typedef struct Expr Expr;

Expr * new_not_expr(Expr *c);
Expr * new_and_expr(Expr *c1, Expr *c2);
Expr * new_or_expr(Expr *c1, Expr *c2);
Expr * new_xor_expr(Expr *c1, Expr *c2);
Expr * new_cond_expr(Expr *c1, Expr *c2, Expr *c3);
Expr * new_tag_expr(char *tag_name);
extern int eval(Expr *e);
void define_tag(char *name, Expr *e);
void clear_tag_values(void);
void report_unused_tags(void);

/* In evaluator.c */
typedef struct evaluator Evaluator;
extern int n_evaluators;
extern Evaluator *default_evaluator;
extern Evaluator *start_evaluator(const char *name);
void define_attr(Evaluator *x, char *string, Expr *e, int early);
void define_defattr(Evaluator *x, char *string);
void set_tag_value(char *tag_name);
int evaluate_attrs(char ***, int *);
int evaluator_is_used(Evaluator *x);
void define_defattr(Evaluator *x, char *text);
void define_type(Evaluator *x, char *text);
char* get_defattr(int i);
char* get_attr_type(int i);
char* get_attr_name(int i);
void make_evaluator_array(void);
void emit_dfa_attr_report(char **results, FILE *out);
void eval_initialise(void);

void compress_transition_table(struct DFA *dfa, int ntokens);
unsigned long increment(unsigned long x, int field);
unsigned long count_bits_set(unsigned long x);

/* in abbrevs.c */
struct Abbrev * lookup_abbrev(char *name);

/* in stimulus.c */
extern Stimulus *stimulus_from_epsilon(void);
extern Stimulus *stimulus_from_string(char *str);
extern Stimulus *stimulus_from_inline_block(InlineBlock *block);
extern Stimulus *stimulus_from_char_class(CharClass *char_class);
extern StimulusList *append_stimulus_to_list(StimulusList *existing, Stimulus *stim);

/* in charclass.c */
extern int cc_test_bit(const unsigned long *bitmap, int entry);
extern CharClass *new_charclass(void);
extern void free_charclass(CharClass *what);
extern void add_charclass_to_list(CharClass *cc);
extern void add_singleton_to_charclass(CharClass *towhat, char thechar);
extern void add_range_to_charclass(CharClass *towhat, char star, char end);
extern void invert_charclass(CharClass *what);
extern void diff_charclasses(CharClass *left, CharClass *right);
extern void split_charclasses(const Block *b);
extern void print_charclass_mapping(FILE *out, FILE *header_out, const char *prefix_under);
extern void print_charclass(FILE *out, int idx);

/* Return new number of DFA states */
extern void compress_dfa(struct DFA *dfa, int ntokens,
    int n_dfa_entries, struct DFAEntry *dfa_entries);

#endif /* N2D_H */

