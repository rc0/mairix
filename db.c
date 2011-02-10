/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002,2003,2004,2005,2006,2007,2009
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

/* Handle complete database */

#include "mairix.h"
#include "reader.h"
#include <ctype.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

struct sortable_token {/*{{{*/
  char *text;
  int index;
};
/*}}}*/
static int compare_sortable_tokens(const void *a, const void *b)/*{{{*/
{
  const struct sortable_token *aa = (const struct sortable_token *) a;
  const struct sortable_token *bb = (const struct sortable_token *) b;
  int foo;
  foo = strcmp(aa->text, bb->text);
  if (foo) {
    return foo;
  } else {
    if (aa->index < bb->index) return -1;
    else if (aa->index > bb->index) return +1;
    else return 0;
  }
}
/*}}}*/
static void check_toktable_enc_integrity(int n_msgs, struct toktable *table)/*{{{*/
{
  /* FIXME : Check reachability of tokens that are displaced from their natural
   * hash bucket (if deletions have occurred during purge). */

  int idx, incr;
  int i, k;
  unsigned char *j, *last_char;
  int broken_chains = 0;
  struct sortable_token *sort_list;
  int any_duplicates;

  for (i=0; i<table->size; i++) {
    struct token *tok = table->tokens[i];
    if (tok) {
      idx = 0;
      incr = 0;
      last_char = tok->match0.msginfo + tok->match0.n;
      for (j = tok->match0.msginfo; j < last_char; ) {
        incr = read_increment(&j);
        idx += incr;
      }
      if (idx != tok->match0.highest) {
        fprintf(stderr, "broken encoding chain for token <%s>, highest=%ld\n", tok->text, tok->match0.highest);
        fflush(stderr);
        broken_chains = 1;
      }
      if (idx >= n_msgs) {
        fprintf(stderr, "end of chain higher than number of message paths (%d) for token <%s>\n", n_msgs, tok->text);
        fflush(stderr);
        broken_chains = 1;
      }
    }
  }

  assert(!broken_chains);

  /* Check there are no duplicated tokens in the table. */
  sort_list = new_array(struct sortable_token, table->n);
  k = 0;
  for (i=0; i<table->size; i++) {
    struct token *tok = table->tokens[i];
    if (tok) {
      sort_list[k].text = new_string(tok->text);
      sort_list[k].index = i;
      k++;
    }
  }
  assert(k == table->n);

  qsort(sort_list, table->n, sizeof(struct sortable_token), compare_sortable_tokens);
  /* Check for uniqueness of neighbouring token texts */
  any_duplicates = 0;
  for (i=0; i<(table->n - 1); i++) {
    if (!strcmp(sort_list[i].text, sort_list[i+1].text)) {
      fprintf(stderr, "Token table contains duplicated token %s at indices %d and %d\n",
               sort_list[i].text, sort_list[i].index, sort_list[i+1].index);
      any_duplicates = 1;
    }
  }

  /* release */
  for (i=0; i<table->n; i++) {
    free(sort_list[i].text);
  }
  free(sort_list);

  if (any_duplicates) {
    fprintf(stderr, "Token table contained duplicate entries, aborting\n");
    assert(0);
  }
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
  /* TODO : for now only checks integrity of non-mbox paths. */
  /* Check there are no duplicates */
  int i;
  int n;
  int has_duplicate = 0;

  char **paths;
  paths = new_array(char *, db->n_msgs);
  for (i=0, n=0; i<db->n_msgs; i++) {
    switch (db->type[i]) {
      case MTY_DEAD:
      case MTY_MBOX:
        break;
      case MTY_FILE:
        paths[n++] = db->msgs[i].src.mpf.path;
        break;
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
  check_toktable_enc_integrity(db->n_msgs, db->to);
  if (verbose) fprintf(stderr, "Checking cc\n");
  check_toktable_enc_integrity(db->n_msgs, db->cc);
  if (verbose) fprintf(stderr, "Checking from\n");
  check_toktable_enc_integrity(db->n_msgs, db->from);
  if (verbose) fprintf(stderr, "Checking subject\n");
  check_toktable_enc_integrity(db->n_msgs, db->subject);
  if (verbose) fprintf(stderr, "Checking body\n");
  check_toktable_enc_integrity(db->n_msgs, db->body);
  if (verbose) fprintf(stderr, "Checking attachment_name\n");
  check_toktable_enc_integrity(db->n_msgs, db->attachment_name);
}
/*}}}*/
struct database *new_database(unsigned int hash_key)/*{{{*/
{
  struct database *result = new(struct database);
  struct timeval tv;
  pid_t  pid;

  result->to = new_toktable();
  result->cc = new_toktable();
  result->from = new_toktable();
  result->subject = new_toktable();
  result->body = new_toktable();
  result->attachment_name = new_toktable();

  result->msg_ids = new_toktable2();

  if ( hash_key == CREATE_RANDOM_DATABASE_HASH )
    {
      gettimeofday(&tv, NULL);
      pid = getpid();
      hash_key = tv.tv_sec ^ (pid ^ (tv.tv_usec << 15));
    }
  result->hash_key = hash_key;

  result->msgs = NULL;
  result->type = NULL;
  result->n_msgs = 0;
  result->max_msgs = 0;

  result->mboxen = NULL;
  result->n_mboxen = 0;
  result->max_mboxen = 0;

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
  free_toktable(db->attachment_name);
  free_toktable2(db->msg_ids);

  if (db->msgs) {
    for (i=0; i<db->n_msgs; i++) {
      switch (db->type[i]) {
        case MTY_DEAD:
          break;
        case MTY_MBOX:
          break;
        case MTY_FILE:
          assert(db->msgs[i].src.mpf.path);
          free(db->msgs[i].src.mpf.path);
          break;
      }
    }
    free(db->msgs);
    free(db->type);
  }

  free(db);
}
/*}}}*/

static int get_max (int a, int b) {/*{{{*/
  return (a > b) ? a : b;
}
/*}}}*/
static void import_toktable(char *data, unsigned int hash_key, int n_msgs, struct toktable_db *in, struct toktable *out)/*{{{*/
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
    hash = hashfn((unsigned char *) text, strlen(text), hash_key);

    nt = new(struct token);
    nt->hashval = hash;
    nt->text = new_string(text);
    /* Allow a bit of headroom for adding more entries later */
    nt->match0.max = get_max(16, enc_len + (enc_len >> 1));
    nt->match0.n = enc_len;
    nt->match0.highest = enc_hi;
    assert(nt->match0.highest < n_msgs);
    nt->match0.msginfo = new_array(unsigned char, nt->match0.max);
    memcpy(nt->match0.msginfo, enc, nt->match0.n);

    index = hash & out->mask;
    while (out->tokens[index]) {
      /* Audit to look for corrupt database with multiple entries for the same
       * string. */
      if (!strcmp(nt->text, out->tokens[index]->text)) {
        fprintf(stderr, "\n!!! Corrupt token table found in database, token <%s> duplicated, aborting\n",
            nt->text);
        fprintf(stderr, "  Delete the database file and rebuild from scratch as a workaround\n");
        /* No point going on - need to find out why the database got corrupted
         * in the 1st place.  Workaround for user - rebuild database from
         * scratch by deleting it then rerunning. */
        unlock_and_exit(1);
      }
      ++index;
      index &= out->mask;
    }

    out->tokens[index] = nt;
  }
}
/*}}}*/
static void import_toktable2(char *data, unsigned int hash_key, int n_msgs, struct toktable2_db *in, struct toktable2 *out)/*{{{*/
{
  int n, size, i;

  n = in->n;
  size = 1;
  while (size < n) size <<= 1;
  size <<= 1; /* safe hash table size */

  out->size = size;
  out->mask = size - 1;
  out->n = n;
  out->tokens = new_array(struct token2 *, size);
  memset(out->tokens, 0, size * sizeof(struct token *));
  out->hwm = (n + size) >> 1;

  for (i=0; i<n; i++) {
    unsigned int hash, index;
    char *text;
    struct token2 *nt;
    unsigned char *enc0, *enc1;
    int enc0_len, enc1_len;
    int enc0_hi, enc1_hi;
    int idx, incr;
    unsigned char *j;

/*{{{ do enc0*/
    enc0 = (unsigned char *) data + in->enc0_offsets[i];
    idx = 0;
    for (j = enc0; *j != 0xff; ) {
      incr = read_increment(&j);
      idx += incr;
    }
    enc0_len = j - enc0;
    enc0_hi = idx;
/*}}}*/
/*{{{ do enc1*/
    enc1 = (unsigned char *) data + in->enc1_offsets[i];
    idx = 0;
    for (j = enc1; *j != 0xff; ) {
      incr = read_increment(&j);
      idx += incr;
    }
    enc1_len = j - enc1;
    enc1_hi = idx;
/*}}}*/

    text = data + in->tok_offsets[i];
    hash = hashfn((unsigned char *) text, strlen(text), hash_key);

    nt = new(struct token2);
    nt->hashval = hash;
    nt->text = new_string(text);
    /* Allow a bit of headroom for adding more entries later */
    /*{{{ set up match0 chain */
    nt->match0.max = get_max(16, enc0_len + (enc0_len >> 1));
    nt->match0.n = enc0_len;
    nt->match0.highest = enc0_hi;
    assert(nt->match0.highest < n_msgs);
    nt->match0.msginfo = new_array(unsigned char, nt->match0.max);
    memcpy(nt->match0.msginfo, enc0, nt->match0.n);
    /*}}}*/
    /*{{{ set up match1 chain */
    nt->match1.max = get_max(16, enc1_len + (enc1_len >> 1));
    nt->match1.n = enc1_len;
    nt->match1.highest = enc1_hi;
    assert(nt->match1.highest < n_msgs);
    nt->match1.msginfo = new_array(unsigned char, nt->match1.max);
    memcpy(nt->match1.msginfo, enc1, nt->match1.n);
    /*}}}*/

    index = hash & out->mask;
    while (out->tokens[index]) {
      ++index;
      index &= out->mask;
    }

    out->tokens[index] = nt;
  }
}
/*}}}*/
struct database *new_database_from_file(char *db_filename, int do_integrity_checks)/*{{{*/
{
  /* Read existing database from file for doing incremental update */
  struct database *result;
  struct read_db *input;
  int i, n, N;

