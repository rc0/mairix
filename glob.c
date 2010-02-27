/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2003,2004,2005
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

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "mairix.h"


struct globber {
  unsigned int pat[256];
  unsigned int starpat;
  unsigned int twostarpat;
  unsigned int hit;
};

struct globber_array {
  int n;
  struct globber **globs;
};

static const char *parse_charclass(const char *in, struct globber *result, unsigned int mask)/*{{{*/
{
  int first = 1;
  int prev = -1;
  in++; /* Advance over '[' */
  while (*in) {
    if (*in == ']') {
      if (first) {
        result->pat[(int)']'] |= mask;
      } else {
        return in;
      }
    } else if (*in == '-') {
      /* Maybe range */
      if ((prev < 0) || !in[1] || (in[1]==']')) {
        /* - at either end of string (or right after an earlier range) means
         * normal - */
        result->pat['-'] |= mask;
      } else {
        int next = in[1];
        int hi, lo;
        int i;
        /* Cope with range being inverted */
        if (prev < next) {
          lo = prev, hi = next;
        } else {
          lo = next, hi = prev;
        }
        for (i=lo; i<=hi; i++) {
          int index = 0xff & i;
          result->pat[index] |= mask;
        }
        /* require 1 extra increment */
        in++;
        prev = -1; /* Avoid junk like [a-e-z] */
      }
    } else {
      int index = 0xff & (int)*in;
      result->pat[index] |= mask;
    }
    prev = *in;
    first = 0;
    in++;
  }
  return in;
}
/*}}}*/

struct globber *make_globber(const char *wildstring)/*{{{*/
{
  struct globber *result;
  int n, i;
  const char *p;
  char c;
  int index;
  unsigned int mask;

  result = new(struct globber);
  memset(&result->pat, 0x00, 256*sizeof(unsigned int));
  memset(&result->starpat, 0x00, sizeof(unsigned int));
  memset(&result->twostarpat, 0x00, sizeof(unsigned int));
  mask = 0x1;

  n = 0;
  for (p=wildstring; *p; p++) {
    mask = 1<<n;
    c = *p;
    switch (c) {
      case '*':/*{{{*/
        if (p[1] == '*') {
          result->twostarpat |= mask;
          p++;
        } else {
          /* Match zero or more of anything */
          result->starpat |= mask;
        }
        break;
/*}}}*/
      case '[':/*{{{*/
        p = parse_charclass(p, result, mask);
        n++;
        break;
/*}}}*/
      case '?':/*{{{*/
        for (i=0; i<256; i++) {
          result->pat[i] |= mask;
        }
        n++;
        break;
/*}}}*/
      default:/*{{{*/
        index = 0xff & (int)c;
        result->pat[index] |= mask;
        n++;
        break;
/*}}}*/
    }
  }

  result->hit = (1<<n);
  return result;

}
/*}}}*/
void free_globber(struct globber *old)/*{{{*/
{
  free(old);
}
/*}}}*/

#define DODEBUG 0

