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


#ifndef MAIRIX_H
#define MAIRIX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "memmac.h"

enum message_type {/*{{{*/
  MTY_DEAD,     /* msg no longer exists, i.e. don't report in searches,
                   prune it on a '-p' run. */
  MTY_FILE,     /* msg <-> file in 1-1 correspondence e.g. maildir, MH */
  MTY_MBOX,     /* multiple msgs per file : MBOX format file */
  MTY_IMAP	/* Message on IMAP server; syntax: uidvalidity:uid:folder */
};
/*}}}*/

enum folder_type {/*{{{*/
  FT_MAILDIR,
  FT_MH,
  FT_MBOX,
  FT_RAW,
  FT_EXCERPT,
  FT_IMAP
};
/*}}}*/

struct msgpath {/*{{{*/
  enum message_type type; /* selector for union 'src' */
  union {
    struct {
      char *path;
      size_t size;  /* size of the message in bytes */
      time_t mtime; /* mtime of message file on disc */
    } mpf; /* message per file */
    struct {
      int file_index; /* index into table of mbox files */
      int msg_index;  /* index of message within the file */
    } mbox; /* for messages in mbox format folders */
  } src;

  /* Now fields that are common to both types of message. */
  time_t date;  /* representation of Date: header in message */
  int tid;      /* thread-id */

  /* Track the folder type this came from, so we know the difference
     between MH and Maildir, both of which have type MTY_FILE. */
  enum folder_type source_ft;

  /* Message flags. */
  unsigned int seen:1;
  unsigned int replied:1;
  unsigned int flagged:1;
    
  /* + other stuff eventually */
};
/*}}}*/

struct msgpath_array {/*{{{*/
  struct msgpath *paths;
  int n;
  int max;
};
/*}}}*/

struct matches {/*{{{*/
  unsigned char *msginfo;
  int n; /* bytes in use */
  int max; /* bytes allocated */
  unsigned long highest;
};
/*}}}*/
struct token {/*{{{*/
  char *text;
  unsigned long hashval;
  /* to store delta-compressed info of which msgpaths match the token */
  struct matches match0;
};
/*}}}*/
struct token2 {/*{{{*/
  char *text;
  unsigned long hashval;
  /* to store delta-compressed info of which msgpaths match the token */
  struct matches match0;
  struct matches match1;
};
/*}}}*/
struct toktable {/*{{{*/
  struct token **tokens;
  int n; /* # in use */
  int size; /* # allocated */
  unsigned int mask; /* for masking down hash values */
  int hwm; /* number to have before expanding */
};
/*}}}*/
struct toktable2 {/*{{{*/
  struct token2 **tokens;
  int n; /* # in use */
  int size; /* # allocated */
  unsigned int mask; /* for masking down hash values */
  int hwm; /* number to have before expanding */
};
/*}}}*/

enum content_type {/*{{{*/
  CT_TEXT_PLAIN,
  CT_TEXT_HTML,
  CT_TEXT_OTHER,
  CT_MESSAGE_RFC822,
  CT_OTHER
};
/*}}}*/
struct rfc822;
struct attachment {/*{{{*/
  struct attachment *next;
  struct attachment *prev;
  enum content_type ct;
  char *filename;
  union attachment_body {
    struct normal_attachment_body {
      int len;
      char *bytes;
    } normal;
    struct rfc822 *rfc822;
  } data;
};
/*}}}*/
struct headers {/*{{{*/
  char *to;
  char *cc;
  char *from;
  char *subject;

  /* The following are needed to support threading */
  char *message_id;
  char *in_reply_to;
  char *references;

  struct {
    unsigned int seen:1;
    unsigned int replied:1;
    unsigned int flagged:1;
  } flags;

  time_t date;
};
/*}}}*/
struct rfc822 {/*{{{*/
  struct headers hdrs;
  struct attachment atts;
};
/*}}}*/

typedef char checksum_t[16];