  result = new_database( CREATE_RANDOM_DATABASE_HASH );
  input = open_db(db_filename);
  if (!input) {
    /* Nothing to initialise */
    if (verbose) printf("Database file was empty, creating a new database\n");
    return result;
  }

  /* Build pathname information */
  n = result->n_msgs = input->n_msgs;
  result->max_msgs = input->n_msgs; /* let it be extended as-and-when */
  result->msgs = new_array(struct msgpath, n);
  result->type = new_array(enum message_type, n);

  result->hash_key = input->hash_key;

  /* Set up mbox structures */
  N = result->n_mboxen = result->max_mboxen = input->n_mboxen;
  result->mboxen = N ? (new_array(struct mbox, N)) : NULL;
  for (i=0; i<N; i++) {
    int nn;
    if (input->mbox_paths_table[i]) {
      result->mboxen[i].path = new_string(input->data + input->mbox_paths_table[i]);
    } else {
      /* mbox is dead. */
      result->mboxen[i].path = NULL;
    }
    result->mboxen[i].file_mtime = input->mbox_mtime_table[i];
    result->mboxen[i].file_size  = input->mbox_size_table[i];
    nn = result->mboxen[i].n_msgs = input->mbox_entries_table[i];
    result->mboxen[i].max_msgs = nn;
    result->mboxen[i].start = new_array(off_t, nn);
    result->mboxen[i].len   = new_array(size_t, nn);
    result->mboxen[i].check_all = new_array(checksum_t, nn);
    /* Copy the entire checksum table in one go. */
    memcpy(result->mboxen[i].check_all,
           input->data + input->mbox_checksum_table[i],
           nn * sizeof(checksum_t));
    result->mboxen[i].n_so_far = 0;
  }

