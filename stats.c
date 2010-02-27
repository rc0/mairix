/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004
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

#include "mairix.h"
#include "memmac.h"
#include "reader.h"

static void do_toktable(struct toktable *x, int *lc, int *elc, int *ec, int size, int *ml, int *mel, int *me)
{
  int i;
  for (i=0; i<x->size; i++) {
    struct token *tok = x->tokens[i];
    unsigned char *j, *last_char;
    int incr;

    if (tok) {
      int len = strlen(tok->text);
      if (len > size) {
        fprintf(stderr, "Token length %d exceeds size\n", len);
      } else {
        lc[len]++;
        if (len > *ml) *ml = len;
      }

      /* Deal with encoding length */
      if (tok->match0.n > size) {
        fprintf(stderr, "Token encoding length %d exceeds size\n", tok->match0.n);
      } else {
        elc[tok->match0.n]++;
        if (tok->match0.n > *mel) *mel = tok->match0.n;
      }

      /* Deal with encoding */
      j = tok->match0.msginfo;
      last_char = j + tok->match0.n;
      while (j < last_char) {
        incr = read_increment(&j);
        if (incr > size) {
          fprintf(stderr, "Encoding increment %d exceeds size\n", incr);
        } else {
          ec[incr]++;
          if (incr > *me) *me = incr;
        }
      }
    }
  }
}

void print_table(int *x, int max) {
  int total, sum;
  int i;
  int kk, kk1;

  total = 0;
  for (i = 0; i<=max; i++) {
    total += x[i];
  }
  sum = 0;
  kk1 = 0;
  for (i = 0; i<=max; i++) {
    sum += x[i];
    kk = (int)((double)sum*256.0/(double)total);
    printf("%5d : %5d %3d %3d\n", i, x[i], kk-kk1, kk);
    kk1 = kk;
  }
}

void get_db_stats(struct database *db)
{
  /* Deal with paths later - problem is, they will be biased by length of folder_base at the moment. */

  int size = 4096;
  int *len_counts, *enc_len_counts, *enc_counts;
  int max_len, max_enc_len, max_enc;

  max_len = 0;
  max_enc_len = 0;
  max_enc = 0;

  len_counts = new_array(int, size);
  memset(len_counts, 0, size * sizeof(int));
  enc_len_counts = new_array(int, size);
  memset(enc_len_counts, 0, size * sizeof(int));
  enc_counts = new_array(int, size);
  memset(enc_counts, 0, size * sizeof(int));

  do_toktable(db->to, len_counts, enc_len_counts, enc_counts, size, &max_len, &max_enc_len, &max_enc);
  do_toktable(db->cc, len_counts, enc_len_counts, enc_counts, size, &max_len, &max_enc_len, &max_enc);
  do_toktable(db->from, len_counts, enc_len_counts, enc_counts, size, &max_len, &max_enc_len, &max_enc);
  do_toktable(db->subject, len_counts, enc_len_counts, enc_counts, size, &max_len, &max_enc_len, &max_enc);
  do_toktable(db->body, len_counts, enc_len_counts, enc_counts, size, &max_len, &max_enc_len, &max_enc);
#if 0
  /* no longer works now that the msg_ids table has 2 encoding chains.  fix
   * this when required. */
  do_toktable(db->msg_ids, len_counts, enc_len_counts, enc_counts, size, &max_len, &max_enc_len, &max_enc);
#endif

  printf("Max token length : %d\n", max_len);
  print_table(len_counts, max_len);

  printf("Max encoding vector length : %d\n", max_enc_len);
  print_table(enc_len_counts, max_enc_len);

  printf("Max encoding increment : %d\n", max_enc);
  print_table(enc_counts, max_enc);

  return;
}