struct message_list {/*{{{*/
  struct message_list *next;
  off_t start;
  size_t len;
};
/*}}}*/
struct mbox {/*{{{*/
  /* If path==NULL, this indicates that the mbox is dead, i.e. no longer
   * exists. */
  char *path;
  /* As read in from database (i.e. current last time mairix scan was run.) */
  time_t file_mtime;
  size_t file_size;
  /* As found in the filesystem now. */
  time_t current_mtime;
  size_t current_size;
  /* After reconciling a loaded database with what's on the disc, this entry
     stores how many of the msgs that used to be there last time are still
     present at the head of the file.  Thus, all messages beyond that are
     treated as dead, and scanning starts at that point to find 'new' messages
     (whch may actually be old ones that have moved, but they're treated as
     new.) */
  int n_old_msgs_valid;

  /* Hold list of new messages and their number.  Number is temporary -
   * eventually just list walking in case >=2 have to be reattached. */
  struct message_list *new_msgs;
  int n_new_msgs;

  int n_so_far; /* Used during database load. */

  int n_msgs;   /* Number of entries in 'start' and 'len' */
  int max_msgs; /* Allocated size of 'start' and 'len' */
  /* File offset to the start of each message (first line of real header, not to mbox 'From ' line) */
  off_t *start;
  /* Length of each message */
  size_t *len;
  /* Checksums on whole messages. */
  checksum_t *check_all;

};
/*}}}*/
struct database {/*{{{*/
  /* Used to hold an entire mapping between an array of filenames, each
     containing a single message, and the sets of tokens that occur in various
     parts of those messages */

  enum message_type *type;
  struct msgpath *msgs; /* Paths to messages */
  int n_msgs; /* Number in use */
  int max_msgs; /* Space allocated */

  struct mbox *mboxen;
  int n_mboxen; /* number in use. */
  int max_mboxen; /* space allocated */

  /* Seed for hashing in the token tables.  Randomly created for
   * each new database - avoid DoS attacks through carefully
   * crafted messages. */
  unsigned int hash_key;

  /* Token tables */
  struct toktable *to;
  struct toktable *cc;
  struct toktable *from;
  struct toktable *subject;
  struct toktable *body;
  struct toktable *attachment_name;

  /* Encoding chain 0 stores all msgids appearing in the following message headers:
   * Message-Id, In-Reply-To, References.  Used for thread reconciliation.
   * Encoding chain 1 stores just the Message-Id.  Used for search by message ID.
  */
  struct toktable2 *msg_ids;
};
/*}}}*/

struct string_list {/*{{{*/
  struct string_list *next;
  struct string_list *prev;
  char *data;
};
/*}}}*/

struct msg_src {
  enum {MS_FILE, MS_MBOX} type;
  char *filename;
  off_t start;
  size_t len;
};

/* Outcomes of checking a filename/dirname to see whether to keep on looking
 * at filenames within this dir. */
enum traverse_check {
  TRAV_PROCESS, /* Continue looking at this entry */
  TRAV_IGNORE,  /* Ignore just this dir entry */
  TRAV_FINISH   /* Ignore this dir entry and don't bother looking at the rest of the directory */
};

struct traverse_methods {
  int (*filter)(const char *, const struct stat *);
  enum traverse_check (*scrutinize)(int, const char *);
};

extern struct traverse_methods maildir_traverse_methods;
extern struct traverse_methods mh_traverse_methods;
extern struct traverse_methods mbox_traverse_methods;

extern int verbose; /* cmd line -v switch */
extern int do_hardlinks; /* cmd line -H switch */

/* Lame fix for systems where NAME_MAX isn't defined after including the above
 * set of .h files (Solaris, FreeBSD so far).  Probably grossly oversized but
 * it'll do. */

#if !defined(NAME_MAX)
#define NAME_MAX 4096
#endif

/* In glob.c */
struct globber;
struct globber_array;

struct globber *make_globber(const char *wildstring);
void free_globber(struct globber *old);
int is_glob_match(struct globber *g, const char *s);
struct globber_array *colon_sep_string_to_globber_array(const char *in);
int is_globber_array_match(struct globber_array *ga, const char *s);
void free_globber_array(struct globber_array *in);

/* In hash.c */
unsigned int hashfn( unsigned char *k, unsigned int length, unsigned int initval);

