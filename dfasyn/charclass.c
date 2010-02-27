/***************************************
  Handle character classes
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

#include "dfasyn.h"
#include <ctype.h>

struct cc_list {
  struct cc_list *next;
  CharClass *cc;
};

static struct cc_list *cc_list = NULL;
static short mapping[256];

int n_charclasses;
static char *strings[256];

static void set_bit(unsigned long *bitmap, int entry)/*{{{*/
{
  int i, j, mask;
  i = (entry >> 5);
  j = entry & 31;
  mask = 1<<j;
  bitmap[i] |= mask;
}
/*}}}*/
static void clear_bit(unsigned long *bitmap, int entry)/*{{{*/
{
  int i, j, mask;
  i = (entry >> 5);
  j = entry & 31;
  mask = 1<<j;
  bitmap[i] &= ~mask;
}
/*}}}*/
int cc_test_bit(const unsigned long *bitmap, int entry)/*{{{*/
{
  int i, j, mask;
  i = (entry >> 5);
  j = entry & 31;
  mask = 1<<j;
  return (bitmap[i] & mask) ? 1 : 0;
}
/*}}}*/
CharClass *new_charclass(void)/*{{{*/
{
  CharClass *result = new(CharClass);
  result->is_used = 0;
  memset(result->char_bitmap, 0, sizeof(result->char_bitmap));
  memset(result->group_bitmap, 0, sizeof(result->group_bitmap));
  return result;
}
/*}}}*/
void free_charclass(CharClass *what)/*{{{*/
{
  free(what);
}
/*}}}*/
void add_charclass_to_list(CharClass *cc)/*{{{*/
{
  /* Add the cc to the master list for later processing. */
  struct cc_list *elt = new(struct cc_list);
  elt->next = cc_list;
  elt->cc = cc;
  cc_list = elt;
}
/*}}}*/
void add_singleton_to_charclass(CharClass *towhat, char thechar)/*{{{*/
{
  int x;
  x = (int)(unsigned char) thechar;
  set_bit(towhat->char_bitmap, x);
}
/*}}}*/
void add_range_to_charclass(CharClass *towhat, char start, char end)/*{{{*/
{
  int sx, ex, t;
  sx = (int)(unsigned char) start;
  ex = (int)(unsigned char) end;
  if (sx > ex) {
    t = sx, sx = ex, ex = t;
  }
  for (t=sx; t<=ex; t++) {
    set_bit(towhat->char_bitmap, t);
  }
}
/*}}}*/
void invert_charclass(CharClass *what)/*{{{*/
{
  int i;
  for (i=0; i<ULONGS_PER_CC; i++) {
    what->char_bitmap[i] ^= 0xffffffffUL;
  }
}
/*}}}*/
void diff_charclasses(CharClass *left, CharClass *right)/*{{{*/
{
  /* Compute set difference */
  int i;
  for (i=0; i<ULONGS_PER_CC; i++) {
    left->char_bitmap[i] &= ~(right->char_bitmap[i]);
  }
}
/*}}}*/

static char *emit_char (char *p, int i)/*{{{*/
{
  if (i == '\\') {
    *p++ = '\\';
    *p++ = '\\';
  } else if (isprint(i) && (i != '-')) {
    *p++ = i;
  } else if (i == '\n') {
    *p++ = '\\';
    *p++ = 'n';
  } else if (i == '\r') {
    *p++ = '\\';
    *p++ = 'r';
  } else if (i == '\f') {
    *p++ = '\\';
    *p++ = 'f';
  } else if (i == '\t') {
    *p++ = '\\';
    *p++ = 't';
  } else {
    p += sprintf(p, "\\%03o", i);
  }
  return p;
}
/*}}}*/
static void generate_string(int idx, const unsigned long *x)/*{{{*/
{
  int i, j;
  char buffer[4096];
  char *p;

  p = buffer;
  *p++ = '[';
  /* Force '-' to be shown at the start. */
  i = 0;
  do {
    while ((i < 256) && !cc_test_bit(x,i)) i++;
    if (i>=256) break;

    j = i + 1;
    while ((j < 256) && cc_test_bit(x,j)) j++;
    j--;

    p = emit_char(p, i);
    if (j == (i + 1)) {
      p = emit_char(p, j);
    } else if (j > (i + 1)) {
      *p++ = '-';
      p = emit_char(p, j);
    }

    i = j + 1;
  } while (i < 256);
  *p++ = ']';
  *p = 0;
  strings[idx] = new_string(buffer);
  return;
}
/*}}}*/
static void combine(unsigned long *into, const unsigned long *with)/*{{{*/
{
  int i;
  for (i=0; i<ULONGS_PER_CC; i++)  into[i] |= with[i];
}
/*}}}*/
static void set_all(unsigned long *x)/*{{{*/
{
  int i;
  for (i=0; i<ULONGS_PER_CC; i++) x[i] = 0xffffffffUL;
}
/*}}}*/
static void clear_all(unsigned long *x)/*{{{*/
{
  int i;
  for (i=0; i<ULONGS_PER_CC; i++) x[i] = 0x0UL;
}
/*}}}*/
static int find_lowest_bit_set(const unsigned long *x)/*{{{*/
{
  int i;
  for (i=0; i<ULONGS_PER_CC; i++) {
    if (x[i]) {
      int pos = 0;
      unsigned long val = x[i];
      if (!(val & 0xffff)) pos += 16, val >>= 16;
      if (!(val & 0x00ff)) pos +=  8, val >>=  8;
      if (!(val & 0x000f)) pos +=  4, val >>=  4;
      if (!(val & 0x0003)) pos +=  2, val >>=  2;
      if (!(val & 0x0001)) pos +=  1;
      return (i << 5) + pos;
    }
  }
  return -1;
}
/*}}}*/

