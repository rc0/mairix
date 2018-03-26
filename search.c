/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002,2003,2004,2005,2006
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>

/* Lame fix for systems where NAME_MAX isn't defined after including the above
 * set of .h files (Solaris, FreeBSD so far).  Probably grossly oversized but
 * it'll do. */

#if !defined(NAME_MAX)
#define NAME_MAX 4096
#endif

#include "mairix.h"
#include "reader.h"
#include "memmac.h"
#include "imapinterface.h"

static void mark_hits_in_table(struct read_db *db, struct toktable_db *tt, int hit_tok, char *hits)/*{{{*/
{
  /* mark files containing matched token */
  int idx;
  unsigned char *j, *first_char;
  idx = 0;
  first_char = (unsigned char *) db->data + tt->enc_offsets[hit_tok];
  for (j = first_char; *j != 0xff; ) {
    idx += read_increment(&j);
    assert(idx < db->n_msgs);
    hits[idx] = 1;
  }
}
/*}}}*/
static void mark_hits_in_table2(struct read_db *db, struct toktable2_db *tt, int hit_tok, char *hits)/*{{{*/
{
  /* mark files containing matched token */
  int idx;
  unsigned char *j, *first_char;
  idx = 0;
  first_char = (unsigned char *) db->data + tt->enc1_offsets[hit_tok];
  for (j = first_char; *j != 0xff; ) {
    idx += read_increment(&j);
    assert(idx < db->n_msgs);
    hits[idx] = 1;
  }
}
/*}}}*/

/* See "Fast text searching with errors, Sun Wu and Udi Manber, TR 91-11,
   University of Arizona.  I have been informed that this algorithm is NOT
   patented.  This implementation of it is entirely the work of Richard P.
   Curnow - I haven't looked at any related source (webglimpse, agrep etc) in
   writing this.
*/
static void build_match_vector(char *substring, unsigned long long *a, unsigned long long *hit)/*{{{*/
{
  int len;
  char *p;
  int i;

  len = strlen(substring);
  if (len > 31 || len == 0) {
    fprintf(stderr, "Can't match patterns longer than 31 characters or empty\n");
    unlock_and_exit(2);
  }
  memset(a, 0xff, 256 * sizeof(unsigned long));
  for (p=substring, i=0; *p; p++, i++) {
    a[(unsigned int) *(unsigned char *)p] &= ~(1UL << i);
  }
  *hit = ~(1UL << (len-1));
  return;
}
/*}}}*/
static int substring_match_0(unsigned long long *a, unsigned long long hit, int left_anchor, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long long r0;
  unsigned long long anchor, anchor1;

  r0 = ~0;
  got_hit = 0;
  anchor = 0;
  anchor1 = left_anchor ? 0x1 : 0x0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    r0 = (r0<<1) | anchor | a[idx];
    if (~(r0 | hit)) {
      got_hit = 1;
      break;
    }
    anchor = anchor1;
  }
  return got_hit;
}
/*}}}*/
static int substring_match_1(unsigned long long *a, unsigned long long hit, int left_anchor, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long long r0, r1, nr0;
  unsigned long long anchor, anchor1;

  r0 = ~0;
  r1 = r0<<1;
  got_hit = 0;
  anchor = 0;
  anchor1 = left_anchor ? 0x1 : 0x0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    nr0 = (r0<<1) | anchor | a[idx];
    r1  = ((r1<<1) | anchor | a[idx]) & ((r0 & nr0) << 1) & r0;
    r0  = nr0;
    if (~((r0 & r1) | hit)) {
      got_hit = 1;
      break;
    }
    anchor = anchor1;
  }
  return got_hit;
}
/*}}}*/
static int substring_match_2(unsigned long long *a, unsigned long long hit, int left_anchor, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long long r0, r1, r2, nr0, nr1;
  unsigned long long anchor, anchor1;

  r0 = ~0;
  r1 = r0<<1;
  r2 = r1<<1;
  got_hit = 0;
  anchor = 0;
  anchor1 = left_anchor ? 0x1 : 0x0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    nr0 =  (r0<<1) | anchor | a[idx];
    nr1 = ((r1<<1) | anchor | a[idx]) & ((r0 & nr0) << 1) & r0;
    r2  = ((r2<<1) | anchor | a[idx]) & ((r1 & nr1) << 1) & r1;
    r0  = nr0;
    r1  = nr1;
    if (~((r0 & r1 & r2) | hit)) {
      got_hit = 1;
      break;
    }
    anchor = anchor1;
  }
  return got_hit;
}
/*}}}*/
static int substring_match_3(unsigned long long *a, unsigned long long hit, int left_anchor, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long long r0, r1, r2, r3, nr0, nr1, nr2;
  unsigned long long anchor, anchor1;

  r0 = ~0;
  r1 = r0<<1;
  r2 = r1<<1;
  r3 = r2<<1;
  got_hit = 0;
  anchor = 0;
  anchor1 = left_anchor ? 0x1 : 0x0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    nr0 =  (r0<<1) | anchor | a[idx];
    nr1 = ((r1<<1) | anchor | a[idx]) & ((r0 & nr0) << 1) & r0;
    nr2 = ((r2<<1) | anchor | a[idx]) & ((r1 & nr1) << 1) & r1;
    r3  = ((r3<<1) | anchor | a[idx]) & ((r2 & nr2) << 1) & r2;
    r0  = nr0;
    r1  = nr1;
    r2  = nr2;
    if (~((r0 & r1 & r2 & r3) | hit)) {
      got_hit = 1;
      break;
    }
    anchor = anchor1;
  }
  return got_hit;
}
/*}}}*/
static int substring_match_general(unsigned long long *a, unsigned long long hit, int left_anchor, char *token, int max_errors, unsigned long long *r, unsigned long long *nr)/*{{{*/
{
  int got_hit=0;
  char *p;
  int j;
  unsigned long long anchor, anchor1;

  r[0] = ~0;
  anchor = 0;
  anchor1 = left_anchor ? 0x1 : 0x0;
  for (j=1; j<=max_errors; j++) {
    r[j] = r[j-1] << 1;
  }
  got_hit = 0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    int d;
    unsigned int compo;

    compo = nr[0] = ((r[0]<<1) | anchor | a[idx]);
    for (d=1; d<=max_errors; d++) {
      nr[d] = ((r[d]<<1) | anchor | a[idx])
        & ((r[d-1] & nr[d-1])<<1)
        & r[d-1];
      compo &= nr[d];
    }
    memcpy(r, nr, (1 + max_errors) * sizeof(unsigned long));
    if (~(compo | hit)) {
      got_hit = 1;
      break;
    }
    anchor = anchor1;
  }
  return got_hit;
}
/*}}}*/