/* In dirscan.c */
struct msgpath_array *new_msgpath_array(void);
int valid_mh_filename_p(const char *x);
void free_msgpath_array(struct msgpath_array *x);
void string_list_to_array(struct string_list *list, int *n, char ***arr);
void split_on_colons(const char *str, int *n, char ***arr);
void build_message_list(char *folder_base, char *folders, enum folder_type ft,
    struct msgpath_array *msgs, struct globber_array *omit_globs);

/* In rfc822.c */
struct rfc822 *make_rfc822(char *filename);
void free_rfc822(struct rfc822 *msg);
enum data_to_rfc822_error {
  DTR8_OK,
  DTR8_MISSING_END, /* missing endpoint marker. */
  DTR8_MULTIPART_SANS_BOUNDARY, /* multipart with no boundary string defined */
  DTR8_BAD_HEADERS, /* corrupt headers */
  DTR8_BAD_ATTACHMENT /* corrupt attachment (e.g. no body part) */
};
struct rfc822 *data_to_rfc822(struct msg_src *src, char *data, size_t length, enum data_to_rfc822_error *error);
void create_ro_mapping(const char *filename, unsigned char **data, size_t *len);
void free_ro_mapping(unsigned char *data, size_t len);
char *format_msg_src(struct msg_src *src);

/* In tok.c */
struct toktable *new_toktable(void);
struct toktable2 *new_toktable2(void);
void free_token(struct token *x);
void free_token2(struct token2 *x);
void free_toktable(struct toktable *x);
void free_toktable2(struct toktable2 *x);
void add_token_in_file(int file_index, unsigned int hash_key, char *tok_text, struct toktable *table);
void check_and_enlarge_encoding(struct matches *m);
void insert_index_on_encoding(struct matches *m, int idx);
void add_token2_in_file(int file_index, unsigned int hash_key, char *tok_text, struct toktable2 *table, int add_to_chain1);

/* In db.c */
#define CREATE_RANDOM_DATABASE_HASH 0
struct database *new_database(unsigned int hash_key);
struct database *new_database_from_file(char *db_filename, int do_integrity_checks);
void free_database(struct database *db);
void maybe_grow_message_arrays(struct database *db);
void tokenise_message(int file_index, struct database *db, struct rfc822 *msg);
struct imap_ll;
int update_database(struct database *db, struct msgpath *sorted_paths, int n_paths, int do_fast_index, struct imap_ll *);
void check_database_integrity(struct database *db);
int cull_dead_messages(struct database *db, int do_integrity_checks);

/* In mbox.c */
void build_mbox_lists(struct database *db, const char *folder_base,
    const char *mboxen_paths, struct globber_array *omit_globs,
    int do_mbox_symlinks);
int add_mbox_messages(struct database *db);
void compute_checksum(const char *data, size_t len, checksum_t *csum);
void cull_dead_mboxen(struct database *db);
unsigned int encode_mbox_indices(unsigned int mb, unsigned int msg);
void decode_mbox_indices(unsigned int index, unsigned int *mb, unsigned int *msg);
int verify_mbox_size_constraints(struct database *db);
void glob_and_expand_paths(const char *folder_base, char **paths_in, int n_in, char ***paths_out, int *n_out, const struct traverse_methods *methods, struct globber_array *omit_globs);

/* In glob.c */
struct globber;

struct globber *make_globber(const char *wildstring);
void free_globber(struct globber *old);
int is_glob_match(struct globber *g, const char *s);

/* In writer.c */
void write_database(struct database *db, char *filename, int do_integrity_checks);

/* In search.c */
int search_top(int do_threads, int do_augment, char *database_path, char *complete_mfolder, char **argv, enum folder_type ft, int verbose, const char *imap_pipe, const char *imap_server, const char *imap_username, const char *imap_password);

/* In stats.c */
void get_db_stats(struct database *db);

/* In dates.c */
int scan_date_string(char *in, time_t *start, int *has_start, time_t *end, int *has_end);

/* In dumper.c */
void dump_database(char *filename);

/* In strexpand.c */
char *expand_string(const char *p);

/* In dotlock.c */
void lock_database(char *path, int forced_unlock);
void unlock_database(void);
void unlock_and_exit(int code);

/* In mairix.c */
void report_error(const char *str, const char *filename);

#endif /* MAIRIX_H */