  for (i=0; i<n; i++) {
    switch (rd_msg_type(input, i)) {
      case DB_MSG_DEAD:
        result->type[i] = MTY_DEAD;
        break;
      case DB_MSG_FILE:
        result->type[i] = MTY_FILE;
        result->msgs[i].src.mpf.path = new_string(input->data + input->path_offsets[i]);
        result->msgs[i].src.mpf.mtime = input->mtime_table[i];
        result->msgs[i].src.mpf.size  = input->size_table[i];
        break;
      case DB_MSG_MBOX:
        {
          unsigned int mbi, msgi;
          int n;
          struct mbox *mb;
          result->type[i] = MTY_MBOX;
          decode_mbox_indices(input->path_offsets[i], &mbi, &msgi);
          result->msgs[i].src.mbox.file_index = mbi;
          mb = &result->mboxen[mbi];
          assert(mb->n_so_far == msgi);
          n = mb->n_so_far;
          result->msgs[i].src.mbox.msg_index = n;
          mb->start[n] = input->mtime_table[i];
          mb->len[n] = input->size_table[i];
          ++mb->n_so_far;
        }

        break;
    }
    result->msgs[i].seen    = (input->msg_type_and_flags[i] & FLAG_SEEN)    ? 1:0;
    result->msgs[i].replied = (input->msg_type_and_flags[i] & FLAG_REPLIED) ? 1:0;
    result->msgs[i].flagged = (input->msg_type_and_flags[i] & FLAG_FLAGGED) ? 1:0;
    result->msgs[i].date  = input->date_table[i];
    result->msgs[i].tid   = input->tid_table[i];
  }