static void match_substring_in_table(struct read_db *db, struct toktable_db *tt, char *substring, int max_errors, int left_anchor, char *hits)/*{{{*/
{

  int i, got_hit;
  unsigned long long a[256];
  unsigned long long *r=NULL, *nr=NULL;
  unsigned long long hit;
  char *token;

  build_match_vector(substring, a, &hit);

  got_hit = 0;
  if (max_errors > 3) {
    r = new_array(unsigned long, 1 + max_errors);
    nr = new_array(unsigned long, 1 + max_errors);
  }
  for (i=0; i<tt->n; i++) {
    token = db->data + tt->tok_offsets[i];
    switch (max_errors) {
      /* Optimise common cases for few errors to allow optimizer to keep bitmaps
       * in registers */
      case 0:
        got_hit = substring_match_0(a, hit, left_anchor, token);
        break;
      case 1:
        got_hit = substring_match_1(a, hit, left_anchor, token);
        break;
      case 2:
        got_hit = substring_match_2(a, hit, left_anchor, token);
        break;
      case 3:
        got_hit = substring_match_3(a, hit, left_anchor, token);
        break;
      default:
        got_hit = substring_match_general(a, hit, left_anchor, token, max_errors, r, nr);
        break;
    }
    if (got_hit) {
      mark_hits_in_table(db, tt, i, hits);
    }
  }
  if (r)  free(r);
  if (nr) free(nr);
}
/*}}}*/
static void match_substring_in_table2(struct read_db *db, struct toktable2_db *tt, char *substring, int max_errors, int left_anchor, char *hits)/*{{{*/
{

  int i, got_hit;
  unsigned long long a[256];
  unsigned long long *r=NULL, *nr=NULL;
  unsigned long long hit;
  char *token;

  build_match_vector(substring, a, &hit);

  got_hit = 0;
  if (max_errors > 3) {
    r = new_array(unsigned long, 1 + max_errors);
    nr = new_array(unsigned long, 1 + max_errors);
  }
  for (i=0; i<tt->n; i++) {
    token = db->data + tt->tok_offsets[i];
    switch (max_errors) {
      /* Optimise common cases for few errors to allow optimizer to keep bitmaps
       * in registers */
      case 0:
        got_hit = substring_match_0(a, hit, left_anchor, token);
        break;
      case 1:
        got_hit = substring_match_1(a, hit, left_anchor, token);
        break;
      case 2:
        got_hit = substring_match_2(a, hit, left_anchor, token);
        break;
      case 3:
        got_hit = substring_match_3(a, hit, left_anchor, token);
        break;
      default:
        got_hit = substring_match_general(a, hit, left_anchor, token, max_errors, r, nr);
        break;
    }
    if (got_hit) {
      mark_hits_in_table2(db, tt, i, hits);
    }
  }
  if (r)  free(r);
  if (nr) free(nr);
}
/*}}}*/
static void match_substring_in_paths(struct read_db *db, char *substring, int max_errors, int left_anchor, char *hits)/*{{{*/
{

  int i;
  unsigned long long a[256];
  unsigned long long *r=NULL, *nr=NULL;
  unsigned long long hit;

  build_match_vector(substring, a, &hit);

  if (max_errors > 3) {
    r = new_array(unsigned long, 1 + max_errors);
    nr = new_array(unsigned long, 1 + max_errors);
  }
  for (i=0; i<db->n_msgs; i++) {
    char *token = NULL;
    unsigned int mbix, msgix;
    switch (rd_msg_type(db, i)) {
      case DB_MSG_FILE:
        token = db->data + db->path_offsets[i];
        break;
      case DB_MSG_MBOX:
        decode_mbox_indices(db->path_offsets[i], &mbix, &msgix);
        token = db->data + db->mbox_paths_table[mbix];
        break;
      case DB_MSG_DEAD:
        hits[i] = 0; /* never match on dead paths */
        goto next_message;
    }

    assert(token);

    switch (max_errors) {
      /* Optimise common cases for few errors to allow optimizer to keep bitmaps
       * in registers */
      case 0:
        hits[i] = substring_match_0(a, hit, left_anchor, token);
        break;
      case 1:
        hits[i] = substring_match_1(a, hit, left_anchor, token);
        break;
      case 2:
        hits[i] = substring_match_2(a, hit, left_anchor, token);
        break;
      case 3:
        hits[i] = substring_match_3(a, hit, left_anchor, token);
        break;
      default:
        hits[i] = substring_match_general(a, hit, left_anchor, token, max_errors, r, nr);
        break;
    }
next_message:
    (void) 0;
  }

  if (r)  free(r);
  if (nr) free(nr);
}
/*}}}*/
static void match_string_in_table(struct read_db *db, struct toktable_db *tt, char *key, char *hits)/*{{{*/
{
  /* TODO : replace with binary search? */
  int i;

  for (i=0; i<tt->n; i++) {
    if (!strcmp(key, db->data + tt->tok_offsets[i])) {
      /* get all matching files */
      mark_hits_in_table(db, tt, i, hits);
    }
  }
}
/*}}}*/
static void match_string_in_table2(struct read_db *db, struct toktable2_db *tt, char *key, char *hits)/*{{{*/
{
  /* TODO : replace with binary search? */
  int i;

  for (i=0; i<tt->n; i++) {
    if (!strcmp(key, db->data + tt->tok_offsets[i])) {
      /* get all matching files */
      mark_hits_in_table2(db, tt, i, hits);
    }
  }
}
/*}}}*/
static int parse_size_expr(char *x)/*{{{*/
{
  int result;
  int n;

  if (1 == sscanf(x, "%d%n", &result, &n)) {
    x += n;
    switch (*x) {
      case 'k':
      case 'K':
        result <<= 10;
        break;
      case 'm':
      case 'M':
        result <<= 20;
        break;
      default:
        break;
    }

    return result;
  } else {
    fprintf(stderr, "Could not parse message size expression <%s>\n", x);
    return -1;
  }
}
/*}}}*/
static void parse_size_range(char *size_expr, int *has_start, int *start, int *has_end, int *end)/*{{{*/
{
  char *x = size_expr;
  char *dash;
  int len;

  if (*x == ':') x++;
  len = strlen(x);
  dash = strchr(x, '-');
  *has_start = *has_end = 0;
  if (dash) {
    char *p, *q;
    if (dash > x) {
      char *s;
      s = new_array(char, dash - x + 1);
      for (p=s, q=x; q<dash; ) *p++ = *q++;
      *p = 0;
      *start = parse_size_expr(s);
      *has_start = 1;
      free(s);
    }
    if (dash[1]) { /* dash not at end of arg */
      char *e;
      e = new_array(char, (x + len) - dash);
      for (p=e, q=dash+1; *q; ) *p++ = *q++;
      *p = 0;
      *end = parse_size_expr(e);
      *has_end = 1;
      free(e);
    }
  } else {
    *has_start = 0;
    *end = parse_size_expr(size_expr);
    *has_end = 1;
  }
  return;
}
/*}}}*/
static void find_size_matches_in_table(struct read_db *db, char *size_expr, char *hits)/*{{{*/
{
  int start, end;
  int has_start, has_end, start_cond, end_cond;
  int i;

  start = end = -1; /* avoid compiler warning about uninitialised variables. */
  parse_size_range(size_expr, &has_start, &start, &has_end, &end);
  if (has_start && has_end) {
    /* Allow user to put the endpoints in backwards */
    if (start > end) {
      int temp = start;
      start = end;
      end = temp;
    }
  }

  for (i=0; i<db->n_msgs; i++) {
    start_cond = has_start ? (db->size_table[i] > start) : 1;
    end_cond   = has_end   ? (db->size_table[i] < end  ) : 1;
    if (start_cond && end_cond) {
      hits[i] = 1;
    }
  }
}
/*}}}*/
static void find_date_matches_in_table(struct read_db *db, char *date_expr, char *hits)/*{{{*/
{
  time_t start, end;
  int has_start, has_end, start_cond, end_cond;
  int i;
  int status;

  status = scan_date_string(date_expr, &start, &has_start, &end, &has_end);
  if (status) {
    unlock_and_exit (2);
  }

  if (has_start && has_end) {
    /* Allow user to put the endpoints in backwards */
    if (start > end) {
      time_t temp = start;
      start = end;
      end = temp;
    }
  }

  for (i=0; i<db->n_msgs; i++) {
    start_cond = has_start ? (db->date_table[i] > start) : 1;
    end_cond   = has_end   ? (db->date_table[i] < end  ) : 1;
    if (start_cond && end_cond) {
      hits[i] = 1;
    }
  }
}
/*}}}*/
static void find_flag_matches_in_table(struct read_db *db, char *flag_expr, char *hits)/*{{{*/
{
  int pos_seen, neg_seen;
  int pos_replied, neg_replied;
  int pos_flagged, neg_flagged;
  int negate;
  char *p;
  int i;

  negate = 0;
  pos_seen = neg_seen = 0;
  pos_replied = neg_replied = 0;
  pos_flagged = neg_flagged = 0;
  for (p=flag_expr; *p; p++) {
    switch (*p) {
      case '-':
        negate = 1;
        break;
      case 's':
      case 'S':
        if (negate) neg_seen = 1;
        else pos_seen = 1;
        negate = 0;
        break;
      case 'r':
      case 'R':
        if (negate) neg_replied = 1;
        else pos_replied = 1;
        negate = 0;
        break;
      case 'f':
      case 'F':
        if (negate) neg_flagged = 1;
        else pos_flagged = 1;
        negate = 0;
        break;
      default:
        fprintf(stderr, "Did not understand the character '%c' (0x%02x) in the flags argument F:%s\n",
            isprint(*p) ? *p : '.',
            (int) *(unsigned char *) p,
            flag_expr);
        break;
    }
  }

  for (i=0; i<db->n_msgs; i++) {
    if ((!pos_seen || (db->msg_type_and_flags[i] & FLAG_SEEN)) &&
        (!neg_seen || !(db->msg_type_and_flags[i] & FLAG_SEEN)) &&
        (!pos_replied || (db->msg_type_and_flags[i] & FLAG_REPLIED)) &&
        (!neg_replied || !(db->msg_type_and_flags[i] & FLAG_REPLIED)) &&
        (!pos_flagged || (db->msg_type_and_flags[i] & FLAG_FLAGGED)) &&
        (!neg_flagged || !(db->msg_type_and_flags[i] & FLAG_FLAGGED))) {
      hits[i] = 1;
    }
  }
}
/*}}}*/

