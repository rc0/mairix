/*
  $Header: /cvs/src/mairix/search.c,v 1.29 2004/01/11 23:46:54 richard Exp $

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
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
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
static void build_match_vector(char *substring, unsigned long *a, unsigned long *hit)/*{{{*/
{
  int len;
  char *p;
  int i;

  len = strlen(substring);
  if (len > 31 || len == 0) {
    fprintf(stderr, "Can't match patterns longer than 31 characters or empty\n");
    exit(2);
  }
  memset(a, 0xff, 256 * sizeof(unsigned long));
  for (p=substring, i=0; *p; p++, i++) {
    a[(unsigned int) *(unsigned char *)p] &= ~(1UL << i);
  }
  *hit = ~(1UL << (len-1));
  return;
}
/*}}}*/
static int substring_match_0(unsigned long *a, unsigned long hit, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long r0;
  
  r0 = ~0;
  got_hit = 0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    r0 = (r0<<1) | a[idx];
    if (~(r0 | hit)) {
      got_hit = 1;
      break;
    }
  }
  return got_hit;
}
/*}}}*/
static int substring_match_1(unsigned long *a, unsigned long hit, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long r0, r1, nr0;

  r0 = ~0;
  r1 = r0<<1;
  got_hit = 0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    nr0 = (r0<<1) | a[idx];
    r1  = ((r1<<1) | a[idx]) & ((r0 & nr0) << 1) & r0;
    r0  = nr0;
    if (~((r0 & r1) | hit)) {
      got_hit = 1;
      break;
    }
  }
  return got_hit;
}
/*}}}*/
static int substring_match_2(unsigned long *a, unsigned long hit, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long r0, r1, r2, nr0, nr1;

  r0 = ~0;
  r1 = r0<<1;
  r2 = r1<<1;
  got_hit = 0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    nr0 =  (r0<<1) | a[idx];
    nr1 = ((r1<<1) | a[idx]) & ((r0 & nr0) << 1) & r0;
    r2  = ((r2<<1) | a[idx]) & ((r1 & nr1) << 1) & r1;
    r0  = nr0;
    r1  = nr1;
    if (~((r0 & r1& r2) | hit)) {
      got_hit = 1;
      break;
    }
  }
  return got_hit;
}
/*}}}*/
static int substring_match_3(unsigned long *a, unsigned long hit, char *token)/*{{{*/
{
  int got_hit=0;
  char *p;
  unsigned long r0, r1, r2, r3, nr0, nr1, nr2;

  r0 = ~0;
  r1 = r0<<1;
  r2 = r1<<1;
  r3 = r2<<1;
  got_hit = 0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    nr0 =  (r0<<1) | a[idx];
    nr1 = ((r1<<1) | a[idx]) & ((r0 & nr0) << 1) & r0;
    nr2 = ((r2<<1) | a[idx]) & ((r1 & nr1) << 1) & r1;
    r3  = ((r3<<1) | a[idx]) & ((r2 & nr2) << 1) & r2;
    r0  = nr0;
    r1  = nr1;
    r2  = nr2;
    if (~((r0 & r1 & r2 & r3) | hit)) {
      got_hit = 1;
      break;
    }
  }
  return got_hit;
}
/*}}}*/
static int substring_match_general(unsigned long *a, unsigned long hit, char *token, int max_errors, unsigned long *r, unsigned long *nr)/*{{{*/
{
  int got_hit=0;
  char *p;
  int j;

  r[0] = ~0;
  for (j=1; j<=max_errors; j++) {
    r[j] = r[j-1] << 1;
  }
  got_hit = 0;
  for(p=token; *p; p++) {
    int idx = (unsigned int) *(unsigned char *)p;
    int d;
    unsigned int compo;

    compo = nr[0] = ((r[0]<<1) | a[idx]);
    for (d=1; d<=max_errors; d++) {
      nr[d] = ((r[d]<<1) | a[idx])
        & ((r[d-1] & nr[d-1])<<1)
        & r[d-1];
      compo &= nr[d];
    }
    memcpy(r, nr, (1 + max_errors) * sizeof(unsigned long));
    if (~(compo | hit)) {
      got_hit = 1;
      break;
    }
  }
  return got_hit;
}
/*}}}*/

