/*
  $Header: /cvs/src/mairix/glob.c,v 1.5 2004/01/11 23:46:54 richard Exp $

  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2003-2004
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

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include "mairix.h"


struct globber {
  unsigned int pat[256];
  unsigned int starpat;
  unsigned int hit;
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
  memset(&result->pat, 0x00, 256*sizeof(unsigned long));
  memset(&result->starpat, 0x00, sizeof(unsigned long));
  mask = 0x1;

  n = 0;
  for (p=wildstring; *p; p++) {
    mask = 1<<n;
    c = *p;
    switch (c) {
      case '*':/*{{{*/
        /* Match zero or more of anything */
        result->starpat |= mask;
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
  int index;

  reg = 0x1;
  while (*s) {
    index = 0xff & (int) *s;
#if DODEBUG
    printf("*s=%c index=%02x old_reg=%08lx pat=%08lx ",
           *s, index, reg, g->pat[index]);
#endif
    stars = (reg & g->starpat);
    reg &= g->pat[index];
    reg <<= 1;
    reg |= stars;
#if DODEBUG
    printf("new_reg=%08lx ", reg);
    printf("starpat=%08lx stars=%08lx\n", g->starpat, stars);
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

  return 0;
}
/*}}}*/
#endif