  import_toktable(input->data, input->hash_key, result->n_msgs, &input->to, result->to);
  import_toktable(input->data, input->hash_key, result->n_msgs, &input->cc, result->cc);
  import_toktable(input->data, input->hash_key, result->n_msgs, &input->from, result->from);
  import_toktable(input->data, input->hash_key, result->n_msgs, &input->subject, result->subject);
  import_toktable(input->data, input->hash_key, result->n_msgs, &input->body, result->body);
  import_toktable(input->data, input->hash_key, result->n_msgs, &input->attachment_name, result->attachment_name);
  import_toktable2(input->data, input->hash_key, result->n_msgs, &input->msg_ids, result->msg_ids);

  close_db(input);

  if (do_integrity_checks) {
    check_database_integrity(result);
  }

  return result;
}
/*}}}*/

static void add_angled_terms(int file_index, unsigned int hash_key, struct toktable2 *table, int add_to_chain1, char *s)/*{{{*/
{
  char *left, *right;

  if (s) {
    left = strchr(s, '<');
    while (left) {
      right = strchr(left, '>');
      if (right) {
        *right = '\0';
        add_token2_in_file(file_index, hash_key, left+1, table, add_to_chain1);
        *right = '>'; /* restore */
      } else {
        break;
      }
      left = strchr(right, '<');
    }
  }
}
/*}}}*/

/* Macro for what characters can make up token strings.

   The following characters have special meanings:
   0x2b +
   0x2d -
   0x2e .
   0x40 @
   0x5f _

   since they can occur within email addresses and message IDs when considered
   as a whole rather than as individual words.  Underscore (0x5f) is considered
   a word-character always too.

   */
static unsigned char special_table[256] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00-0f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 10-1f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 2, 0, /* 20-2f */
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

