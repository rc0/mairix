/***************************************
  Routines for merging and prioritising exit tags and attribute tags
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

/* Handle boolean expressions used to determine the final scanner result from
   the set of NFA accepting states that are simultaneously active at the end of
   the scan.  */

#include "dfasyn.h"

struct Attr {
  char *attr; /* The string to write to the output file */
  /* The boolean expression that defines whether the attribute is active */
  Expr *e;
  /* If != 0, assume the state machine that the program's output is embedded in
     will exit immediately if this result occurs.  This may allow lots of
     states to be culled from the DFA. */
  int early;
};

typedef struct Attr Attr;
struct evaluator {
  Attr *attrs;
  int is_used; /* Set if any input rules reference this evaluator */
  int n_attrs;
  int max_attrs;
  char *name;
  char *defattr;
  char *attr_type;
};

Evaluator *default_evaluator;

struct evaluator_list {
  struct evaluator_list *next;
  Evaluator *evaluator;
};

static struct evaluator_list *evaluator_list = NULL;

/* Array pointer */
static struct evaluator **evaluators = NULL;
int n_evaluators = 0;

Evaluator* start_evaluator(const char *name)/*{{{*/
{
  Evaluator *x = NULL;
  struct evaluator_list *el;
  for (el=evaluator_list; el; el=el->next) {
    /* name is null for the default (anonymous) attribute group */
    const char *een = el->evaluator->name;
    if ((!een && !name) ||
        (een && name && !strcmp(een, name))) {
      x = el->evaluator;
      break;
    }
  }
  if (!x) {
    struct evaluator_list *nel;
    x = new(struct evaluator);
    x->attrs = NULL;
    x->is_used = 0;
    x->n_attrs = x->max_attrs = 0;
    x->name = name ? new_string(name) : NULL;
    x->defattr = NULL;
    x->attr_type = NULL;
    nel = new(struct evaluator_list);
    nel->next = evaluator_list;
    nel->evaluator = x;
    evaluator_list = nel;
  }
  return x;
}
/*}}}*/
void destroy_evaluator(Evaluator *x)/*{{{*/
{
  /* Just leak memory for now, no need to clean up. */
  return;
}
/*}}}*/
void define_defattr(Evaluator *x, char *text)/*{{{*/
{
  x = x ? x : default_evaluator;
  x->defattr = new_string(text);
  x->is_used = 1;
}
/*}}}*/
void define_type(Evaluator *x, char *text)/*{{{*/
{
  x = x ? x : default_evaluator;
  x->attr_type = new_string(text);
  x->is_used = 1;
}
/*}}}*/
char* get_defattr(int i)/*{{{*/
{
  Evaluator *x = evaluators[i];
  return x->defattr;
}
/*}}}*/
char* get_attr_type(int i)/*{{{*/
{
  Evaluator *x = evaluators[i];
  return x->attr_type ? x->attr_type : "short";
}
/*}}}*/
char* get_attr_name(int i)/*{{{*/
{
  Evaluator *x = evaluators[i];
  return x->name ? x->name : NULL;
}
/*}}}*/
static void grow_attrs(Evaluator *x)/*{{{*/
{
  if (x->n_attrs == x->max_attrs) {
    x->max_attrs += 32;
    x->attrs = resize_array(Attr, x->attrs, x->max_attrs);
  }
}
/*}}}*/

void define_attr(Evaluator *x, char *string, Expr *e, int early)/*{{{*/
/*++++++++++++++++++++
  Add a attr defn.  If the expr is null, it means build a single expr corr.
  to the value of the tag with the same name as the attr string.
  ++++++++++++++++++++*/
{
  Attr *r;

  x = x ? x : default_evaluator;

  x->is_used = 1;
  grow_attrs(x);
  r = &(x->attrs[x->n_attrs++]);
  r->attr = new_string(string);
  r->early = early;
  if (e) {
    r->e = e;
  } else {
    Expr *ne;
    ne = new_tag_expr(string);
    r->e = ne;
  }

  return;
}
/*}}}*/

void make_evaluator_array(void)/*{{{*/
{
  int n;
  struct evaluator_list *el;
  for (el=evaluator_list, n=0; el; el=el->next, n++) ;
  evaluators = new_array(struct evaluator *, n);
  n_evaluators = n;
  for (el=evaluator_list, n=0; el; el=el->next, n++) {
    evaluators[n] = el->evaluator;
  }
}
/*}}}*/
int evaluate_attrs(char ***attrs, int *attr_early)/*{{{*/
/*++++++++++++++++++++
  Evaluate the attr which holds given the tags that are set
  ++++++++++++++++++++*/
{
  int i, j;
  int status;

  if (attr_early) *attr_early = 0;
  status = 1;

  *attrs = new_array(char *, n_evaluators);

  for (j=0; j<n_evaluators; j++) {
    char **attr;
    struct evaluator *x;
    int any_attrs_so_far = 0;
    int matched = -1;

    attr = &(*attrs)[j];
    x = evaluators[j];

    for (i=0; i<x->n_attrs; i++) {
      if (eval(x->attrs[i].e)) {
        if (matched >= 0) {
          *attr = NULL;
          status = 0;
          break;
        } else {
          any_attrs_so_far = 1;
          matched = i;
        }
      }
    }
    if (matched < 0) {
      *attr = NULL;
    } else {
      *attr = x->attrs[matched].attr;
      if (attr_early) *attr_early |= x->attrs[matched].early;
    }
  }

  return status;
}
/*}}}*/
int evaluator_is_used(Evaluator *x)/*{{{*/
{
  return x->is_used;
}
/*}}}*/
void emit_dfa_attr_report(char **attrs, FILE *out)/*{{{*/
{
  int i;
  for (i=0; i<n_evaluators; i++) {
    if (attrs[i]) {
      const char *name = evaluators[i]->name;
      fprintf(out, "  Attributes for <%s> : %s\n",
          name ? name : "(DEFAULT)", attrs[i]);
    }
  }
}
/*}}}*/
/* Initialisation */
void eval_initialise(void)/*{{{*/
{
  default_evaluator = start_evaluator(NULL);
}
/*}}}*/