static void match_substring_in_table(struct read_db *db, struct toktable_db *tt, char *substring, int max_errors, char *hits)/*{{{*/
{

  int i, got_hit;
  unsigned long a[256];
  unsigned long *r=NULL, *nr=NULL;
  unsigned long hit;
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
        got_hit = substring_match_0(a, hit, token);
        break;
      case 1:
        got_hit = substring_match_1(a, hit, token);
        break;
      case 2:
        got_hit = substring_match_2(a, hit, token);
        break;
      case 3:
        got_hit = substring_match_3(a, hit, token);
        break;
      default:
        got_hit = substring_match_general(a, hit, token, max_errors, r, nr);
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
static void match_substring_in_table2(struct read_db *db, struct toktable2_db *tt, char *substring, int max_errors, char *hits)/*{{{*/
{

  int i, got_hit;
  unsigned long a[256];
  unsigned long *r=NULL, *nr=NULL;
  unsigned long hit;
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
        got_hit = substring_match_0(a, hit, token);
        break;
      case 1:
        got_hit = substring_match_1(a, hit, token);
        break;
      case 2:
        got_hit = substring_match_2(a, hit, token);
        break;
      case 3:
        got_hit = substring_match_3(a, hit, token);
        break;
      default:
        got_hit = substring_match_general(a, hit, token, max_errors, r, nr);
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
static void match_substring_in_paths(struct read_db *db, char *substring, int max_errors, char *hits)/*{{{*/
{

  int i;
  unsigned long a[256];
  unsigned long *r=NULL, *nr=NULL;
  unsigned long hit;
  char *token;

  build_match_vector(substring, a, &hit);

  if (max_errors > 3) {
    r = new_array(unsigned long, 1 + max_errors);
    nr = new_array(unsigned long, 1 + max_errors);
  }
  for (i=0; i<db->n_msgs; i++) {
    char *token;
    int mbix, msgix;
    switch (db->msg_type[i]) {
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
        hits[i] = substring_match_0(a, hit, token);
        break;
      case 1:
        hits[i] = substring_match_1(a, hit, token);
        break;
      case 2:
        hits[i] = substring_match_2(a, hit, token);
        break;
      case 3:
        hits[i] = substring_match_3(a, hit, token);
        break;
      default:
        hits[i] = substring_match_general(a, hit, token, max_errors, r, nr);
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
static time_t parse_date_expr(char *x)/*{{{*/
{
  char *p;
  for (p=x; *p; p++) ;
  p--;
  if (!isdigit(*p)) {
    int value;
    value = atoi(x);
    switch (*p) {
      case 'd': value *= 86400; break;
      case 'w': value *= 7 * 86400; break;
      case 'm': value *= 30 * 86400; break;
      case 'y': value *= 365 * 86400; break;
    }
    return time(NULL) - value;
  } else {
    int len = strlen(x);
    int day, month, year;
    int n;
    struct tm tm;
    time_t now;
    time(&now);
    tm = *localtime(&now);
    
    switch (len) {
      case 2:
        n = sscanf(x, "%2d", &day);
        if (n != 1) {
          fprintf(stderr, "Can't parse day from %s\n", x);
          return (time_t) 0; /* arbitrary */
        }
        tm.tm_mday = day;
        break;
      case 4:
        n = sscanf(x, "%2d%2d", &month, &day);
        if (n != 2) {
          fprintf(stderr, "Can't parse month and day from %s\n", x);
          return (time_t) 0; /* arbitrary */
        }
        tm.tm_mday = day;
        tm.tm_mon = month;
        break;
      case 6:
        n = sscanf(x, "%2d%2d%2d", &year, &month, &day);
        if (n != 3) {
          fprintf(stderr, "Can't parse year, month and day from %s\n", x);
          return (time_t) 0; /* arbitrary */
        }
        if (year < 70) year += 100;
        tm.tm_mday = day;
        tm.tm_mon = month;
        tm.tm_year = year;
        break;
      case 8:
        n = sscanf(x, "%4d%2d%2d", &year, &month, &day);
        if (n != 3) {
          fprintf(stderr, "Can't parse year, month and day from %s\n", x);
          return (time_t) 0; /* arbitrary */
        }
        year -= 1900;
        tm.tm_mday = day;
        tm.tm_mon = month;
        tm.tm_year = year;
        break;
      default:
       fprintf(stderr, "Can't parse date from %s\n", x);
       return (time_t) 0; /* arbitrary */
       break;
    }
    return mktime(&tm);
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
static void parse_date_range(char *date_expr, int *has_start, time_t *start, int *has_end, time_t *end)/*{{{*/
{
  char *x = date_expr;
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
      *start = parse_date_expr(s);
      *has_start = 1;
      free(s);
    }
    if (dash[1]) { /* dash not at end of arg */
      char *e;
      e = new_array(char, (x + len) - dash);
      for (p=e, q=dash+1; *q; ) *p++ = *q++;
      *p = 0;
      *end = parse_date_expr(e);
      *has_end = 1;
      free(e);
    }
  } else {
    *has_start = 0;
    *end = parse_date_expr(date_expr);
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
    exit (2);
  }

#if 0
  parse_date_range(date_expr, &has_start, &start, &has_end, &end);
#endif
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

static char *mk_maildir_path(int token, char *output_dir)/*{{{*/
{
  char *result; 
  char uniq_buf[48];
  int len;

  len = strlen(output_dir) + 64; /* oversize */
  result = new_array(char, len);
  strcpy(result, output_dir);
  strcat(result, "/cur/");
  sprintf(uniq_buf, "mairix_%d:2,S", token);
  strcat(result, uniq_buf);
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
static void create_symlink(char *link_target, char *new_link)/*{{{*/
{
  if (symlink(link_target, new_link) < 0) {
    if (verbose) {
      perror("symlink");
      fprintf(stderr, "Failed path <%s> -> <%s>\n", link_target, new_link);
    }
  }
}
/*}}}*/
static void append_file_to_mbox(const char *path, FILE *out)/*{{{*/
{
  unsigned char *data;
  int len;
  create_ro_mapping(path, &data, &len);
  if (data) {
    fprintf(out, "From mairix@mairix Mon Jan 1 12:34:56 1970\n");
    fwrite (data, sizeof(unsigned char), len, out);
    munmap(data, len);
  }
  return;
}
/*}}}*/

static int had_failed_checksum;

static void get_validated_mbox_msg(struct read_db *db, int msg_index,/*{{{*/
                                   int *mbox_index,
                                   unsigned char **mbox_data, int *mbox_len,
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
  *msg_len = db->size_table[msg_index];
  compute_checksum(start, *msg_len, &csum);
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
  int mbox_len, msg_len;
  int mbox_index;

  get_validated_mbox_msg(db, msg_index, &mbox_index, &mbox_start, &mbox_len, &msg_start, &msg_len);
  if (msg_start) {
    fprintf(out, "From mairix@mairix Mon Jan 1 12:34:56 1970\n");
    fwrite(msg_start, sizeof(unsigned char), msg_len, out);
  }
  if (mbox_start) {
    munmap(mbox_start, mbox_len);
  }
}
/*}}}*/
static void try_copy_to_path(struct read_db *db, int msg_index, char *target_path)/*{{{*/
{
  unsigned char *data;
  int mbox_len, msg_len;
  int mbi;
  FILE *out;
  unsigned char *start;

  get_validated_mbox_msg(db, msg_index, &mbi, &data, &mbox_len, &start, &msg_len);

  if (start) {
    out = fopen(target_path, "wb");
    if (out) {
      /* Artificial from line, we don't have the envelope sender so this is
         going to be artificial anyway. */
      fwrite(start, sizeof(char), msg_len, out);
      fclose(out);
    }
  }

  if (data) {
    munmap(data, mbox_len);
  }
  return;
}
/*}}}*/
static int do_search(struct read_db *db, char **args, char *output_path, int show_threads, enum folder_type ft, int verbose)/*{{{*/
{
  char *colon, *start_words;
  int do_body, do_subject, do_from, do_to, do_cc, do_date, do_size;
  int do_path, do_msgid;
  char *key;
  char *hit0, *hit1, *hit2, *hit3;
  int i;
  int n_hits;

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
          default: fprintf(stderr, "Unknown key type <%c>\n", *p); break;
        }
      }
      start_words = 1 + colon;
    } else {
      do_body = do_subject = do_to = do_cc = do_from = 1;
      start_words = key;
    }

    if (do_date || do_size) {
      memset(hit0, 0, db->n_msgs);
      if (do_date) {
        find_date_matches_in_table(db, start_words, hit0);
      } else if (do_size) {
        find_size_matches_in_table(db, start_words, hit0);
      }
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
      char *p;
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
          if (!isspace(*q)) {
            *p++ = *q;
          }
        }
        *p = 0;
        start_words = andsep + 1;
      } else if (orsep) { /* comes before + if there's a + */
        char *p, *q;
        word = new_array(char, 1 + (orsep - start_words)); /* maybe oversize */
        for (p=word, q=start_words; q < orsep; q++) {
          if (!isspace(*q)) {
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

      equal = strchr(word, '=');
      if (equal) {
        *equal = 0;
        max_errors = atoi(equal + 1);
        /* Extend this to do anchoring etc */
      } else {
        max_errors = 0; /* keep GCC quiet */
      }

      /* Canonicalise search string to lowercase, since the database has all
       * tokens handled that way.  But not for path search! */
      lower_word = new_string(word);
      for (p=lower_word; *p; p++) {
        *p = tolower(*p);
      }

      memset(hit0, 0, db->n_msgs);
      if (equal) {
        if (do_to) match_substring_in_table(db, &db->to, lower_word, max_errors, hit0);
        if (do_cc) match_substring_in_table(db, &db->cc, lower_word, max_errors, hit0);
        if (do_from) match_substring_in_table(db, &db->from, lower_word, max_errors, hit0);
        if (do_subject) match_substring_in_table(db, &db->subject, lower_word, max_errors, hit0);
        if (do_body) match_substring_in_table(db, &db->body, lower_word, max_errors, hit0);
        if (do_path) match_substring_in_paths(db, word, max_errors, hit0);
        if (do_msgid) match_substring_in_table2(db, &db->msg_ids, lower_word, max_errors, hit0);
      } else {
        if (do_to) match_string_in_table(db, &db->to, lower_word, hit0);
        if (do_cc) match_string_in_table(db, &db->cc, lower_word, hit0);
        if (do_from) match_string_in_table(db, &db->from, lower_word, hit0);
        if (do_subject) match_string_in_table(db, &db->subject, lower_word, hit0);
        if (do_body) match_string_in_table(db, &db->body, lower_word, hit0);
        /* FIXME */
        if (do_path) match_substring_in_paths(db, word, 0, hit0);
        if (do_msgid) match_string_in_table2(db, &db->msg_ids, lower_word, hit0);
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
          switch (db->msg_type[i]) {
            case DB_MSG_FILE:
              {
                char *target_path = mk_maildir_path(i, output_path);
                create_symlink(db->data + db->path_offsets[i], target_path);
                free(target_path);
                ++n_hits;
              }
              break;
            case DB_MSG_MBOX:
              {
                char *target_path = mk_maildir_path(i, output_path);
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
          switch (db->msg_type[i]) {
            case DB_MSG_FILE:
              {
                char *target_path = mk_mh_path(i, output_path);
                create_symlink(db->data + db->path_offsets[i], target_path);
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
          exit(1);
        }

        for (i=0; i<db->n_msgs; i++) {
          if (hit3[i]) {
            switch (db->msg_type[i]) {
              case DB_MSG_FILE:
                {
                  append_file_to_mbox(db->data + db->path_offsets[i], out);
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
          switch (db->msg_type[i]) {
            case DB_MSG_FILE:
              {
                ++n_hits;
                printf("%s\n", db->data + db->path_offsets[i]);
              }
              break;
            case DB_MSG_MBOX:
              {
                int mbix, msgix;
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
      printf("\n");
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
  printf("Matched %d messages\n", n_hits);
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
static int is_file_or_nothing(char *name)/*{{{*/
{
  struct stat sb;
  if (stat(name, &sb) < 0) {
    if (errno == ENOENT)
      return 1;
    else
      return 0;
  }
  if (S_ISREG(sb.st_mode))
    return 1;
  else
    return 0;
}
/*}}}*/
static void create_dir(char *path)/*{{{*/
{
  if (mkdir(path, 0700) < 0) {
    fprintf(stderr, "Could not create directory %s\n", path);
    exit(2);
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
      if (is_integer_string(de->d_name)) {
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

int search_top(int do_threads, int do_augment, char *database_path, char *folder_base, char *mfolder, char **argv, enum folder_type ft, int verbose)/*{{{*/
{
  struct read_db *db;
  char *complete_mfolder;
  int len;
  int result;

  db = open_db(database_path);

  if ((mfolder[0] == '/') || (mfolder[0] == '.')) {
    complete_mfolder = new_string(mfolder);
  } else {
    len = strlen(folder_base) + strlen(mfolder) + 2;
    complete_mfolder = new_array(char, len);
    strcpy(complete_mfolder, folder_base);
    strcat(complete_mfolder, "/");
    strcat(complete_mfolder, mfolder);
  }

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
        break;
      default:
        assert(0);
    }
  }

  result = do_search(db, argv, complete_mfolder, do_threads, ft, verbose);
  free(complete_mfolder);
  close_db(db);
  return result;
}
/*}}}*/