static char *mk_maildir_path(int token, char *output_dir, int is_in_new,
    int is_seen, int is_replied, int is_flagged)/*{{{*/
{
  char *result;
  char uniq_buf[48];
  int len;

  len = strlen(output_dir) + 64; /* oversize */
  result = new_array(char, len + 1 + sizeof(":2,FRS"));
  strcpy(result, output_dir);
  strcat(result, is_in_new ? "/new/" : "/cur/");
  sprintf(uniq_buf, "123456789.%d.mairix", token);
  strcat(result, uniq_buf);
  if (is_seen || is_replied || is_flagged) {
    strcat(result, ":2,");
  }
  if (is_flagged) strcat(result, "F");
  if (is_replied) strcat(result, "R");
  if (is_seen) strcat(result, "S");
  return result;
}
/*}}}*/
static char *mk_mh_path(int token, char *output_dir)/*{{{*/
{
  char *result;
  char uniq_buf[8];
  int len;

  len = strlen(output_dir) + 10; /* oversize */
  result = new_array(char, len);
  strcpy(result, output_dir);
  strcat(result, "/");
  sprintf(uniq_buf, "%d", token+1);
  strcat(result, uniq_buf);
  return result;
}
/*}}}*/
static int looks_like_maildir_new_p(const char *p)/*{{{*/
{
  const char *s1, *s2;
  s2 = p;
  while (*s2) s2++;
  while ((s2 > p) && (*s2 != '/')) s2--;
  if (s2 <= p) return 0;
  s1 = s2 - 1;
  while ((s1 > p) && (*s1 != '/')) s1--;
  if (s1 <= p) return 0;
  if (!strncmp(s1, "/new/", 5)) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/
static void create_symlink(char *link_target, char *new_link)/*{{{*/
{
  if ((!do_hardlinks && symlink(link_target, new_link) < 0) || link(link_target, new_link)) {
    if (verbose) {
      perror("symlink");
      fprintf(stderr, "Failed path <%s> -> <%s>\n", link_target, new_link);
    }
  }
}
/*}}}*/
static void mbox_terminate(const unsigned char *data, int len, FILE *out)/*{{{*/
{
  if (len == 0)
    fputs("\n", out);
  else if (len == 1) {
    if (data[0] != '\n')
      fputs("\n", out);
  }
  else if (data[len-1] != '\n')
    fputs("\n\n", out);
  else if (data[len-2] != '\n')
    fputs("\n", out);
}
/*}}}*/
static void append_file_to_mbox(const char *path, FILE *out)/*{{{*/
{
  unsigned char *data;
  size_t len;
  create_ro_mapping(path, &data, &len);
  if (data) {
    fprintf(out, "From mairix@mairix Mon Jan  1 12:34:56 1970\n");
    fprintf(out, "X-source-folder: %s\n", path);
    fwrite (data, sizeof(unsigned char), len, out);
    mbox_terminate(data, len, out);
    free_ro_mapping(data, len);
  }
  return;
}
/*}}}*/
static void append_data_to_mbox(const char *data, size_t len, void *arg)/*{{{*/
{
  FILE *out = (FILE *)arg;
  fprintf(out, "From mairix@mairix Mon Jan  1 12:34:56 1970\n");
  fwrite (data, sizeof(unsigned char), len, out);
  fputc('\n', out);
  return;
}
/*}}}*/
static void write_to_file(const char *data, size_t len, void *arg)/*{{{*/
{
  FILE *fp;
  if (!(fp = fopen((char *)arg, "w"))) {
    fprintf(stderr, "Cannot write matched message to \"%s\": %s\n", (char *)arg, strerror(errno));
    return;
  }
  fwrite(data, 1, len, fp);
  fclose(fp);
}
/*}}}*/

static int had_failed_checksum;

static void get_validated_mbox_msg(struct read_db *db, int msg_index,/*{{{*/
                                   int *mbox_index,
                                   unsigned char **mbox_data, size_t *mbox_len,
                                   unsigned char **msg_data,  int *msg_len)
{
  /* msg_data==NULL if checksum mismatches */
  unsigned char *start;
  checksum_t csum;
  unsigned int mbi, msgi;

  *msg_data = NULL;
  *msg_len = 0;

  decode_mbox_indices(db->path_offsets[msg_index], &mbi, &msgi);
  *mbox_index = mbi;

  create_ro_mapping(db->data + db->mbox_paths_table[mbi], mbox_data, mbox_len);
  if (!*mbox_data) return;

  start = *mbox_data + db->mtime_table[msg_index];

  /* Ensure that we don't run off the end of the mmap'd file */
  if (db->mtime_table[msg_index] >= *mbox_len)
    *msg_len = 0;
  else if (db->mtime_table[msg_index] + db->size_table[msg_index] >= *mbox_len)
    *msg_len = *mbox_len - db->mtime_table[msg_index];
  else
    *msg_len = db->size_table[msg_index];

  compute_checksum((char *)start, *msg_len, &csum);
  if (!memcmp((db->data + db->mbox_checksum_table[mbi] + (msgi * sizeof(checksum_t))), &csum, sizeof(checksum_t))) {
    *msg_data = start;
  } else {
    had_failed_checksum = 1;
  }
  return;
}
/*}}}*/
static void append_mboxmsg_to_mbox(struct read_db *db, int msg_index, FILE *out)/*{{{*/
{
  /* Need to common up code with try_copy_to_path */
  unsigned char *mbox_start, *msg_start;
  size_t mbox_len;
  int msg_len;
  int mbox_index;

  get_validated_mbox_msg(db, msg_index, &mbox_index, &mbox_start, &mbox_len, &msg_start, &msg_len);
  if (msg_start) {
    /* Artificial from line, we don't have the envelope sender so this is
       going to be artificial anyway. */
    fprintf(out, "From mairix@mairix Mon Jan  1 12:34:56 1970\n");
    fprintf(out, "X-source-folder: %s\n",
            db->data + db->mbox_paths_table[mbox_index]);
    fwrite(msg_start, sizeof(unsigned char), msg_len, out);
    mbox_terminate(msg_start, msg_len, out);
  }
  if (mbox_start) {
    free_ro_mapping(mbox_start, mbox_len);
  }
}
/*}}}*/
static void try_copy_to_path(struct read_db *db, int msg_index, char *target_path)/*{{{*/
{
  unsigned char *data;
  size_t mbox_len;
  int msg_len;
  int mbi;
  FILE *out;
  unsigned char *start;

  get_validated_mbox_msg(db, msg_index, &mbi, &data, &mbox_len, &start, &msg_len);

  if (start) {
    out = fopen(target_path, "wb");
    if (out) {
      fprintf(out, "X-source-folder: %s\n",
              db->data + db->mbox_paths_table[mbi]);
      fwrite(start, sizeof(char), msg_len?msg_len-1:0, out);
      fclose(out);
    }
  }

  if (data) {
    free_ro_mapping(data, mbox_len);
  }
  return;
}
/*}}}*/
static struct msg_src *setup_mbox_msg_src(char *filename, off_t start, size_t len)/*{{{*/
{
  static struct msg_src result;
  result.type = MS_MBOX;
  result.filename = filename;
  result.start = start;
  result.len = len;
  return &result;
}
/*}}}*/

static void get_flags_from_file(struct read_db *db, int idx, int *is_seen, int *is_replied, int *is_flagged)
{
  *is_seen = (db->msg_type_and_flags[idx] & FLAG_SEEN) ? 1 : 0;
  *is_replied = (db->msg_type_and_flags[idx] & FLAG_REPLIED) ? 1 : 0;
  *is_flagged = (db->msg_type_and_flags[idx] & FLAG_FLAGGED) ? 1 : 0;
}

static void string_tolower(char *str)
{
  char *p;
  for (p=str; *p; p++) {
    *p = tolower(*(unsigned char *)p);
  }
}

static int do_search(struct read_db *db, char **args, char *output_path, int show_threads, enum folder_type ft, int verbose, const char *imap_pipe, const char *imap_server, const char *imap_username, const char *imap_password, int please_clear)/*{{{*/
{
  char *colon, *start_words;
  int do_body, do_subject, do_from, do_to, do_cc, do_date, do_size;
  int do_att_name;
  int do_flags;
  int do_path, do_msgid;
  char *key;
  char *hit0, *hit1, *hit2, *hit3;
  int i;
  int n_hits;
  int left_anchor;
  int imap_tried = 0;
  struct imap_ll *imapc;

#define GET_IMAP if (!imap_tried) {\
        imap_tried = 1;\
	if (imap_pipe || imap_server) {\
		imapc = imap_start(imap_pipe, imap_server, imap_username, imap_password);\
	} else {\
		fprintf(stderr, "[No IMAP settings]\n");\
		imapc = NULL;\
	}\
}

  had_failed_checksum = 0;

  hit0 = new_array(char, db->n_msgs);
  hit1 = new_array(char, db->n_msgs);
  hit2 = new_array(char, db->n_msgs);
  hit3 = new_array(char, db->n_msgs);

  /* Argument structure is
   * x:tokena+tokenb,~tokenc,tokend+tokene
   *
   * + (and) binds more tightly than ,
   * , (or)  binds more tightly than separate args
   *
   *
   * hit1 gathers the tokens and'ed with +
   * hit2 gathers the tokens  or'ed with ,
   * hit3 gathers the separate args and'ed with <gap>
   * */


  /* Everything matches until proven otherwise */
  memset(hit3, 1, db->n_msgs);

  while (*args) {
    /* key is a single argument, separate args are and-ed together */
    key = *args++;

    memset(hit2, 0, db->n_msgs);
    memset(hit1, 1, db->n_msgs);

    do_to = 0;
    do_cc = 0;
    do_from = 0;
    do_subject = 0;
    do_body = 0;
    do_date = 0;
    do_size = 0;
    do_path = 0;
    do_msgid = 0;
    do_att_name = 0;
    do_flags = 0;

    colon = strchr(key, ':');

    if (colon) {
      char *p;
      for (p=key; p<colon; p++) {
        switch(*p) {
          case 'b': do_body = 1; break;
          case 's': do_subject = 1; break;
          case 't': do_to = 1; break;
          case 'c': do_cc = 1; break;
          case 'f': do_from = 1; break;
          case 'r': do_to = do_cc = 1; break;
          case 'a': do_to = do_cc = do_from = 1; break;
          case 'd': do_date = 1; break;
          case 'z': do_size = 1; break;
          case 'p': do_path = 1; break;
          case 'm': do_msgid = 1; break;
          case 'n': do_att_name = 1; break;
          case 'F': do_flags = 1; break;
          default: fprintf(stderr, "Unknown key type <%c>\n", *p); break;
        }
      }
      if (do_msgid && (p-key) > 1) {
        fprintf(stderr, "Message-ID key <m> can't be used with other keys\n");
        unlock_and_exit(2);
      }
      start_words = 1 + colon;
    } else {
      do_body = do_subject = do_to = do_cc = do_from = 1;
      start_words = key;
    }

    if (do_date || do_size || do_flags) {
      memset(hit0, 0, db->n_msgs);
      if (do_date) {
        find_date_matches_in_table(db, start_words, hit0);
      } else if (do_size) {
        find_size_matches_in_table(db, start_words, hit0);
      } else if (do_flags) {
        find_flag_matches_in_table(db, start_words, hit0);
      }

      /* AND-combine match vectors */
      for (i=0; i<db->n_msgs; i++) {
        hit1[i] &= hit0[i];
      }
    } else if (do_msgid) {
      char *lower_word = new_string(start_words);
      string_tolower(lower_word);
      memset(hit0, 0, db->n_msgs);
      match_string_in_table2(db, &db->msg_ids, lower_word, hit0);
      free(lower_word);
      /* AND-combine match vectors */
      for (i=0; i<db->n_msgs; i++) {
        hit1[i] &= hit0[i];
      }
    } else {
/*{{{  Scan over separate words within this argument */

    do {
      /* / = 'or' separator
       * , = 'and' separator */
      char *orsep;
      char *andsep;
      char *word, *orig_word, *lower_word;
      char *equal;
      int negate;
      int had_orsep;
      int max_errors;

      orsep = strchr(start_words, '/');
      andsep  = strchr(start_words, ',');
      had_orsep = 0;

      if (andsep && (!orsep || (andsep < orsep))) {
        char *p, *q;
        word = new_array(char, 1 + (andsep - start_words)); /* maybe oversize */
        for (p=word, q=start_words; q < andsep; q++) {
          if (!isspace(*(unsigned char *)q)) {
            *p++ = *q;
          }
        }
        *p = 0;
        start_words = andsep + 1;
      } else if (orsep) { /* comes before + if there's a + */
        char *p, *q;
        word = new_array(char, 1 + (orsep - start_words)); /* maybe oversize */
        for (p=word, q=start_words; q < orsep; q++) {
          if (!isspace(*(unsigned char *)q)) {
            *p++ = *q;
          }
        }
        *p = 0;
        start_words = orsep + 1;
        had_orsep = 1;

      } else {
        word = new_string(start_words);
        while (*start_words) ++start_words;
      }

      orig_word = word;

      if (word[0] == '~') {
        negate = 1;
        word++;
      } else {
        negate = 0;
      }

      if (word[0] == '^') {
        left_anchor = 1;
        word++;
      } else {
        left_anchor = 0;
      }

      equal = strchr(word, '=');
      if (equal && (equal[1] == '\0' || isdigit(equal[1]))) {
        *equal = 0;
        max_errors = atoi(equal + 1);
        /* Extend this to do anchoring etc */
      } else {
        equal = NULL;
        max_errors = 0; /* keep GCC quiet */
      }

      /* Canonicalise search string to lowercase, since the database has all
       * tokens handled that way.  But not for path search! */
      lower_word = new_string(word);
      string_tolower(lower_word);

      memset(hit0, 0, db->n_msgs);
      if (equal) {
        if (do_to) match_substring_in_table(db, &db->to, lower_word, max_errors, left_anchor, hit0);
        if (do_cc) match_substring_in_table(db, &db->cc, lower_word, max_errors, left_anchor, hit0);
        if (do_from) match_substring_in_table(db, &db->from, lower_word, max_errors, left_anchor, hit0);
        if (do_subject) match_substring_in_table(db, &db->subject, lower_word, max_errors, left_anchor, hit0);
        if (do_body) match_substring_in_table(db, &db->body, lower_word, max_errors, left_anchor, hit0);
        if (do_att_name) match_substring_in_table(db, &db->attachment_name, lower_word, max_errors, left_anchor, hit0);
        if (do_path) match_substring_in_paths(db, word, max_errors, left_anchor, hit0);
      } else {
        if (do_to) match_string_in_table(db, &db->to, lower_word, hit0);
        if (do_cc) match_string_in_table(db, &db->cc, lower_word, hit0);
        if (do_from) match_string_in_table(db, &db->from, lower_word, hit0);
        if (do_subject) match_string_in_table(db, &db->subject, lower_word, hit0);
        if (do_body) match_string_in_table(db, &db->body, lower_word, hit0);
        if (do_att_name) match_string_in_table(db, &db->attachment_name, lower_word, hit0);
        /* FIXME */
        if (do_path) match_substring_in_paths(db, word, 0, left_anchor, hit0);
      }

      free(lower_word);

      /* AND-combine match vectors */
      for (i=0; i<db->n_msgs; i++) {
        if (negate) {
          hit1[i] &= !hit0[i];
        } else {
          hit1[i] &= hit0[i];
        }
      }

      if (had_orsep) {
        /* OR-combine match vectors */
        for (i=0; i<db->n_msgs; i++) {
          hit2[i] |= hit1[i];
        }
        memset(hit1, 1, db->n_msgs);
      }

      free(orig_word);

    } while (*start_words);
/*}}}*/
    }

    /* OR-combine match vectors */
    for (i=0; i<db->n_msgs; i++) {
      hit2[i] |= hit1[i];
    }

    /* AND-combine match vectors */
    for (i=0; i<db->n_msgs; i++) {
      hit3[i] &= hit2[i];
    }
  }

  n_hits = 0;

  if (show_threads) {/*{{{*/
    char *tids;
    tids = new_array(char, db->n_msgs);
    memset(tids, 0, db->n_msgs);
    for (i=0; i<db->n_msgs; i++) {
      if (hit3[i]) {
        tids[db->tid_table[i]] = 1;
      }
    }
    for (i=0; i<db->n_msgs; i++) {
      if (tids[db->tid_table[i]]) {
        hit3[i] = 1;
      }
    }
    free(tids);
  }
/*}}}*/
  switch (ft) {
    case FT_MAILDIR:/*{{{*/
      for (i=0; i<db->n_msgs; i++) {
        if (hit3[i]) {
          int is_seen, is_replied, is_flagged;
          get_flags_from_file(db, i, &is_seen, &is_replied, &is_flagged);
          switch (rd_msg_type(db, i)) {
            case DB_MSG_FILE:
              {
                char *target_path;
                char *message_path;
                int is_in_new;
                message_path = db->data + db->path_offsets[i];
                is_in_new = looks_like_maildir_new_p(message_path);
                target_path = mk_maildir_path(i, output_path, is_in_new, is_seen, is_replied, is_flagged);
                create_symlink(message_path, target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_IMAP:
              {
                char *target_path;
                target_path = mk_maildir_path(i, output_path, 0, is_seen, is_replied, is_flagged);
                GET_IMAP;
                if (imapc) imap_fetch_message_raw(db->data + db->path_offsets[i], imapc, write_to_file, target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_MBOX:
              {
                char *target_path = mk_maildir_path(i, output_path, !is_seen, is_seen, is_replied, is_flagged);
                try_copy_to_path(db, i, target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_DEAD:
              break;
          }
        }
      }
      break;
/*}}}*/
    case FT_MH:/*{{{*/
      for (i=0; i<db->n_msgs; i++) {
        if (hit3[i]) {
          switch (rd_msg_type(db, i)) {
            case DB_MSG_FILE:
              {
                char *target_path = mk_mh_path(i, output_path);
                create_symlink(db->data + db->path_offsets[i], target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_IMAP:
              {
                char *target_path = mk_mh_path(i, output_path);
                GET_IMAP;
                if (imapc) imap_fetch_message_raw(db->data + db->path_offsets[i], imapc, write_to_file, target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_MBOX:
              {
                char *target_path = mk_mh_path(i, output_path);
                try_copy_to_path(db, i, target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_DEAD:
              break;
          }
        }
      }
      break;
/*}}}*/
    case FT_MBOX:/*{{{*/
      {
        FILE *out;
        out = fopen(output_path, "ab");
        if (!out) {
          fprintf(stderr, "Cannot open output folder %s\n", output_path);
          unlock_and_exit(1);
        }

        for (i=0; i<db->n_msgs; i++) {
          if (hit3[i]) {
            switch (rd_msg_type(db, i)) {
              case DB_MSG_FILE:
                {
                  append_file_to_mbox(db->data + db->path_offsets[i], out);
                  ++n_hits;
                }
                break;
              case DB_MSG_IMAP:
                {
                  GET_IMAP;
                  if (imapc) imap_fetch_message_raw(db->data + db->path_offsets[i], imapc, append_data_to_mbox, out);
                  ++n_hits;
                }
                break;
              case DB_MSG_MBOX:
                {
                  append_mboxmsg_to_mbox(db, i, out);
                  ++n_hits;
                }
                break;
              case DB_MSG_DEAD:
                break;
            }
          }
        }
        fclose(out);
      }

      break;
/*}}}*/
    case FT_RAW:/*{{{*/
      for (i=0; i<db->n_msgs; i++) {
        if (hit3[i]) {
          switch (rd_msg_type(db, i)) {
            case DB_MSG_FILE:
            case DB_MSG_IMAP:
              {
                ++n_hits;
                printf("%s\n", db->data + db->path_offsets[i]);
              }
              break;
            case DB_MSG_MBOX:
              {
                unsigned int mbix, msgix;
                int start, len, after_end;
                start = db->mtime_table[i];
                len   = db->size_table[i];
                after_end = start + len;
                ++n_hits;
                decode_mbox_indices(db->path_offsets[i], &mbix, &msgix);
                printf("mbox:%s [%d,%d)\n", db->data + db->mbox_paths_table[mbix], start, after_end);
              }
              break;
            case DB_MSG_DEAD:
              break;
          }
        }
      }
      break;
/*}}}*/
    case FT_EXCERPT:/*{{{*/
      for (i=0; i<db->n_msgs; i++) {
        if (hit3[i]) {
          struct rfc822 *parsed = NULL;
          switch (rd_msg_type(db, i)) {
            case DB_MSG_FILE:
              {
                char *filename;
                ++n_hits;
                printf("---------------------------------\n");
                filename = db->data + db->path_offsets[i];
                printf("%s\n", filename);
                parsed = make_rfc822(filename);
              }
              break;
            case DB_MSG_IMAP:
              {
                char *filename;
                ++n_hits;
                printf("---------------------------------\n");
                filename = db->data + db->path_offsets[i];
                printf("%s\n", filename);
                GET_IMAP;
                parsed = imapc ? make_rfc822_from_imap(filename, imapc) : NULL;
              }
              break;
            case DB_MSG_MBOX:
              {
                unsigned int mbix, msgix;
                int start, len, after_end;
                unsigned char *mbox_start, *msg_start;
                size_t mbox_len;
                int msg_len;
                int mbox_index;

                start = db->mtime_table[i];
                len   = db->size_table[i];
                after_end = start + len;
                ++n_hits;
                printf("---------------------------------\n");
                decode_mbox_indices(db->path_offsets[i], &mbix, &msgix);
                printf("mbox:%s [%d,%d)\n", db->data + db->mbox_paths_table[mbix], start, after_end);

                get_validated_mbox_msg(db, i, &mbox_index, &mbox_start, &mbox_len, &msg_start, &msg_len);
                if (msg_start) {
                  enum data_to_rfc822_error error;
                  struct msg_src *msg_src;
                  msg_src = setup_mbox_msg_src(db->data + db->mbox_paths_table[mbix], start, msg_len);
                  parsed = data_to_rfc822(msg_src, (char *) msg_start, msg_len, &error);
                }
                if (mbox_start) {
                  free_ro_mapping(mbox_start, mbox_len);
                }
              }
              break;
            case DB_MSG_DEAD:
              break;
          }

          if (parsed) {
            char datebuf[64];
            struct tm *thetm;
            if (parsed->hdrs.to)      printf("  To:         %s\n", parsed->hdrs.to);
            if (parsed->hdrs.cc)      printf("  Cc:         %s\n", parsed->hdrs.cc);
            if (parsed->hdrs.from)    printf("  From:       %s\n", parsed->hdrs.from);
            if (parsed->hdrs.subject) printf("  Subject:    %s\n", parsed->hdrs.subject);
            if (parsed->hdrs.message_id)
                                      printf("  Message-ID: %s\n", parsed->hdrs.message_id);
            if (parsed->hdrs.in_reply_to)
                                      printf("  In-Reply-To:%s\n", parsed->hdrs.in_reply_to);
            thetm = gmtime(&parsed->hdrs.date);
            strftime(datebuf, sizeof(datebuf), "%a, %d %b %Y", thetm);
            printf("  Date:        %s\n", datebuf);
            free_rfc822(parsed);
          }
        }
      }
      break;
/*}}}*/
    case FT_IMAP:/*{{{*/
      GET_IMAP;
      if (!imapc) break;
      if (please_clear) {
        imap_clear_folder(imapc, output_path);
      }
      for (i=0; i<db->n_msgs; i++) {
        if (hit3[i]) {
          int is_seen, is_replied, is_flagged;
          get_flags_from_file(db, i, &is_seen, &is_replied, &is_flagged);
          switch (rd_msg_type(db, i)) {
            case DB_MSG_FILE:
              {
                int len;
                unsigned char *data;
                create_ro_mapping(db->data + db->path_offsets[i], &data, &len);
                if (data) {
                  imap_append_new_message(imapc, output_path, data, len, is_seen, is_replied, is_flagged);
                  free_ro_mapping(data, len);
                }
                ++n_hits;
              }
              break;
            case DB_MSG_IMAP:
              {
                imap_copy_message(imapc, db->data + db->path_offsets[i], output_path);
                ++n_hits;
              }
              break;
            case DB_MSG_MBOX:
              {
                unsigned char *start, *data;
                int mbox_len, msg_len, mbi;
                get_validated_mbox_msg(db, i, &mbi, &data, &mbox_len, &start, &msg_len);
                imap_append_new_message(imapc, output_path, start, msg_len, is_seen, is_replied, is_flagged);
                if (data) {
                  free_ro_mapping(data, mbox_len);
                }
                ++n_hits;
              }
              break;
            case DB_MSG_DEAD:
              break;
          }
        }
      }
      break;
/*}}}*/
    default:
      assert(0);
      break;
  }

  free(hit0);
  free(hit1);
  free(hit2);
  free(hit3);
  if ((ft != FT_RAW) && (ft != FT_EXCERPT)) {
    printf("Matched %d messages\n", n_hits);
  }
  fflush(stdout);

  if (had_failed_checksum) {
    fprintf(stderr,
            "WARNING : \n"
            "Matches were found in mbox folders but the message checksums failed.\n"
            "You may need to run mairix in indexing mode then repeat your search.\n");
  }

  /* Return error code 1 to the shell if no messages were matched. */
  return (n_hits == 0) ? 1 : 0;
}
/*}}}*/

static int directory_exists_remove_other(char *name)/*{{{*/
{
  struct stat sb;

  if (stat(name, &sb) < 0) {
    return 0;
  }
  if (S_ISDIR(sb.st_mode)) {
    return 1;
  } else {
    /* Try to remove. */
    unlink(name);
    return 0;
  }
}
/*}}}*/
static void create_dir(char *path)/*{{{*/
{
  if (mkdir(path, 0700) < 0) {
    fprintf(stderr, "Could not create directory %s\n", path);
    unlock_and_exit(2);
  }
  fprintf(stderr, "Created directory %s\n", path);
  return;
}
/*}}}*/
static void maybe_create_maildir(char *path)/*{{{*/
{
  char *subdir, *tailpos;
  int len;

  if (!directory_exists_remove_other(path)) {
    create_dir(path);
  }

  len = strlen(path);
  subdir = new_array(char, len + 5);
  strcpy(subdir, path);
  strcpy(subdir+len, "/");
  tailpos = subdir + len + 1;

  strcpy(tailpos,"cur");
  if (!directory_exists_remove_other(subdir)) {
    create_dir(subdir);
  }
  strcpy(tailpos,"new");
  if (!directory_exists_remove_other(subdir)) {
    create_dir(subdir);
  }
  strcpy(tailpos,"tmp");
  if (!directory_exists_remove_other(subdir)) {
    create_dir(subdir);
  }
  free(subdir);
  return;
}
/*}}}*/
static void clear_maildir_subfolder(char *path, char *subdir)/*{{{*/
{
  char *sdir;
  char *fpath;
  int len;
  DIR *d;
  struct dirent *de;
  struct stat sb;

  len = strlen(path) + strlen(subdir);

  sdir = new_array(char, len + 2);
  fpath = new_array(char, len + 3 + NAME_MAX);
  strcpy(sdir, path);
  strcat(sdir, "/");
  strcat(sdir, subdir);

  d = opendir(sdir);
  if (d) {
    while ((de = readdir(d))) {
      strcpy(fpath, sdir);
      strcat(fpath, "/");
      strcat(fpath, de->d_name);
      if (lstat(fpath, &sb) >= 0) {
        /* Deal with both symlinks to maildir/MH messages as well as real files
         * where mbox messages have been written. */
        if (S_ISLNK(sb.st_mode) || S_ISREG(sb.st_mode)) {
          /* FIXME : Can you unlink from a directory while doing a readdir loop over it? */
          if (unlink(fpath) < 0) {
            fprintf(stderr, "Unlinking %s failed\n", fpath);
          }
        }
      }
    }
    closedir(d);
  }

  free(fpath);
  free(sdir);
}
/*}}}*/
static void clear_mh_folder(char *path)/*{{{*/
{
  char *fpath;
  int len;
  DIR *d;
  struct dirent *de;
  struct stat sb;

  len = strlen(path);

  fpath = new_array(char, len + 3 + NAME_MAX);

  d = opendir(path);
  if (d) {
    while ((de = readdir(d))) {
      if (valid_mh_filename_p(de->d_name)) {
        strcpy(fpath, path);
        strcat(fpath, "/");
        strcat(fpath, de->d_name);
        if (lstat(fpath, &sb) >= 0) {
          /* See under maildir above for explanation */
          if (S_ISLNK(sb.st_mode) || S_ISREG(sb.st_mode)) {
            /* FIXME : Can you unlink from a directory while doing a readdir loop over it? */
            if (unlink(fpath) < 0) {
              fprintf(stderr, "Unlinking %s failed\n", fpath);
            }
          }
        }
      }
    }
    closedir(d);
  }

  free(fpath);
}
/*}}}*/
static void clear_mbox_folder(char *path)/*{{{*/
{
  unlink(path);
}
/*}}}*/

int search_top(int do_threads, int do_augment, char *database_path, char *complete_mfolder, char **argv, enum folder_type ft, int verbose, const char *imap_pipe, const char *imap_server, const char *imap_username, const char *imap_password)/*{{{*/
{
  struct read_db *db;
  int result;
  int please_clear = 0;

  db = open_db(database_path);

  switch (ft) {
    case FT_MAILDIR:
      maybe_create_maildir(complete_mfolder);
      break;
    case FT_MH:
      if (!directory_exists_remove_other(complete_mfolder)) {
        create_dir(complete_mfolder);
      }
      break;
    case FT_MBOX:
      /* Nothing to do */
      break;
    case FT_RAW:
    case FT_EXCERPT:
    case FT_IMAP:	/* Do it later, we do not yet have an IMAP connection */
      break;
    default:
      assert(0);
  }

  if (!do_augment) {
    switch (ft) {
      case FT_MAILDIR:
        clear_maildir_subfolder(complete_mfolder, "new");
        clear_maildir_subfolder(complete_mfolder, "cur");
        break;
      case FT_MH:
        clear_mh_folder(complete_mfolder);
        break;
      case FT_MBOX:
        clear_mbox_folder(complete_mfolder);
        break;
      case FT_RAW:
      case FT_EXCERPT:
        break;
      case FT_IMAP:
        /* Do it later: we do not yet have an IMAP connection */
        please_clear = 1;
        break;
      default:
        assert(0);
    }
  }

  result = do_search(db, argv, complete_mfolder, do_threads, ft, verbose, imap_pipe, imap_server, imap_username, imap_password, please_clear);
  free(complete_mfolder);
  close_db(db);
  return result;
}
/*}}}*/


