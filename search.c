/*
  $Header: /cvs/src/mairix/search.c,v 1.1 2002/07/03 22:15:59 richard Exp $

  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002
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
#include <unistd.h>
#include <assert.h>
#include <dirent.h>

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
    assert(idx < db->n_paths);
    hits[idx] = 1;
  }
}
/*}}}*/
static void match_substring_in_table(struct read_db *db, struct toktable_db *tt, char *substring, int max_errors, char *hits)/*{{{*/
{
  /* See "Fast text searching with errors, Sun Wu and Udi Manber, TR 91-11,
     University of Arizona.  I have been informed that this algorithm is NOT
     patented.  This implementation of it is entirely the work of Richard P.
     Curnow - I haven't looked at any related source (webglimpse, agrep etc) in
     writing this.
  */

  int i, len, got_hit;
  unsigned long a[256];
  unsigned long *r, *nr;
  unsigned long hit;
  char *p;

  len = strlen(substring);
  if (len > 31 || len == 0) {
    fprintf(stderr, "Can't match patterns longer than 31 characters or empty\n");
    exit(1);
  }

  /* Set array 'a' to all -1 values */
  memset(a, 0xff, 256 * sizeof(unsigned long));
  for (p=substring, i=0; *p; p++, i++) {
    a[(unsigned int) *(unsigned char *)p] &= ~(1UL << i);
  }
  hit = ~(1UL << (len-1));

  got_hit = 0;
  switch (max_errors) {
    /* Optimise common cases for few errors to allow optimizer to keep bitmaps
     * in registers */
    case 0:/*{{{*/
      for (i=0; i<tt->n; i++) {
        char *token;
        unsigned long r0;
        r0 = ~0;
        got_hit = 0;
        token = db->data + tt->tok_offsets[i];
        for(p=token; *p; p++) {
          int idx = (unsigned int) *(unsigned char *)p;
          r0 = (r0<<1) | a[idx];
          if (~(r0 | hit)) {
            got_hit = 1;
            break;
          }
        }
        if (got_hit) {
          mark_hits_in_table(db, tt, i, hits);
        }
      }
      break;
      /*}}}*/
    case 1:/*{{{*/
      for (i=0; i<tt->n; i++) {
        char *token;
        unsigned long r0, r1, nr0;
        r0 = ~0;
        r1 = r0<<1;
        got_hit = 0;
        token = db->data + tt->tok_offsets[i];
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
        if (got_hit) {
          mark_hits_in_table(db, tt, i, hits);
        }
      }
      break;
/*}}}*/
    case 2:/*{{{*/
      for (i=0; i<tt->n; i++) {
        char *token;
        unsigned long r0, r1, r2, nr0, nr1;
        r0 = ~0;
        r1 = r0<<1;
        r2 = r1<<1;
        got_hit = 0;
        token = db->data + tt->tok_offsets[i];
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
        if (got_hit) {
          mark_hits_in_table(db, tt, i, hits);
        }
      }
      break;
/*}}}*/
    case 3:/*{{{*/
      /* Probably the biggest common case, e.g. two letters maybe transposed +
       * one error elsewhere.  More than 3 diffs leads to too many matches for
       * anything other than very long words anyway. */
      for (i=0; i<tt->n; i++) {
        char *token;
        unsigned long r0, r1, r2, r3, nr0, nr1, nr2;
        r0 = ~0;
        r1 = r0<<1;
        r2 = r1<<1;
        r3 = r2<<1;
        got_hit = 0;
        token = db->data + tt->tok_offsets[i];
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
        if (got_hit) {
          mark_hits_in_table(db, tt, i, hits);
        }
      }
      break;
      /*}}}*/
    default:/*{{{*/
      /* slower (but still very quick!) general case */
      r = new_array(unsigned long, 1 + max_errors);
      nr = new_array(unsigned long, 1 + max_errors);
      
      for (i=0; i<tt->n; i++) {
        int j;
        char *token;
        r[0] = ~0;
        for (j=1; j<=max_errors; j++) {
          r[j] = r[j-1] << 1;
        }
        got_hit = 0;
        token = db->data + tt->tok_offsets[i];
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
        if (got_hit) {
          mark_hits_in_table(db, tt, i, hits);
        }
      }

      free(r);
      free(nr);
      break;
/*}}}*/
  }
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

  for (i=0; i<db->n_paths; i++) {
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
  
  parse_date_range(date_expr, &has_start, &start, &has_end, &end);

  for (i=0; i<db->n_paths; i++) {
    start_cond = has_start ? (db->date_table[i] > start) : 1;
    end_cond   = has_end   ? (db->date_table[i] < end  ) : 1;
    if (start_cond && end_cond) {
      hits[i] = 1;
    }
  }
}
/*}}}*/

static void do_search(struct read_db *db, char **args, char *output_dir, int show_threads)/*{{{*/
{
  char *colon, *start_words;
  int do_body, do_subject, do_from, do_to, do_cc, do_date, do_size;
  char *key;
  char *hit0, *hit1, *hit2, *hit3;
  int i;
  int pid = getpid();
  time_t time_now = time(NULL);
  int n_hits;

  hit0 = new_array(char, db->n_paths);
  hit1 = new_array(char, db->n_paths);
  hit2 = new_array(char, db->n_paths);
  hit3 = new_array(char, db->n_paths);

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
  memset(hit3, 1, db->n_paths);
  
  while (*args) {
    /* key is a single argument, separate args are and-ed together */
    key = *args++;
    
    memset(hit2, 0, db->n_paths);
    memset(hit1, 1, db->n_paths);

    do_to = 0;
    do_cc = 0;
    do_from = 0;
    do_subject = 0;
    do_body = 0;
    do_date = 0;
    do_size = 0;
    
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
          default: fprintf(stderr, "Unknown key type <%c>\n", *p); break;
        }
      }
      start_words = 1 + colon;
    } else {
      do_body = do_subject = do_to = do_cc = do_from = 1;
      start_words = key;
    }

    if (do_date || do_size) {
      memset(hit0, 0, db->n_paths);
      if (do_date) {
        find_date_matches_in_table(db, start_words, hit0);
      } else if (do_size) {
        find_size_matches_in_table(db, start_words, hit0);
      }
      /* AND-combine match vectors */
      for (i=0; i<db->n_paths; i++) {
        hit1[i] &= hit0[i];
      }
    } else {
/*{{{  Scan over separate words within this argument */
    /* Ideas dump:
     * Need to extend this somewhat.  After a word, want to have maybe another
       colon followed by what kind of matching to do.
       :e (default) is exact
       :g           is glob
       :r           is regexp
       :s           is substring
       :a<n>        is approximate (to allow typos) - up to n errors
     *
       Exact matching should use binary chop on datafile for speed
     */
    
    do {
      /* Scan over comma/plus-separated words in a single argument */
      char *comma;
      char *plus;
      char *word, *orig_word;
      char *slash;
      char *p;
      int negate;
      int had_comma;
      int max_errors;
      
      comma = strchr(start_words, ',');
      plus  = strchr(start_words, '+');
      had_comma = 0;

      if (plus && (!comma || (plus < comma))) {
        char *p, *q;
        word = new_array(char, 1 + (plus - start_words)); /* maybe oversize */
        for (p=word, q=start_words; q < plus; q++) {
          if (!isspace(*q)) {
            *p++ = *q;
          }
        }
        *p = 0;
        start_words = plus + 1;
      } else if (comma) { /* comes before + if there's a + */
        char *p, *q;
        word = new_array(char, 1 + (comma - start_words)); /* maybe oversize */
        for (p=word, q=start_words; q < comma; q++) {
          if (!isspace(*q)) {
            *p++ = *q;
          }
        }
        *p = 0;
        start_words = comma + 1;
        had_comma = 1;
        
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

      slash = strchr(word, '/');
      if (slash) {
        *slash = 0;
        max_errors = atoi(slash + 1);
        /* Extend this to do anchoring etc */
      }
      
      /* Canonicalise search string to lowercase, since the database has all
       * tokens handled that way */
      for (p=word; *p; p++) {
        *p = tolower(*p);
      }
      
      memset(hit0, 0, db->n_paths);
      if (slash) {
        if (do_to) match_substring_in_table(db, &db->to, word, max_errors, hit0);
        if (do_cc) match_substring_in_table(db, &db->cc, word, max_errors, hit0);
        if (do_from) match_substring_in_table(db, &db->from, word, max_errors, hit0);
        if (do_subject) match_substring_in_table(db, &db->subject, word, max_errors, hit0);
        if (do_body) match_substring_in_table(db, &db->body, word, max_errors, hit0);
      } else {
        if (do_to) match_string_in_table(db, &db->to, word, hit0);
        if (do_cc) match_string_in_table(db, &db->cc, word, hit0);
        if (do_from) match_string_in_table(db, &db->from, word, hit0);
        if (do_subject) match_string_in_table(db, &db->subject, word, hit0);
        if (do_body) match_string_in_table(db, &db->body, word, hit0);
      }
      
      /* AND-combine match vectors */
      for (i=0; i<db->n_paths; i++) {
        if (negate) {
          hit1[i] &= !hit0[i];
        } else {
          hit1[i] &= hit0[i];
        }
      }

      if (had_comma) {
        /* OR-combine match vectors */
        for (i=0; i<db->n_paths; i++) {
          hit2[i] |= hit1[i];
        }
        memset(hit1, 1, db->n_paths);
      }
      
      free(orig_word);

    } while (*start_words);
/*}}}*/
    }

    /* OR-combine match vectors */
    for (i=0; i<db->n_paths; i++) {
      hit2[i] |= hit1[i];
    }

    /* AND-combine match vectors */
    for (i=0; i<db->n_paths; i++) {
      hit3[i] &= hit2[i];
    }
  }

  n_hits = 0;

  if (show_threads) {
    char *tids;
    tids = new_array(char, db->n_paths);
    memset(tids, 0, db->n_paths);
    for (i=0; i<db->n_paths; i++) {
      if (hit3[i]) {
        tids[db->tid_table[i]] = 1;
      }
    }
    for (i=0; i<db->n_paths; i++) {
      if (tids[db->tid_table[i]]) {
        hit3[i] = 1;
      }
    }
    free(tids);
  }

  for (i=0; i<db->n_paths; i++) {
    if (hit3[i]) {
      if (db->path_offsets[i]) {
        /* File is not dead */
        char *target_path;
        char uniq_buf[48];
        int len;
        
        ++n_hits;
        len = strlen(output_dir) + 64; /* slight oversize */
        target_path = new_array(char, len);
        strcpy(target_path, output_dir);
        strcat(target_path, "/cur/");
        sprintf(uniq_buf, "mairix_%d_%ld_%d:2,S", pid, time_now, i);
        strcat(target_path, uniq_buf);
        
        if (symlink(db->data + db->path_offsets[i], target_path) < 0) {
          perror("symlink");
          /* Don't exit */
        }
        free(target_path);  
      }
    }
  }
  free(hit0);
  free(hit1);
  free(hit2);
  free(hit3);
  printf("Matched %d messages\n", n_hits);
}
/*}}}*/

static void clear_directory(char *path, char *subdir)/*{{{*/
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
        if (S_ISLNK(sb.st_mode)) {
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
void search_top(int do_threads, int do_augment, char *database_path, char *folder_base, char *vfolder, char **argv)/*{{{*/
{
  struct read_db *db;
  char *complete_vfolder;
  int len;

  db = open_db(database_path);

  len = strlen(folder_base) + strlen(vfolder) + 2;
  complete_vfolder = new_array(char, len);
  strcpy(complete_vfolder, folder_base);
  strcat(complete_vfolder, "/");
  strcat(complete_vfolder, vfolder);

  if (!do_augment) {
    clear_directory(complete_vfolder, "new");
    clear_directory(complete_vfolder, "cur");
  }

  do_search(db, argv, complete_vfolder, do_threads);

  close_db(db);
}
/*}}}*/


