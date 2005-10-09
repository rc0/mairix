/***************************************
  $Header: /cvs/src/dfasyn/expr.c,v 1.3 2003/03/03 00:05:41 richard Exp $

  Routines for merging and prioritising exit tags and attribute tags
  ***************************************/

/* 
 **********************************************************************
 * Copyright (C) Richard P. Curnow  2001-2003
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

/* Handle boolean expressions used to determine the final scanner result from
   the set of NFA accepting states that are simultaneously active at the end of
   the scan.  */

#include "n2d.h"

enum ExprType {
  E_AND, E_OR, E_XOR, E_COND, E_NOT, E_WILD, E_SYMBOL
};

struct Symbol;

struct Expr {
  enum ExprType type;
  union {
    struct { struct Expr *c1, *c2; } and; 
    struct { struct Expr *c1, *c2; } or; 
    struct { struct Expr *c1, *c2; } xor; 
    struct { struct Expr *c1, *c2, *c3; } cond; 
    struct { struct Expr *c1; } not; 
    struct { int pad; } wild; 
    struct { char *name; struct Symbol *s; } symbol;
  } data;
};

struct Symbol {
  char *name;
  int is_expr;
  union {
    Expr *e;
    int val;
  } data;
};

struct SymbolList {
  struct SymbolList *next;
  struct Symbol *sym;
};

struct Result {
  char *result; /* The string to write to the output file */
  /* The boolean expression that defines whether the result is active */
  Expr *e;
  /* If != 0, assume the state machine that the program's output is embedded in
     will exit immediately if this result occurs.  This may allow lots of
     states to be culled from the DFA. */
  int early; 
};

typedef struct Result Result;
typedef struct Symbol Symbol;
typedef struct SymbolList SymbolList;

struct evaluator {
  SymbolList *symbols;
  Result *results;
  int is_used; /* Set if any input rules reference this evaluator */
  int n_results;
  int max_results;
  /* Flag indicating whether any results evaluated so far have evaluated true.
     (Used for implementing wildcard expression).  */
  int any_results_so_far;
  char *name;
  char *defresult;
  char *result_type;
};

/* Evaluator used to determine exit value of automaton, if the last input
 * char appears in a particular state */
Evaluator *exit_evaluator;

/* Evaluator used to determine attribute to apply to a DFA state, given those
 * that apply to its constituent NFA states. */
Evaluator *attr_evaluator;

Evaluator* create_evaluator(char *name)/*{{{*/
{
  Evaluator *x = new(struct evaluator);
  x->symbols = NULL;
  x->results = NULL;
  x->is_used = 0;
  x->n_results = x->max_results = 0;
  x->any_results_so_far = 0;
  x->name = new_string(name);
  x->defresult = NULL;
  x->result_type = NULL;
  return x;
}
/*}}}*/
void destroy_evaluator(Evaluator *x)/*{{{*/
{
  /* Just leak memory for now, no need to clean up. */
  return;
}
/*}}}*/
void define_defresult(Evaluator *x, char *text)/*{{{*/
{
  x->defresult = new_string(text);
  x->is_used = 1;
}
/*}}}*/
void define_type(Evaluator *x, char *text)/*{{{*/
{
  x->result_type = new_string(text); 
  x->is_used = 1;
}
/*}}}*/
char* get_defresult(Evaluator *x)/*{{{*/
{
  if (x->defresult) {
    return x->defresult;
  } else {
    fprintf(stderr, "WARNING: Default %s used with no definition, \"0\" assumed\n", x->name);
    return "0";
  }
}
/*}}}*/
char* get_result_type(Evaluator *x)/*{{{*/
{
  return x->result_type ? x->result_type : "short";
}
/*}}}*/
static void add_new_symbol(Evaluator *x, Symbol *s)/*{{{*/
{
  SymbolList *nsl = new(SymbolList);
  nsl->sym = s;
  nsl->next = x->symbols;
  x->symbols = nsl;
}
  /*}}}*/
static void grow_results(Evaluator *x)/*{{{*/
{
  if (x->n_results == x->max_results) {
    x->max_results += 32;
    x->results = resize_array(Result, x->results, x->max_results);
  }
}
/*}}}*/

Expr * new_wild_expr(void)/*{{{*/
{
  Expr *r = new(Expr);
  r->type = E_WILD;
  return r; 
}
/*}}}*/
Expr * new_not_expr(Expr *c)/*{{{*/
{
  Expr *r = new(Expr);
  r->type = E_NOT;
  r->data.not.c1 = c;
  return r; 
}
/*}}}*/
Expr * new_and_expr(Expr *c1, Expr *c2)/*{{{*/
{
  Expr *r = new(Expr);
  r->type = E_AND;
  r->data.and.c1 = c1;
  r->data.and.c2 = c2;
  return r; 
}
/*}}}*/
Expr * new_or_expr(Expr *c1, Expr *c2)/*{{{*/
{
  Expr *r = new(Expr);
  r->type = E_OR;
  r->data.or.c1 = c1;
  r->data.or.c2 = c2;
  return r; 
}
/*}}}*/
Expr * new_xor_expr(Expr *c1, Expr *c2)/*{{{*/
{
  Expr *r = new(Expr);
  r->type = E_XOR;
  r->data.xor.c1 = c1;
  r->data.xor.c2 = c2;
  return r; 
}
/*}}}*/
Expr * new_cond_expr(Expr *c1, Expr *c2, Expr *c3)/*{{{*/
{
  Expr *r = new(Expr);
  r->type = E_COND;
  r->data.cond.c1 = c1;
  r->data.cond.c2 = c2;
  r->data.cond.c3 = c3;
  return r; 
}
/*}}}*/
static Symbol *  find_symbol_or_create(Evaluator *x, char *sym_name)/*{{{*/
{
  Symbol *s;
  SymbolList *sl;
  for (sl=x->symbols; sl; sl=sl->next) {
    s = sl->sym;
    if (!strcmp(s->name, sym_name)) {
      return s;
    }
  }
  
  s = new(Symbol);
  add_new_symbol(x,s);
  s->is_expr = 0; /* Until proven otherwise */
  s->name = new_string(sym_name);
  return s;
}
/*}}}*/

