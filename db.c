/*
  $Header: /cvs/src/mairix/db.c,v 1.6 2002/12/29 23:43:46 richard Exp $

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

/* Handle complete database */

#include "mairix.h"
#include "reader.h"
#include <ctype.h>
#include <assert.h>

static void check_toktable_enc_integrity(int n_paths, struct toktable *table)/*{{{*/
{
  /* FIXME : Check reachability of tokens that are displaced from their natural
   * hash bucket (if deletions have occurred during purge). */

  int idx, incr;
  int i;
  unsigned char *j, *last_char;
  int broken_chains = 0;
  
  for (i=0; i<table->size; i++) {
    struct token *tok = table->tokens[i];
    if (tok) {
      idx = 0;
      incr = 0;
      last_char = tok->msginfo + tok->n;
      for (j = tok->msginfo; j < last_char; ) {
        incr = read_increment(&j);
        idx += incr;
      }
      if (idx != tok->highest) {
        fprintf(stderr, "broken encoding chain for token <%s>, highest=%ld\n", tok->text, tok->highest);
        fflush(stderr);
        broken_chains = 1;
      }
      if (idx >= n_paths) {
        fprintf(stderr, "end of chain higher than number of message paths (%d) for token <%s>\n", n_paths, tok->text);
        fflush(stderr);
        broken_chains = 1;
      }
    }
  }

  assert(!broken_chains);

}
/*}}}*/
static int compare_strings(const void *a, const void *b)/*{{{*/
{
  const char **aa = (const char **) a;
  const char **bb = (const char **) b;
  return strcmp(*aa, *bb);
}
/*}}}*/
static void check_message_path_integrity(struct database *db)/*{{{*/
{
  /* Check there are no duplicates */
  int i;
  int n;
  int has_duplicate = 0;
  
  char **paths;
  paths = new_array(char *, db->n_paths);
  for (i=0, n=0; i<db->n_paths; i++) {
    if (db->paths[i].path) {
      paths[n++] = db->paths[i].path;
    }
  }

  qsort(paths, n, sizeof(char *), compare_strings);

  for (i=1; i<n; i++) {
    if (!strcmp(paths[i-1], paths[i])) {
      fprintf(stderr, "Path <%s> repeated\n", paths[i]);
      has_duplicate = 1;
    }
  }

  fflush(stderr);
  assert(!has_duplicate);

  free(paths);
  return;
}
/*}}}*/
void check_database_integrity(struct database *db)/*{{{*/
{
  if (verbose) fprintf(stderr, "Checking message path integrity\n");
  check_message_path_integrity(db);
  
  /* Just check encoding chains for now */
  if (verbose) fprintf(stderr, "Checking to\n");
  check_toktable_enc_integrity(db->n_paths, db->to);
  if (verbose) fprintf(stderr, "Checking cc\n");
  check_toktable_enc_integrity(db->n_paths, db->cc);
  if (verbose) fprintf(stderr, "Checking from\n");
  check_toktable_enc_integrity(db->n_paths, db->from);
  if (verbose) fprintf(stderr, "Checking subject\n");
  check_toktable_enc_integrity(db->n_paths, db->subject);
  if (verbose) fprintf(stderr, "Checking body\n");
  check_toktable_enc_integrity(db->n_paths, db->body);
}
/*}}}*/
struct database *new_database(void)/*{{{*/
{
  struct database *result = new(struct database);

  result->to = new_toktable();
  result->cc = new_toktable();
  result->from = new_toktable();
  result->subject = new_toktable();
  result->body = new_toktable();

  result->msg_ids = new_toktable();

  result->paths = NULL;
  result->n_paths = 0;
  result->max_paths = 0;