int is_glob_match(struct globber *g, const char *s)/*{{{*/
{
  unsigned int reg;
  unsigned int stars;
  unsigned int twostars;
  unsigned int stars2;
  int index;

  reg = 0x1;
  while (*s) {
    index = 0xff & (int) *s;
#if DODEBUG
    printf("*s=%c index=%02x old_reg=%08lx pat=%08lx //",
           *s, index, reg, g->pat[index]);
#endif
    stars = (reg & g->starpat);
    twostars = (reg & g->twostarpat);
    if (index != '/') {
      stars2 = stars | twostars;
    } else {
      stars2 = twostars;
    }
    reg &= g->pat[index];
    reg <<= 1;
    reg |= stars2;
#if DODEBUG
    printf(" new_reg=%08lx ", reg);
    printf("starpat=%08lx stars=%08lx stars2=%08lx\n", g->starpat, stars, stars2);
#endif
    s++;
  }

#if DODEBUG
  printf("reg=%08lx hit=%08lx\n", reg, g->hit);
#endif
  reg &= g->hit;
  if (reg) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/

struct globber_array *colon_sep_string_to_globber_array(const char *in)/*{{{*/
{
  char **strings;
  int n_strings;
  int i;
  struct globber_array *result;

  split_on_colons(in, &n_strings, &strings);
  result = new(struct globber_array);
  result->n = n_strings;
  result->globs = new_array(struct globber *, n_strings);
  for (i=0; i<n_strings; i++) {
    result->globs[i] = make_globber(strings[i]);
    free(strings[i]);
  }
  free(strings);
  return result;
}
/*}}}*/
int is_globber_array_match(struct globber_array *ga, const char *s)/*{{{*/
{
  int i;
  if (!ga) return 0;
  for (i=0; i<ga->n; i++) {
    if (is_glob_match(ga->globs[i], s)) return 1;
  }
  return 0;
}
/*}}}*/
void free_globber_array(struct globber_array *in)/*{{{*/
{
  int i;
  for (i=0; i<in->n; i++) {
    free_globber(in->globs[i]);
  }
  free(in);
}
/*}}}*/

static char *copy_folder_name(const char *start, const char *end)/*{{{*/
{
  /* 'start' points to start of string to copy.
     Any '\:' sequence is replaced by ':' .
     Otherwise \ is treated normally.
     'end' can be 1 beyond the end of the string to copy.  Otherwise it can be
     null, meaning treat 'start' as the start of a normal null-terminated
     string. */
  char *p;
  const char *q;
  int len;
  char *result;
  if (end) {
    len = end - start;
  } else {
    len = strlen(start);
  }
  result = new_array(char, len + 1);
  for (p=result, q=start;
       end ? (q < end) : *q;
       q++) {
    if ((q[0] == '\\') && (q[1] == ':')) {
      /* Escaped colon : drop the backslash */
    } else {
      *p++ = *q;
    }
  }
  *p = '\0';
  return result;
}
/*}}}*/
void string_list_to_array(struct string_list *list, int *n, char ***arr)/*{{{*/
{
  int N, i;
  struct string_list *a, *next_a;
  char **result;
  for (N=0, a=list->next; a!=list; a=a->next, N++) ;

  result = new_array(char *, N);
  for (i=0, a=list->next; i<N; a=next_a, i++) {
    result[i] = a->data;
    next_a = a->next;
    free(a);
  }

  *n = N;
  *arr = result;
}
/*}}}*/
void split_on_colons(const char *str, int *n, char ***arr)/*{{{*/
{
  struct string_list list, *new_cell;
  const char *left_to_do;

  list.next = list.prev = &list;
  left_to_do = str;
  do {
    char *colon;
    char *xx;

    colon = strchr(left_to_do, ':');
    /* Allow backslash-escaped colons in filenames */
    if (colon && (colon > left_to_do) && (colon[-1]=='\\')) {
      int is_escaped;
      do {
        colon = strchr(colon + 1, ':');
        is_escaped = (colon && (colon[-1] == '\\'));
      } while (colon && is_escaped);
    }
    /* 'colon' now points to the first non-escaped colon or is null if there
       were no more such colons in the rest of the line. */

    xx = copy_folder_name(left_to_do, colon);
    if (colon) {
      left_to_do = colon + 1;
    } else {
      while (*left_to_do) ++left_to_do;
    }

    new_cell = new(struct string_list);
    new_cell->data = xx;
    new_cell->next = &list;
    new_cell->prev = list.prev;
    list.prev->next = new_cell;
    list.prev = new_cell;
  } while (*left_to_do);

  string_list_to_array(&list, n, arr);

}
/*}}}*/

#if defined (TEST)
void run1(char *ref, char *s, int expected)/*{{{*/
{
  struct globber *g;
  int result;
  g = make_globber(ref);
  result = is_glob_match(g, s);

  printf("ref=%s, str=%s, %s  %s\n", ref, s, result ? "MATCHED" : "not matched", (expected==result) ? "" : "??????");
  free_globber(g);
}
/*}}}*/
int main (int argc, char **argv)/*{{{*/
{

  run1("ab?de", "abdde", 1);
  run1("ab?de", "abcde", 1);
  run1("ab?de", "Abcde", 0);
  run1("ab?de", "abcd", 0);
  run1("ab?de", "abc", 0);
  run1("ab[cd]de", "abdde", 1);
  run1("ab[cd]de", "abbde", 0);
  run1("ab[cd]de", "abcde", 1);
  run1("ab*de", "ade", 0);
  run1("ab*de", "abde", 1);
  run1("ab*de", "abcde", 1);
  run1("ab*de", "abccde", 1);
  run1("ab*de", "abccdfde", 1);
  run1("ab*de", "abccdedf", 0);
  run1("ab[b-d]de", "abade",0);
  run1("ab[b-d]de", "abcDe",0);
  run1("ab[b-d]de", "abcde",1);
  run1("ab[b-d]de", "abdde",1);
  run1("ab[b-d]de", "abEde", 0);
  run1("[a-z][0-9A-F][]a-f-]", "yE]", 1);
  run1("[a-z][0-9A-F][]a-f-]", "uE[", 0);
  run1("[a-z][0-9A-F][]a-f-]", "vG-", 0);
  run1("[a-z][0-9A-F][]a-f-]", "w8-", 1);
  run1("*", "a", 1);
  run1("*", "", 1);
  run1("a*", "a", 1);
  run1("a*", "aa", 1);
  run1("a*", "aaA", 1);
  run1("*a", "aaa", 1);
  run1("*a", "a", 1);
  run1("x*abc", "xabdxabc", 1);
  run1("*", "", 1);
  run1("a*", "", 0);
  run1("*a", "", 0);
  run1("a", "", 0);

  run1("*abc*", "x/abc/y", 0);
  run1("**abc**", "x/abc/y", 1);
  run1("x/*/abc**", "x/z/abc/y", 1);
  run1("x/*/abc**", "x/z/w/abc/y", 0);
  run1("x/*/abc**", "x/zz/w/abc/y", 0);
  run1("x/*/abc**", "x/z/ww/abc/y", 0);
  run1("x/**/abc**", "x/z/w/abc/y", 1);
  run1("x/**/abc**", "x/zz/w/abc/y", 1);

  return 0;
}
/*}}}*/
#endif