Expr * new_sym_expr(char *sym_name)/*{{{*/
/* Return expr for symbol name if it already exist, else create.  Don't bind to
   actual symbol instance yet.  At the stage of parsing where this function is
   used, we don't know yet which symbol table the symbol has to exist in.  */
{
  Expr *r;

  r = new(Expr);
  r->type = E_SYMBOL;
  r->data.symbol.name = new_string(sym_name);
  r->data.symbol.s = NULL; /* Force binding at first use */
  return r; 
}
/*}}}*/
void define_result(Evaluator *x, char *string, Expr *e, int early)/*{{{*/
/*++++++++++++++++++++
  Add a result defn.  If the expr is null, it means build a single expr corr.
  to the value of the symbol with the same name as the result string.
  ++++++++++++++++++++*/
{
  Result *r;

  x->is_used = 1;
  grow_results(x);
  r = &(x->results[x->n_results++]);
  r->result = new_string(string);
  r->early = early;
  if (e) {
    r->e = e;
  } else {
    Expr *ne;
    ne = new_sym_expr(string);
    r->e = ne;
  }

  return;
}
/*}}}*/
void define_symbol(Evaluator *x, char *name, Expr *e)/*{{{*/
/*++++++++++++++++++++
  Define an entry in the symbol table.
  ++++++++++++++++++++*/
{
  Symbol *s;
  x->is_used = 1;
  s = find_symbol_or_create(x, name);
  s->data.e = e;
  s->is_expr = 1;
  return;
}
/*}}}*/
  
void define_symresult(Evaluator *x, char *name, Expr *e, int early)/*{{{*/
/*++++++++++++++++++++
  Define an entry in the symbol table, and a result with the same name.
  ++++++++++++++++++++*/
{
  x->is_used = 1;
  define_symbol(x, name, e);
  define_result(x, name, e, early);
  return;
}
/*}}}*/
void clear_symbol_values(Evaluator *x)/*{{{*/
{
  SymbolList *sl;
  for (sl=x->symbols; sl; sl=sl->next) {
    Symbol *s = sl->sym;
    if (0 == s->is_expr) {
      s->data.val = 0;
    }
  }
  x->any_results_so_far = 0;
}
/*}}}*/
void set_symbol_value(Evaluator *x, char *sym_name)/*{{{*/
{
  Symbol *s;

  s = find_symbol_or_create(x, sym_name);
  if (s->is_expr) {
    fprintf(stderr, "Cannot set value for symbol '%s', it is defined by an expression\n", s->name);
    exit(2);
  } else {
    s->data.val = 1;
  }
}
/*}}}*/
static int eval(Evaluator *x, Expr *e)/*{{{*/
/*++++++++++++++++++++
  Evaluate the value of an expr
  ++++++++++++++++++++*/
{
  switch (e->type) {
    case E_AND:
      return eval(x, e->data.and.c1) && eval(x, e->data.and.c2);
    case E_OR:
      return eval(x, e->data.or.c1) || eval(x, e->data.or.c2);
    case E_XOR:
      return eval(x, e->data.xor.c1) ^ eval(x, e->data.xor.c2);
    case E_COND:
      return eval(x, e->data.cond.c1) ? eval(x, e->data.cond.c2) : eval(x, e->data.cond.c3);
    case E_NOT:
      return !eval(x, e->data.not.c1);
    case E_WILD:
      return x->any_results_so_far;
    case E_SYMBOL:
      {
        Symbol *s = e->data.symbol.s;
        if (!s) {
          /* Not bound yet */
          e->data.symbol.s = s = find_symbol_or_create(x, e->data.symbol.name);
        }
        if (s->is_expr) {
          return eval(x, s->data.e);
        } else {
          return s->data.val;
        }
      }
    default:
      fprintf(stderr, "Interal error : Can't get here!\n");
      exit(2);
  }
}
/*}}}*/
int evaluate_result(Evaluator *x, char **result, int *result_early)/*{{{*/
/*++++++++++++++++++++
  Evaluate the result which holds given the symbols that are set
  ++++++++++++++++++++*/
{
  int i;
  int matched = -1;
  for (i=0; i<x->n_results; i++) {
    if (eval(x, x->results[i].e)) {
      if (x->any_results_so_far) {
        *result = NULL;
        return 0;
      } else {
        x->any_results_so_far = 1;
        matched = i;
      }
    }
  }

  if (matched < 0) {
    *result = NULL;
    if (result_early) *result_early = 0;
    return 1;
  } else {
    *result = x->results[matched].result;
    if (result_early) *result_early = x->results[matched].early;
    return 1;
  }
}
/*}}}*/
int evaluator_is_used(Evaluator *x)/*{{{*/
{
  return x->is_used;
}
/*}}}*/
/* Initialisation */
void eval_initialise(void)/*{{{*/
{
  exit_evaluator = create_evaluator("result");
  attr_evaluator = create_evaluator("attribute");
}
/*}}}*/