static void mark_used_in_block(const Block *b)/*{{{*/
{
  int i;

  for (i=0; i<b->nstates; i++) {
    const State *s = b->states[i];
    const TransList *tl;
    for (tl=s->transitions; tl; tl=tl->next) {
      switch (tl->type) {
        case TT_CHARCLASS:
          tl->x.char_class->is_used = 1;
          break;
        default:
          break;
      }
    }
  }
}
/*}}}*/
static void reduce_list(void)/*{{{*/
{
  struct cc_list *ccl, *next_ccl;
  ccl = cc_list;
  cc_list = NULL;
  while (ccl) {
    next_ccl = ccl->next;
    if (ccl->cc->is_used) {
      ccl->next = cc_list;
      cc_list = ccl;
    } else {
      free(ccl->cc);
      free(ccl);
    }
    ccl = next_ccl;
  }
}
/*}}}*/
void split_charclasses(const Block *b)/*{{{*/
{
  unsigned long cc_union[ULONGS_PER_CC];
  struct cc_list *elt;
  int i;
  int any_left;

  mark_used_in_block(b);
  reduce_list();

  n_charclasses = 0;

  if (!cc_list) {
    if (verbose) fprintf(stderr, "No charclasses used\n");
    return;
  }

  /* Form union */
  clear_all(cc_union);
  for (elt=cc_list; elt; elt=elt->next) {
    combine(cc_union, elt->cc->char_bitmap);
  }

  for (i=0; i<256; i++) mapping[i] = -1;

  do {
    int first_char;
    int i;
    unsigned long pos[ULONGS_PER_CC], neg[ULONGS_PER_CC];
    first_char = find_lowest_bit_set(cc_union);
    set_all(pos);
    clear_all(neg);
    for (elt=cc_list; elt; elt=elt->next) {
      if (cc_test_bit(elt->cc->char_bitmap, first_char)) {
        for (i=0; i<ULONGS_PER_CC; i++) pos[i] &= elt->cc->char_bitmap[i];
      } else {
        for (i=0; i<ULONGS_PER_CC; i++) neg[i] |= elt->cc->char_bitmap[i];
      }
    }

    for (i=0; i<ULONGS_PER_CC; i++) {
      pos[i] &= ~neg[i];
    }

    generate_string(n_charclasses, pos);

    for (i=0; i<256; i++) {
      if (cc_test_bit(pos, i)) {
        mapping[i] = n_charclasses;
        clear_bit(cc_union, i);
      }
    }

    n_charclasses++;
    any_left = 0;
    for (i=0; i<ULONGS_PER_CC; i++) {
      if (cc_union[i]) {
        any_left = 1;
        break;
      }
    }
  } while (any_left);

  /* Build group bitmaps */
  for (elt=cc_list; elt; elt=elt->next) {
    for (i=0; i<256; i++) {
      if (cc_test_bit(elt->cc->char_bitmap, i)) {
        set_bit(elt->cc->group_bitmap, mapping[i]);
      }
    }
  }

  fprintf(stderr, "Got %d character classes\n", n_charclasses);

  return;
}
/*}}}*/
void print_charclass_mapping(FILE *out, FILE *header_out, const char *prefix_under)/*{{{*/
{
  int i;
  if (!cc_list) return;
  fprintf(out, "short %schar2tok[256] = {", prefix_under);
  for (i=0; i<256; i++) {
    if (i > 0) fputs(", ", out);
    if ((i & 15) == 0) fputs("\n  ", out);
    if (mapping[i] >= 0) {
      fprintf(out, "%3d", mapping[i] + ntokens);
    } else {
      fprintf(out, "%3d", mapping[i]);
    }
  }
  fputs("\n};\n", out);
  if (header_out) {
    fprintf(header_out, "extern short %schar2tok[256];\n",
        prefix_under);
  }
  return;
}
/*}}}*/
void print_charclass(FILE *out, int idx)/*{{{*/
{
  fprintf(out, "%d:%s", idx, strings[idx]);
}
/*}}}*/

