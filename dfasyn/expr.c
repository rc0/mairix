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

enum ExprType {
  E_AND, E_OR, E_XOR, E_COND, E_NOT, E_TAG
};

struct Tag;

struct Expr {
  enum ExprType type;
  union {
    struct { struct Expr *c1, *c2; } and;
    struct { struct Expr *c1, *c2; } or;
    struct { struct Expr *c1, *c2; } xor;
    struct { struct Expr *c1, *c2, *c3; } cond;
    struct { struct Expr *c1; } not;
    struct { char *name; struct Tag *s; } tag;
  } data;
};

struct Tag {
  char *name;
  int is_expr;
  union {
    Expr *e;
    int val;
  } data;
  int is_used;
};

struct TagList {
  struct TagList *next;
  struct Tag *tag;
};

typedef struct Tag Tag;
typedef struct TagList TagList;

static TagList *tags = NULL;

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

Expr * new_tag_expr(char *tag_name)/*{{{*/
/* Return expr for tag name if it already exist, else create.  Don't bind to
   actual tag instance yet.  At the stage of parsing where this function is
   used, we don't know yet which tag table the tag has to exist in.  */
{
  Expr *r;

  r = new(Expr);
  r->type = E_TAG;
  r->data.tag.name = new_string(tag_name);
  r->data.tag.s = NULL; /* Force binding at first use */
  return r;
}
/*}}}*/
static void add_new_tag(Tag *s)/*{{{*/
{
  TagList *nsl = new(TagList);
  nsl->tag = s;
  nsl->next = tags;
  tags = nsl;
}
  /*}}}*/
static Tag *  find_tag_or_create(char *tag_name)/*{{{*/
{
  Tag *s;
  TagList *sl;
  for (sl=tags; sl; sl=sl->next) {
    s = sl->tag;
    if (!strcmp(s->name, tag_name)) {
      return s;
    }
  }

  s = new(Tag);
  add_new_tag(s);
  s->is_expr = 0; /* Until proven otherwise */
  s->data.val = 0; /* Force initial value to be well-defined */
  s->name = new_string(tag_name);
  s->is_used = 0;
  return s;
}
/*}}}*/
void define_tag(char *name, Expr *e)/*{{{*/
/*++++++++++++++++++++
  Define an entry in the tag table.
  ++++++++++++++++++++*/
{
  Tag *s;
  s = find_tag_or_create(name);
  s->data.e = e;
  s->is_expr = 1;
  return;
}
/*}}}*/

void clear_tag_values(void)/*{{{*/
{
  TagList *sl;
  for (sl=tags; sl; sl=sl->next) {
    Tag *s = sl->tag;
    if (0 == s->is_expr) {
      s->data.val = 0;
    }
  }
}
/*}}}*/
void set_tag_value(char *tag_name)/*{{{*/
{
  Tag *s;

  s = find_tag_or_create(tag_name);
  if (s->is_expr) {
    fprintf(stderr, "Cannot set value for tag '%s', it is defined by an expression\n", s->name);
    exit(2);
  } else {
    s->data.val = 1;
  }
}
/*}}}*/
int eval(Expr *e)/*{{{*/
/*++++++++++++++++++++
  Evaluate the value of an expr
  ++++++++++++++++++++*/
{
  switch (e->type) {
    case E_AND:
      return eval(e->data.and.c1) && eval(e->data.and.c2);
    case E_OR:
      return eval(e->data.or.c1) || eval(e->data.or.c2);
    case E_XOR:
      return eval(e->data.xor.c1) ^ eval(e->data.xor.c2);
    case E_COND:
      return eval(e->data.cond.c1) ? eval(e->data.cond.c2) : eval(e->data.cond.c3);
    case E_NOT:
      return !eval(e->data.not.c1);
    case E_TAG:
      {
        Tag *s = e->data.tag.s;
        int result;
        if (!s) {
          /* Not bound yet */
          e->data.tag.s = s = find_tag_or_create(e->data.tag.name);
        }
        if (s->is_expr) {
          result = eval(s->data.e);
        } else {
          result = s->data.val;
        }
        s->is_used = 1;
        return result;
      }
    default:
      fprintf(stderr, "Interal error : Can't get here!\n");
      exit(2);
  }
}
/*}}}*/
void report_unused_tags(void)/*{{{*/
{
  Tag *s;
  TagList *sl;
  for (sl=tags; sl; sl=sl->next) {
    s = sl->tag;
    if (!s->is_used) {
      fprintf(stderr, "Warning: tag <%s> not referenced by any attribute expression\n", s->name);
    }
  }
}
/*}}}*/
