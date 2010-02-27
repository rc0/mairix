/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004, 2005
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

/* Functions for handling tokens */

#include <assert.h>
#include <ctype.h>
#include "mairix.h"

static void init_matches(struct matches *m) {/*{{{*/
  m->msginfo = NULL;
  m->n = 0;
  m->max = 0;
  m->highest = 0;
}
/*}}}*/
struct token *new_token(void)/*{{{*/
{
  struct token *result = new(struct token);
  result->text = NULL;
  init_matches(&result->match0);
  return result;
}
/*}}}*/
struct token2 *new_token2(void)/*{{{*/
{
  struct token2 *result = new(struct token2);
  result->text = NULL;
  init_matches(&result->match0);
  init_matches(&result->match1);
  return result;
}
/*}}}*/
void free_token(struct token *x)/*{{{*/
{
  if (x->text) free(x->text);
  if (x->match0.msginfo) free(x->match0.msginfo);
  free(x);
}
/*}}}*/
void free_token2(struct token2 *x)/*{{{*/
{
  if (x->text) free(x->text);
  if (x->match0.msginfo) free(x->match0.msginfo);
  if (x->match1.msginfo) free(x->match1.msginfo);
  free(x);
}
/*}}}*/
struct toktable *new_toktable(void)/*{{{*/
{
  struct toktable *result = new(struct toktable);
  result->tokens = NULL;
  result->n = 0;
  result->hwm = 0;
  result->size = 0;
  return result;
}
/*}}}*/
struct toktable2 *new_toktable2(void)/*{{{*/
{
  struct toktable2 *result = new(struct toktable2);
  result->tokens = NULL;
  result->n = 0;
  result->hwm = 0;
  result->size = 0;
  return result;
}
/*}}}*/
void free_toktable(struct toktable *x)/*{{{*/
{
  if (x->tokens) {
    int i;
    for (i=0; i<x->size; i++) {
      if (x->tokens[i]) {
        free_token(x->tokens[i]);
      }
    }
    free(x->tokens);
  }
  free(x);
}
/*}}}*/
void free_toktable2(struct toktable2 *x)/*{{{*/
{
  if (x->tokens) {
    int i;
    for (i=0; i<x->size; i++) {
      if (x->tokens[i]) {
        free_token2(x->tokens[i]);
      }
    }
    free(x->tokens);
  }
  free(x);
}
/*}}}*/
/* FIXME : This stuff really needs cleaning up. */
static void enlarge_toktable(struct toktable *table)/*{{{*/
{
  if (table->size == 0) {
    int i;
    /* initial allocation */
    table->size = 1024;
    table->mask = table->size - 1;
    table->tokens = new_array(struct token *, table->size);
    for (i=0; i<table->size; i++) {
      table->tokens[i] = NULL;
    }
  } else {
    struct token **old_tokens;
    int old_size = table->size;
    int i;
    /* reallocate */
    old_tokens = table->tokens;
    table->size <<= 1;
    table->mask = table->size - 1;
    table->tokens = new_array(struct token *, table->size);
    for (i=0; i<table->size; i++) {
      table->tokens[i] = NULL;
    }
    for (i=0; i<old_size; i++) {
      unsigned long new_index;
      if (old_tokens[i]) {
        new_index = old_tokens[i]->hashval & table->mask;
        while (table->tokens[new_index]) {
          new_index++;
          new_index &= table->mask;
        }
        table->tokens[new_index] = old_tokens[i];
      }
    }
    free(old_tokens);
  }
  table->hwm = (table->size >> 2) + (table->size >> 3); /* allow 3/8 of nodes to be used */
}
/*}}}*/
static void enlarge_toktable2(struct toktable2 *table)/*{{{*/
{
  if (table->size == 0) {
    int i;
    /* initial allocation */
    table->size = 1024;
    table->mask = table->size - 1;
    table->tokens = new_array(struct token2 *, table->size);
    for (i=0; i<table->size; i++) {
      table->tokens[i] = NULL;
    }
  } else {
    struct token2 **old_tokens;
    int old_size = table->size;
    int i;
    /* reallocate */
    old_tokens = table->tokens;
    table->size <<= 1;
    table->mask = table->size - 1;
    table->tokens = new_array(struct token2 *, table->size);
    for (i=0; i<table->size; i++) {
      table->tokens[i] = NULL;
    }
    for (i=0; i<old_size; i++) {
      unsigned long new_index;
      if (old_tokens[i]) {
        new_index = old_tokens[i]->hashval & table->mask;
        while (table->tokens[new_index]) {
          new_index++;
          new_index &= table->mask;
        }
        table->tokens[new_index] = old_tokens[i];
      }
    }
    free(old_tokens);
  }
  table->hwm = (table->size >> 2) + (table->size >> 3); /* allow 3/8 of nodes to be used */
}
/*}}}*/
static int insert_value(unsigned char *x, int val)/*{{{*/
{
  assert(val >= 0);
  if (val <= 127) {
    *x = val;
    return 1;
  } else if (val <= 16383) {
    *x++ = (val >> 8) | 0x80;
    *x   = (val & 0xff);
    return 2;
  } else {
    int a = (val >> 24);
    assert (a <= 63);
    *x++ = a | 0xc0;
    *x++ = ((val >> 16) & 0xff);
    *x++ = ((val >>  8) & 0xff);
    *x   = (val         & 0xff);
    return 4;
  }
}
/*}}}*/
void check_and_enlarge_encoding(struct matches *m)/*{{{*/
{
  if (m->n + 4 >= m->max) {
    if (m->max == 0) {
      m->max = 16;
    } else {
      m->max += (m->max >> 1);
    }
    m->msginfo = grow_array(unsigned char, m->max, m->msginfo);
  }
}
/*}}}*/
void insert_index_on_encoding(struct matches *m, int idx)/*{{{*/
{
  if (m->n == 0) {
    /* Always encode value */
    m->n += insert_value(m->msginfo + m->n, idx);
  } else {
    assert(idx >= m->highest);
    if (idx > m->highest) {
      int increment = idx - m->highest;
      m->n += insert_value(m->msginfo + m->n, increment);
    } else {
      /* token has already been seen in this file */
    }
  }
  m->highest = idx;
}
/*}}}*/
void add_token_in_file(int file_index, unsigned int hash_key, char *tok_text, struct toktable *table)/*{{{*/
{
  unsigned long hash;
  int index;
  struct token *tok;
  char *lc_tok_text;
  char *p;

  lc_tok_text = new_string((char*)tok_text);
  for (p = lc_tok_text; *p; p++) {
    *p = tolower(*(unsigned char *) p);
  }
  /* 2nd arg is string length */
  hash = hashfn((unsigned char *) lc_tok_text, p - lc_tok_text, hash_key);

  if (table->n >= table->hwm) {
    enlarge_toktable(table);
  }

  index = hash & table->mask;
  while (table->tokens[index]) {
    /* strcmp ok as text has been tolower'd earlier */
    if (!strcmp(lc_tok_text, table->tokens[index]->text))
      break;
    index++;
    index &= table->mask;
  }

  if (!table->tokens[index]) {
    /* Allocate new */
    struct token *new_tok = new_token();
    /* New token takes ownership of lc_tok_text, no need to free that later. */
    new_tok->text = (char *) lc_tok_text;
    new_tok->hashval = hash; /* save full width for later */
    table->tokens[index] = new_tok;
    ++table->n;
  } else {
    free(lc_tok_text);
  }

  tok = table->tokens[index];

  check_and_enlarge_encoding(&tok->match0);
  insert_index_on_encoding(&tok->match0, file_index);
}
/*}}}*/
void add_token2_in_file(int file_index, unsigned int hash_key, char *tok_text, struct toktable2 *table, int add_to_chain1)/*{{{*/
{
  unsigned long hash;
  int index;
  struct token2 *tok;
  char *lc_tok_text;
  char *p;

  lc_tok_text = new_string(tok_text);
  for (p = lc_tok_text; *p; p++) {
    *p = tolower(*(unsigned char *) p);
  }
  /* 2nd arg is string length */
  hash = hashfn((unsigned char *) lc_tok_text, p - lc_tok_text, hash_key);

  if (table->n >= table->hwm) {
    enlarge_toktable2(table);
  }

  index = hash & table->mask;
  while (table->tokens[index]) {
    /* strcmp ok as text has been tolower'd earlier */
    if (!strcmp(lc_tok_text, table->tokens[index]->text))
      break;
    index++;
    index &= table->mask;
  }

  if (!table->tokens[index]) {
    /* Allocate new */
    struct token2 *new_tok = new_token2();
    /* New token takes ownership of lc_tok_text, no need to free that later. */
    new_tok->text = lc_tok_text;
    new_tok->hashval = hash; /* save full width for later */
    table->tokens[index] = new_tok;
    ++table->n;
  } else {
    free(lc_tok_text);
  }

  tok = table->tokens[index];

  check_and_enlarge_encoding(&tok->match0);
  insert_index_on_encoding(&tok->match0, file_index);
  if (add_to_chain1) {
    check_and_enlarge_encoding(&tok->match1);
    insert_index_on_encoding(&tok->match1, file_index);
  }
}
/*}}}*/