  return result;
}
/*}}}*/
void free_database(struct database *db)/*{{{*/
{
  int i;

  free_toktable(db->to);
  free_toktable(db->cc);
  free_toktable(db->from);
  free_toktable(db->subject);
  free_toktable(db->body);
  free_toktable(db->msg_ids);

  if (db->paths) {
    for (i=0; i<db->n_paths; i++) {
      if (db->paths[i].path) free(db->paths[i].path);
    }
    free(db->paths);
  }

  free(db);
}
/*}}}*/

static int get_max (int a, int b) {/*{{{*/
  return (a > b) ? a : b;
}
/*}}}*/
static void import_toktable(char *data, int n_paths, struct toktable_db *in, struct toktable *out)/*{{{*/
{
  int n, size, i;

  n = in->n;
  size = 1;
  while (size < n) size <<= 1;
  size <<= 1; /* safe hash table size */

  out->size = size;
  out->mask = size - 1;
  out->n = n;
  out->tokens = new_array(struct token *, size);
  memset(out->tokens, 0, size * sizeof(struct token *));
  out->hwm = (n + size) >> 1;

  for (i=0; i<n; i++) {
    unsigned int hash, index;
    char *text;
    unsigned char *enc;
    int enc_len;
    struct token *nt;
    int enc_hi;
    int idx, incr;
    unsigned char *j;

    /* Recover enc_len and enc_hi from the data */
    enc = (unsigned char *) data + in->enc_offsets[i];
    idx = 0;
    for (j = enc; *j != 0xff; ) {
      incr = read_increment(&j);
      idx += incr;
    }
    enc_len = j - enc;
    enc_hi = idx;

    text = data + in->tok_offsets[i];
    hash = hashfn(text, strlen(text), 0);

    nt = new(struct token);
    nt->hashval = hash;
    nt->text = new_string(text);
    /* Allow a bit of headroom for adding more entries later */
    nt->max = get_max(16, enc_len + (enc_len >> 1));
    nt->n = enc_len;
    nt->highest = enc_hi;
    assert(nt->highest < n_paths);
    nt->msginfo = new_array(char, nt->max);
    memcpy(nt->msginfo, enc, nt->n);

    index = hash & out->mask;
    while (out->tokens[index]) {
      ++index;
      index &= out->mask;
    }

    out->tokens[index] = nt;
  }
}
/*}}}*/
struct database *new_database_from_file(char *db_filename)/*{{{*/
{ 
  /* Read existing database from file for doing incremental update */
  struct database *result;
  struct read_db *input;
  int i, n;
  
  result = new_database();
  input = open_db(db_filename);
  if (!input) {
    /* Nothing to initialise */
    if (verbose) printf("Database file was empty, creating a new database\n");
    return result;
  }

  /* Build pathname information */
  n = result->n_paths = input->n_paths;
  result->max_paths = input->n_paths; /* let it be extended as-and-when */
  result->paths = new_array(struct msgpath, n);
  for (i=0; i<n; i++) {
    if (input->path_offsets[i]) {
      result->paths[i].path = new_string(input->data + input->path_offsets[i]);
    } else {
      result->paths[i].path = NULL;
    }
    result->paths[i].mtime = input->mtime_table[i];
    result->paths[i].date  = input->date_table[i];
    result->paths[i].size  = input->size_table[i];
    result->paths[i].tid   = input->tid_table[i];
  }

  import_toktable(input->data, result->n_paths, &input->to, result->to);
  import_toktable(input->data, result->n_paths, &input->cc, result->cc);
  import_toktable(input->data, result->n_paths, &input->from, result->from);
  import_toktable(input->data, result->n_paths, &input->subject, result->subject);
  import_toktable(input->data, result->n_paths, &input->body, result->body);
  import_toktable(input->data, result->n_paths, &input->msg_ids, result->msg_ids);

  close_db(input);

  check_database_integrity(result);
  
  return result;
}
/*}}}*/