#if 0
#define CHAR_VALID(x,mask) (isalnum((unsigned char) x) || (special_table[(unsigned int)(unsigned char) x] & mask))
#endif
static inline int char_valid_p(char x, unsigned int mask)/*{{{*/
{
  unsigned char xx = (unsigned char) x;
  if (isalnum(xx)) return 1;
  else if (special_table[(unsigned int) xx] & mask) return 1;
  else return 0;
}
/*}}}*/
static void tokenise_string(int file_index, unsigned int hash_key, struct toktable *table, char *data, int match_mask)/*{{{*/
{
  char *ss, *es, old_es;
  ss = data;
  for (;;) {
    while (*ss && !char_valid_p(*ss,match_mask)) ss++;
    if (!*ss) break;
    es = ss + 1;
    while (*es && char_valid_p(*es,match_mask)) es++;

    /* deal with token [ss,es) */
    old_es = *es;
    *es = '\0';
    /* FIXME: Ought to do this by passing start and length - clean up later */
    add_token_in_file(file_index, hash_key, ss, table);
    *es = old_es;

    if (!*es) break;
    ss = es;
  }
}
/*}}}*/
static void tokenise_html_string(int file_index, unsigned int hash_key, struct toktable *table, char *data)/*{{{*/
{
  char *ss, *es, old_es;

  /* FIXME : Probably want to rewrite this as an explicit FSM */

  ss = data;
  for (;;) {
    /* Assume < and > are never valid token characters ! */
    while (*ss && !char_valid_p(*ss, 1)) {
      if (*ss++ == '<') {
        /* Skip over HTML tag */
        while (*ss && (*ss != '>')) ss++;
      }
    }
    if (!*ss) break;

    es = ss + 1;
    while (*es && char_valid_p(*es, 1)) es++;

    /* deal with token [ss,es) */
    old_es = *es;
    *es = '\0';
    /* FIXME: Ought to do this by passing start and length - clean up later */
    add_token_in_file(file_index, hash_key, ss, table);
    *es = old_es;

    if (!*es) break;
    ss = es;
  }
}
/*}}}*/
void tokenise_message(int file_index, struct database *db, struct rfc822 *msg)/*{{{*/
{
  struct attachment *a;

  /* Match on whole addresses in these headers as well as the individual words */
  if (msg->hdrs.to) {
    tokenise_string(file_index, db->hash_key, db->to, msg->hdrs.to, 1);
    tokenise_string(file_index, db->hash_key, db->to, msg->hdrs.to, 2);
  }
  if (msg->hdrs.cc) {
    tokenise_string(file_index, db->hash_key, db->cc, msg->hdrs.cc, 1);
    tokenise_string(file_index, db->hash_key, db->cc, msg->hdrs.cc, 2);
  }
  if (msg->hdrs.from) {
    tokenise_string(file_index, db->hash_key, db->from, msg->hdrs.from, 1);
    tokenise_string(file_index, db->hash_key, db->from, msg->hdrs.from, 2);
  }
  if (msg->hdrs.subject) tokenise_string(file_index, db->hash_key, db->subject, msg->hdrs.subject, 1);

  for (a=msg->atts.next; a!=&msg->atts; a=a->next) {
    switch (a->ct) {
      case CT_TEXT_PLAIN:
        tokenise_string(file_index, db->hash_key, db->body, a->data.normal.bytes, 1);
        break;
      case CT_TEXT_HTML:
        tokenise_html_string(file_index, db->hash_key, db->body, a->data.normal.bytes);
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

    if (a->filename) {
      add_token_in_file(file_index, db->hash_key, a->filename, db->attachment_name);
    }

  }

  /* Deal with threading information */
  add_angled_terms(file_index, db->hash_key, db->msg_ids, 1, msg->hdrs.message_id);
  add_angled_terms(file_index, db->hash_key, db->msg_ids, 0, msg->hdrs.in_reply_to);
  add_angled_terms(file_index, db->hash_key, db->msg_ids, 0, msg->hdrs.references);
}
/*}}}*/

static void scan_maildir_flags(struct msgpath *m)/*{{{*/
{
  const char *p, *start;
  start = m->src.mpf.path;
  m->seen = 0;
  m->replied = 0;
  m->flagged = 0;
  for (p=start; *p; p++) {}
  for (p--; (p >= start) && ((*p) != ':'); p--) {}
  if (p >= start) {
    if (!strncmp(p, ":2,", 3)) {
      p += 3;
      while (*p) {
        switch (*p) {
          case 'F': m->flagged = 1; break;
          case 'R': m->replied = 1; break;
          case 'S': m->seen = 1; break;
          default: break;
        }
        p++;
      }
    }
  }
}
/*}}}*/
static void scan_new_messages(struct database *db, int start_at)/*{{{*/
{
  int i;
  for (i=start_at; i<db->n_msgs; i++) {
    struct rfc822 *msg = NULL;
    int len = strlen(db->msgs[i].src.mpf.path);

    if (len > 10 && !strcmp(db->msgs[i].src.mpf.path + len - 11, "/.gitignore"))
      continue;

    switch (db->type[i]) {
      case MTY_DEAD:
        assert(0);
        break;
      case MTY_MBOX:
        assert(0); /* Should never get here - mbox messages are scanned elsewhere. */
        break;
      case MTY_FILE:
        if (verbose) fprintf(stderr, "Scanning <%s>\n", db->msgs[i].src.mpf.path);
        msg = make_rfc822(db->msgs[i].src.mpf.path);
        break;
    }
    if(msg)
    {
      db->msgs[i].date = msg->hdrs.date;
      scan_maildir_flags(&db->msgs[i]);
      tokenise_message(i, db, msg);
      free_rfc822(msg);
    }
    else
      fprintf(stderr, "Skipping %s (could not parse message)\n", db->msgs[i].src.mpf.path);
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

  np = db->n_msgs;
  nm = db->msg_ids->n;
  sm = db->msg_ids->size;

  ix = new_array(int, np);
  for (i=0; i<np; i++) {
    ix[i] = i; /* default - every message in a thread of its own */
  }

  for (m=0; m<sm; m++) {
    struct token2 *tok = db->msg_ids->tokens[m];
    if (tok) {
      unsigned char *j = tok->match0.msginfo;
      unsigned char *last_char = j + tok->match0.n;
      int cur = 0, incr, first=1;
      int new_base=-1, old_base;
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
            assert(new_base != -1);
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
      db->msgs[i].tid = next_tid++;
    } else {
      db->msgs[i].tid = db->msgs[ix[i]].tid;
    }
  }

  free(ix);
  return;
}
/*}}}*/
static int lookup_msgpath(struct msgpath *sorted_paths, int n_msgs, char *key)/*{{{*/
{
  /* Implement bisection search */
 int l, h, m, r;
 l = 0, h = n_msgs;
 m = -1;
 while (h > l) {
   m = (h + l) >> 1;
   /* Should only get called on 'file' type messages - TBC */
   r = strcmp(sorted_paths[m].src.mpf.path, key);
   if (r == 0) break;
   if (l == m) return -1;
   if (r > 0) h = m;
   else       l = m;
 }
 return m;
}
/*}}}*/
void maybe_grow_message_arrays(struct database *db)/*{{{*/
{
  if (db->n_msgs == db->max_msgs) {
    if (db->max_msgs <= 128) {
      db->max_msgs = 256;
    } else {
      db->max_msgs += (db->max_msgs >> 1);
    }
    db->msgs  = grow_array(struct msgpath,    db->max_msgs, db->msgs);
    db->type  = grow_array(enum message_type, db->max_msgs, db->type);
  }
}
/*}}}*/
static void add_msg_path(struct database *db, char *path, time_t mtime, size_t message_size)/*{{{*/
{
  maybe_grow_message_arrays(db);
  db->type[db->n_msgs] = MTY_FILE;
  db->msgs[db->n_msgs].src.mpf.path = new_string(path);
  db->msgs[db->n_msgs].src.mpf.mtime = mtime;
  db->msgs[db->n_msgs].src.mpf.size = message_size;
  ++db->n_msgs;
}
/*}}}*/

static int do_stat(struct msgpath *mp)/*{{{*/
{
  struct stat sb;
  int status;
  status = stat(mp->src.mpf.path, &sb);
  if ((status < 0) ||
      !S_ISREG(sb.st_mode)) {
    return 0;
  } else {
    mp->src.mpf.mtime = sb.st_mtime;
    mp->src.mpf.size = sb.st_size;
    return 1;
  }
}
/*}}}*/
int update_database(struct database *db, struct msgpath *sorted_paths, int n_msgs, int do_fast_index)/*{{{*/
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
  int any_new, n_newly_pruned, n_already_dead;
  int status;

  file_in_db = new_array(char, n_msgs);
  file_in_new_list = new_array(char, db->n_msgs);
  bzero(file_in_db, n_msgs);
  bzero(file_in_new_list, db->n_msgs);

  n_already_dead = 0;
  n_newly_pruned = 0;

  for (i=0; i<db->n_msgs; i++) {
    switch (db->type[i]) {
      case MTY_FILE:
        matched_index = lookup_msgpath(sorted_paths, n_msgs, db->msgs[i].src.mpf.path);
        if (matched_index >= 0) {
          if (do_fast_index) {
            /* Assume the presence of a matching path is good enough without
             * even bothering to stat the file that's there now. */
            file_in_db[matched_index] = 1;
            file_in_new_list[i] = 1;
          } else {
            status = do_stat(sorted_paths + matched_index);
            if (status) {
              if (sorted_paths[matched_index].src.mpf.mtime == db->msgs[i].src.mpf.mtime) {
                /* Treat stale files as though the path has changed. */
                file_in_db[matched_index] = 1;
                file_in_new_list[i] = 1;
              }
            } else {
              /* This path will get treated as dead, and be re-stated below.
               * When that stat fails, the path won't get added to the db. */
            }
          }
        }
        break;
      case MTY_MBOX:
        /* Nothing to do on this pass. */
        break;
      case MTY_DEAD:
        break;
    }
  }

  /* Add new entries to database */
  new_entries_start_at = db->n_msgs;

  for (i=0; i<db->n_msgs; i++) {
    /* Weed dead entries */
    switch (db->type[i]) {
      case MTY_FILE:
        if (!file_in_new_list[i]) {
          free(db->msgs[i].src.mpf.path);
          db->msgs[i].src.mpf.path = NULL;
          db->type[i] = MTY_DEAD;
          ++n_newly_pruned;
        }
        break;
      case MTY_MBOX:
        {
          int msg_index, file_index, number_valid;
          int mbox_valid;
          msg_index = db->msgs[i].src.mbox.msg_index;
          file_index = db->msgs[i].src.mbox.file_index;
          assert (file_index < db->n_mboxen);
          mbox_valid = (db->mboxen[file_index].path) ? 1 : 0;
          number_valid = db->mboxen[file_index].n_old_msgs_valid;
          if (!mbox_valid || (msg_index >= number_valid)) {
            db->type[i] = MTY_DEAD;
            ++n_newly_pruned;
          }
        }
        break;
      case MTY_DEAD:
        /* already dead */
        ++n_already_dead;
        break;
    }
  }

  if (verbose) {
    fprintf(stderr, "%d newly dead messages, %d messages now dead in total\n", n_newly_pruned, n_newly_pruned+n_already_dead);
  }

  any_new = 0;
  for (i=0; i<n_msgs; i++) {
    if (!file_in_db[i]) {
      int status;
      any_new = 1;
      /* The 'sorted_paths' array is only used for file-per-message folders. */
      status = do_stat(sorted_paths + i);
      if (status) {
        /* We only add files that could be successfully stat()'d as regular
         * files. */
        add_msg_path(db, sorted_paths[i].src.mpf.path, sorted_paths[i].src.mpf.mtime, sorted_paths[i].src.mpf.size);
      } else {
        fprintf(stderr, "Cannot add '%s' to database; stat() failed\n", sorted_paths[i].src.mpf.path);
      }
    }
  }

  if (any_new) {
    scan_new_messages(db, new_entries_start_at);
  }

  /* Add newly found mbox messages. */
  any_new |= add_mbox_messages(db);

  if (any_new) {
    find_threading(db);
  } else {
    if (verbose) fprintf(stderr, "No new messages found\n");
  }

  free(file_in_db);
  free(file_in_new_list);

  return any_new || (n_newly_pruned > 0);
}
/*}}}*/
static void recode_encoding(struct matches *m, int *new_idx)/*{{{*/
{
  unsigned char *new_enc, *old_enc;
  unsigned char *j, *last_char;
  int incr, idx, n_idx;

  old_enc = m->msginfo;
  j = old_enc;
  last_char = old_enc + m->n;

  new_enc = new_array(unsigned char, m->max); /* Probably not bigger than this. */
  m->n = 0;
  m->highest = 0;
  m->msginfo = new_enc;
  idx = 0;

  while (j < last_char) {
    incr = read_increment(&j);
    idx += incr;
    n_idx = new_idx[idx];
    if (n_idx >= 0) {
      check_and_enlarge_encoding(m);
      insert_index_on_encoding(m, n_idx);
    }
  }
  free(old_enc);
}
/*}}}*/
static void recode_toktable(struct toktable *tbl, int *new_idx)/*{{{*/
{
  /* Re-encode the vectors according to the new path indices */
  int i;
  int any_dead = 0;
  int any_moved, pass;

  for (i=0; i<tbl->size; i++) {
    struct token *tok = tbl->tokens[i];
    if (tok) {
      recode_encoding(&tok->match0, new_idx);
      if (tok->match0.n == 0) {
        /* Delete this token.  Gotcha - there may be tokens further on in the
         * array that didn't get their natural hash bucket due to collisions.
         * Need to shuffle such tokens up to guarantee that the buckets between
         * the natural one and the one where they are now are all occupied, to
         * prevent their lookups failing. */

#if 0
        fprintf(stderr, "Token <%s> (bucket %d) no longer has files containing it, deleting\n", tok->text, i);
#endif
        free_token(tok);
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
static void recode_toktable2(struct toktable2 *tbl, int *new_idx)/*{{{*/
{
  /* Re-encode the vectors according to the new path indices */
  int i;
  int any_dead = 0;
  int any_moved, pass;

  for (i=0; i<tbl->size; i++) {
    struct token2 *tok = tbl->tokens[i];
    if (tok) {
      recode_encoding(&tok->match0, new_idx);
      recode_encoding(&tok->match1, new_idx);
      if ((tok->match0.n == 0) && (tok->match1.n == 0)) {
        /* Delete this token.  Gotcha - there may be tokens further on in the
         * array that didn't get their natural hash bucket due to collisions.
         * Need to shuffle such tokens up to guarantee that the buckets between
         * the natural one and the one where they are now are all occupied, to
         * prevent their lookups failing. */

#if 0
        fprintf(stderr, "Token <%s> (bucket %d) no longer has files containing it, deleting\n", tok->text, i);
#endif
        free_token2(tok);
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
int cull_dead_messages(struct database *db, int do_integrity_checks)/*{{{*/
{
  /* Return true if any culled */

  int *new_idx, i, j, n_old;
  int any_culled = 0;

  /* Check db is OK before we start on this. (Check afterwards is done in the
   * writer.c code.) */
  if (do_integrity_checks) {
    check_database_integrity(db);
  }

  if (verbose) {
    fprintf(stderr, "Culling dead messages\n");
  }

  n_old = db->n_msgs;

  new_idx = new_array(int, n_old);
  for (i=0, j=0; i<n_old; i++) {
    switch (db->type[i]) {
      case MTY_FILE:
      case MTY_MBOX:
        new_idx[i] = j++;
        break;
      case MTY_DEAD:
        new_idx[i] = -1;
        any_culled = 1;
        break;
    }
  }

  recode_toktable(db->to, new_idx);
  recode_toktable(db->cc, new_idx);
  recode_toktable(db->from, new_idx);
  recode_toktable(db->subject, new_idx);
  recode_toktable(db->body, new_idx);
  recode_toktable(db->attachment_name, new_idx);
  recode_toktable2(db->msg_ids, new_idx);

  /* And crunch down the filename table */
  for (i=0, j=0; i<n_old; i++) {
    switch (db->type[i]) {
      case MTY_DEAD:
        break;
      case MTY_FILE:
      case MTY_MBOX:
        if (i > j) {
          db->msgs[j] = db->msgs[i];
          db->type[j]  = db->type[i];
        }
        j++;
        break;
    }
  }
  db->n_msgs = j;

  free(new_idx);

  /* .. and cull dead mboxen */
  cull_dead_mboxen(db);

  return any_culled;
}
/*}}}*/