static void add_angled_terms(int file_index, struct toktable *table, char *s)/*{{{*/
{
  char *left, *right;

  if (s) {
    left = strchr(s, '<');
    while (left) {
      right = strchr(left, '>');
      if (right) {
        *right = '\0';
        add_token_in_file(file_index, left+1, table);
        *right = '>'; /* restore */
      } else {
        break;
      }
      left = strchr(right, '<');
    }
  }
}
/*}}}*/

/* Macro for what characters can make up token strings */
static unsigned char special_table[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00-0f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 10-1f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, /* 20-2f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 30-3f */
  2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 40-4f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, /* 50-5f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 60-6f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 70-7f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 80-8f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 90-9f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* a0-af */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* b0-bf */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* c0-cf */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* d0-df */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* e0-ef */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* f0-ff */
};

#define CHAR_VALID(x,mask) (isalnum(x) || (special_table[(unsigned int)(unsigned char) x] & mask))

static void tokenise_string(int file_index, struct toktable *table, char *data, int match_mask)/*{{{*/
{
  char *ss, *es, old_es;
  ss = data;
  for (;;) {
    while (*ss && !CHAR_VALID(*ss,match_mask)) ss++;
    if (!*ss) break;
    es = ss + 1;
    while (*es && CHAR_VALID(*es,match_mask)) es++;
    
    /* deal with token [ss,es) */
    old_es = *es;
    *es = '\0';
    /* FIXME: Ought to do this by passing start and length - clean up later */
    add_token_in_file(file_index, ss, table);
    *es = old_es;

    if (!*es) break;
    ss = es;
  }
}
/*}}}*/
static void tokenise_html_string(int file_index, struct toktable *table, char *data)/*{{{*/
{
  char *ss, *es, old_es;

  /* FIXME : Probably want to rewrite this as an explicit FSM */
  
  ss = data;
  for (;;) {
    /* Assume < and > are never valid token characters ! */
    while (*ss && !CHAR_VALID(*ss, 1)) {
      if (*ss++ == '<') {
        /* Skip over HTML tag */
        while (*ss && (*ss != '>')) ss++;
      }
    }
    if (!*ss) break;
    
    es = ss + 1;
    while (*es && CHAR_VALID(*es, 1)) es++;
    
    /* deal with token [ss,es) */
    old_es = *es;
    *es = '\0';
    /* FIXME: Ought to do this by passing start and length - clean up later */
    add_token_in_file(file_index, ss, table);
    *es = old_es;

    if (!*es) break;
    ss = es;
  }
}
/*}}}*/
static void tokenise_message(int file_index, struct database *db, struct rfc822 *msg)/*{{{*/
{
  struct attachment *a;

  /* Match on whole addresses in these headers as well as the individual words */
  if (msg->hdrs.to) {
    tokenise_string(file_index, db->to, msg->hdrs.to, 1);
    tokenise_string(file_index, db->to, msg->hdrs.to, 2);
  }
  if (msg->hdrs.cc) {
    tokenise_string(file_index, db->cc, msg->hdrs.cc, 1);
    tokenise_string(file_index, db->cc, msg->hdrs.cc, 2);
  }
  if (msg->hdrs.from) {
    tokenise_string(file_index, db->from, msg->hdrs.from, 1);
    tokenise_string(file_index, db->from, msg->hdrs.from, 2);
  }
  if (msg->hdrs.subject) tokenise_string(file_index, db->subject, msg->hdrs.subject, 1);

  for (a=msg->atts.next; a!=&msg->atts; a=a->next) {
    switch (a->ct) {
      case CT_TEXT_PLAIN:
        tokenise_string(file_index, db->body, a->data.normal.bytes, 1);
        break;
      case CT_TEXT_HTML:
        tokenise_html_string(file_index, db->body, a->data.normal.bytes);
        break;
      case CT_MESSAGE_RFC822:
        /* Just recurse for now - maybe we should have separate token tables
         * for tokens occurring in embedded messages? */

        if (a->data.rfc822) {
          tokenise_message(file_index, db, a->data.rfc822);
        }
        break;
      default:
        /* Don't do anything - unknown text format or some nasty binary stuff.
         * In future, we could have all kinds of 'plug-ins' here, e.g.
         * something that can parse PDF to get the basic text strings out of
         * the pages? */
        break;
    }

  }

  /* Deal with threading information */
  add_angled_terms(file_index, db->msg_ids, msg->hdrs.message_id);
  add_angled_terms(file_index, db->msg_ids, msg->hdrs.in_reply_to);
  add_angled_terms(file_index, db->msg_ids, msg->hdrs.references);
}
/*}}}*/
static void scan_new_messages(struct database *db, int start_at)/*{{{*/
{
  int i;
  for (i=start_at; i<db->n_paths; i++) {
    struct rfc822 *msg;
    if (verbose) fprintf(stderr, "Scanning <%s>\n", db->paths[i].path);
    msg = make_rfc822(db->paths[i].path);
    if(msg) 
    {
      db->paths[i].date = msg->hdrs.date;
      tokenise_message(i, db, msg);
      free_rfc822(msg);
    }
    else
      fprintf(stderr, "Skipping...\n");
  }
}
/*}}}*/

static inline void set_bit(unsigned long *x, int n)/*{{{*/
{
  int set;
  unsigned long mask;
  set = (n >> 5);
  mask = (1UL << (n & 31));
  x[set] |= mask;
}
/*}}}*/
static inline int isset_bit(unsigned long *x, int n)/*{{{*/
{
  int set;
  unsigned long mask;
  set = (n >> 5);
  mask = (1UL << (n & 31));
  return (x[set] & mask) ? 1 : 0;
}
/*}}}*/
static int find_base(int *table, int index) {/*{{{*/
  int a = index;

  /* TODO : make this compress the path lengths down to the base entry */
  while (table[a] != a) {
    a = table[a];
  }
  return a;
}
/*}}}*/
static void find_threading(struct database *db)/*{{{*/
{

  /* ix is a table mapping path array index to the lowest path array index that
   * is known to share at least one message ID in its hdrs somewhere (i.e. they
   * must be in the same thread) */
  int *ix;

  int i, m, np, nm, sm;
  int next_tid;
  
  np = db->n_paths;
  nm = db->msg_ids->n;
  sm = db->msg_ids->size;
  
  ix = new_array(int, np);
  for (i=0; i<np; i++) {
    ix[i] = i; /* default - every message in a thread of its own */
  }

  for (m=0; m<sm; m++) {
    struct token *tok = db->msg_ids->tokens[m];
    if (tok) {
      unsigned char *j = tok->msginfo;
      unsigned char *last_char = j + tok->n;
      int cur = 0, incr, first=1;
      int new_base, old_base;
      while (j < last_char) {
        incr = read_increment(&j);
        cur += incr;
        if (first) {
          new_base = find_base(ix, cur);
          first = 0;
        } else {
          old_base = find_base(ix, cur);
          if (old_base < new_base) {
            ix[new_base] = old_base;
            new_base = old_base;
          } else if (old_base > new_base) {
            ix[old_base] = new_base;
          }
        }
      }
    }
  }

  /* Now make each entry point directly to its base */
  for (i=0; i<np; i++) {
    if (ix[i] != i) {
      /* Sure to work as we're going up from the bottom */
      ix[i] = ix[ix[i]];
    }
  }

  /* Now allocate contiguous thread group numbers */
  next_tid = 0;
  for (i=0; i<np; i++) {
    if (ix[i] == i) {
      db->paths[i].tid = next_tid++;
    } else {
      db->paths[i].tid = db->paths[ix[i]].tid;
    }
  }

  free(ix);
  return;
}
/*}}}*/
static int lookup_msgpath(struct msgpath *sorted_paths, int n_paths, char *key)/*{{{*/
{
  /* Implement bisection search */
 int l, h, m, r;
 l = 0, h = n_paths;
 m = -1;
 while (h > l) {
   m = (h + l) >> 1;
   r = strcmp(sorted_paths[m].path, key);
   if (r == 0) break;
   if (l == m) return -1;
   if (r > 0) h = m;
   else       l = m;
 }
 return m;
}
/*}}}*/
static void add_msg_path(struct database *db, char *path, time_t mtime, size_t message_size)/*{{{*/
{
  if (db->n_paths == db->max_paths) {
    if (db->max_paths == 0) {
      db->max_paths = 256;
    } else {
      db->max_paths += (db->max_paths >> 1);
    }
    db->paths = grow_array(struct msgpath, db->max_paths, db->paths);
  }
  db->paths[db->n_paths].path = new_string(path);
  db->paths[db->n_paths].mtime = mtime;
  db->paths[db->n_paths].size = message_size;
  ++db->n_paths;
}
/*}}}*/
int update_database(struct database *db, struct msgpath *sorted_paths, int n_paths)/*{{{*/
{
  /* The incoming list must be sorted into order, to make binary searching
   * possible.  We search for each existing path in the incoming sorted array.
   * If the date differs, or the file no longer exist, the existing database
   * entry for that file is nulled.  (These are only recovered if the database
   * is actively compressed.)  If the date differed, a new entry for the file
   * is put at the end of the list.  Similarly, any new file goes at the end.
   * These new entries are all rescanned to find tokens and add them to the
   * database. */

  char *file_in_db, *file_in_new_list;
  int matched_index;
  int i, new_entries_start_at;
  int any_new, n_pruned;
  
  file_in_db = new_array(char, n_paths);
  file_in_new_list = new_array(char, db->n_paths);
  bzero(file_in_db, n_paths);
  bzero(file_in_new_list, db->n_paths);

  for (i=0; i<db->n_paths; i++) {
    if (db->paths[i].path) {
      matched_index = lookup_msgpath(sorted_paths, n_paths, db->paths[i].path);
      if ((matched_index >= 0) &&
          (sorted_paths[matched_index].mtime == db->paths[i].mtime)) {
      /* Treat stale files as though the path has changed. */
        file_in_db[matched_index] = 1;
        file_in_new_list[i] = 1;
      }
    } else {
      /* Just leave the entry dead */
    }
  }

  /* Add new entries to database */
  new_entries_start_at = db->n_paths;
  
  n_pruned = 0;
  for (i=0; i<db->n_paths; i++) {
    /* Weed dead entries */
    if (!file_in_new_list[i]) {
      if (db->paths[i].path) free(db->paths[i].path);
      db->paths[i].path = NULL;
      ++n_pruned;
    }
  }

  if (verbose) {
    fprintf(stderr, "%d paths no longer exist\n", n_pruned);
  }

  any_new = 0;
  for (i=0; i<n_paths; i++) {
    if (!file_in_db[i]) {
      any_new = 1;
      add_msg_path(db, sorted_paths[i].path, sorted_paths[i].mtime, sorted_paths[i].size);
    }
  }
  
  if (any_new) {
    scan_new_messages(db, new_entries_start_at);
    find_threading(db);
  } else {
    if (verbose) fprintf(stderr, "No new messages found\n");
  }
  
  free(file_in_db);
  free(file_in_new_list);

  return any_new || (n_pruned > 0);
}
/*}}}*/
static void recode_toktable(struct toktable *tbl, int *new_idx)/*{{{*/
{
  /* Re-encode the vectors according to the new path indices */
  unsigned char *new_enc, *old_enc;
  unsigned char *j, *last_char;
  int incr, idx, n_idx;
  int i;
  int any_dead = 0;
  int any_moved, pass;
  
  for (i=0; i<tbl->size; i++) {
    struct token *tok = tbl->tokens[i];
    if (tok) {
      old_enc = tok->msginfo;
      j = old_enc;
      last_char = old_enc + tok->n;
      
      new_enc = new_array(unsigned char, tok->max); /* Probably not bigger than this. */
      tok->n = 0;
      tok->highest = 0;
      tok->msginfo = new_enc;
      idx = 0;

      while (j < last_char) {
        incr = read_increment(&j);
        idx += incr;
        n_idx = new_idx[idx];
        if (n_idx >= 0) {
          check_and_enlarge_tok_encoding(tok);
          insert_index_on_token(tok, n_idx);
        }
      }
      free(old_enc);
      if (tok->n == 0) {
        /* Delete this token.  Gotcha - there may be tokens further on in the
         * array that didn't get their natural hash bucket due to collisions.
         * Need to shuffle such tokens up to guarantee that the buckets between
         * the natural one and the one where they are now are all occupied, to
         * prevent their lookups failing. */

#if 0
        fprintf(stderr, "Token <%s> (bucket %d) no longer has files containing it, deleting\n", tok->text, i);
#endif
        free(tok->text);
        free(new_enc);
        free(tok);
        tbl->tokens[i] = NULL;
        --tbl->n; /* Maintain number in use counter */
        any_dead = 1;
      }

    }
  }


  if (any_dead) {
    /* Now close gaps.  This has to be done in a second pass, otherwise we get a
     * problem with moving entries that need deleting back before the current
       scan point. */

    pass = 1;
    for (;;) {
      int i;

      if (verbose) {
        fprintf(stderr, "Pass %d\n", pass);
      }

      any_moved = 0;

      for (i=0; i<tbl->size; i++) {
        if (tbl->tokens[i]) {
          int nat_bucket_i;
          nat_bucket_i = tbl->tokens[i]->hashval & tbl->mask;
          if (nat_bucket_i != i) {
            /* Find earliest bucket that we could move i to */
            int j = nat_bucket_i;
            while (j != i) {
              if (!tbl->tokens[j]) {
                /* put it here */
#if 0
                fprintf(stderr, "Moved <%s> from bucket %d to %d (natural bucket %d)\n", tbl->tokens[i]->text, i, j, nat_bucket_i);
#endif
                tbl->tokens[j] = tbl->tokens[i];
                tbl->tokens[i] = NULL;
                any_moved = 1;
                break;
              } else {
                j++;
                j &= tbl->mask;
              }
            }
            if (tbl->tokens[i]) {
#if 0
              fprintf(stderr, "NOT moved <%s> from bucket %d (natural bucket %d)\n", tbl->tokens[i]->text, i, nat_bucket_i);
#endif
            }
          }
        }
      }

      if (!any_moved) break;
      pass++;
    }
  }
}
/*}}}*/
int cull_dead_messages(struct database *db)/*{{{*/
{
  /* Return true if any culled */

  int *new_idx, i, j, n_old;
  int any_culled = 0;

  /* Check db is OK before we start on this. (Check afterwards is done in the
   * writer.c code.) */
  check_database_integrity(db);

  if (verbose) {
    fprintf(stderr, "Culling dead messages\n");
  }

  n_old = db->n_paths;

  new_idx = new_array(int, n_old);
  for (i=0, j=0; i<n_old; i++) {
    if (db->paths[i].path) {
      new_idx[i] = j++;
    } else {
      new_idx[i] = -1;
      any_culled = 1;
    }
  }

  recode_toktable(db->to, new_idx);
  recode_toktable(db->cc, new_idx);
  recode_toktable(db->from, new_idx);
  recode_toktable(db->subject, new_idx);
  recode_toktable(db->body, new_idx);
  recode_toktable(db->msg_ids, new_idx);
  
  /* And crunch down the filename table */
  for (i=0, j=0; i<n_old; i++) {
    if (db->paths[i].path) {
      if (i > j) {
        db->paths[j] = db->paths[i];
      }
      j++;
    }
  }
  db->n_paths = j;

  free(new_idx);

  return any_culled;
}
/*}}}*/
