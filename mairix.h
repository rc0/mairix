/*
  $Header: /cvs/src/mairix/mairix.h,v 1.2 2002/07/29 23:03:47 richard Exp $

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


#ifndef MAIRIX_H
#define MAIRIX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memmac.h"

struct msgpath {/*{{{*/
  char *path;
  size_t size;  /* size of the message in bytes */
  time_t mtime; /* mtime of message file on disc */
  time_t date;  /* representation of Date: header in message */
  int tid;      /* thread-id */
  /* + other stuff eventually */
};
/*}}}*/

struct msgpath_array {/*{{{*/
  struct msgpath *paths;
  int n;
  int max;
};
/*}}}*/

struct token {/*{{{*/
  char *text;
  unsigned long hashval;
  
  /* to store delta-compressed info of which msgpaths match the token */
  unsigned char *msginfo;
  int n; /* bytes in use */
  int max; /* bytes allocated */
  unsigned long highest;
  
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

  time_t date;
};
/*}}}*/
struct rfc822 {/*{{{*/
  struct headers hdrs;
  struct attachment atts;
};
/*}}}*/

struct database {/*{{{*/
  /* Used to hold an entire mapping between an array of filenames, each
     containing a single message, and the sets of tokens that occur in various
     parts of those messages */

  struct msgpath *paths; /* Paths to messages */
  int n_paths; /* Number in use */
  int max_paths; /* Space allocated */

  /* Token tables */
  struct toktable *to;
  struct toktable *cc;
  struct toktable *from;
  struct toktable *subject;
  struct toktable *body;

  struct toktable *msg_ids;
};
/*}}}*/

enum folder_type {/*{{{*/
  FT_MAILDIR,
  FT_MH
};
/*}}}*/

extern int verbose; /* cmd line -v switch */

/* In hash.c */
unsigned int hashfn( unsigned char *k, unsigned int length, unsigned int initval);

/* In dirscan.c */
struct msgpath_array *new_msgpath_array(void);
int is_integer_string(char *x);
void free_msgpath_array(struct msgpath_array *x);
void build_message_list(char *folder_base, char *folders, enum folder_type ft, struct msgpath_array *msgs);
  
/* In rfc822.c */
struct rfc822 *make_rfc822(char *filename);
void free_rfc822(struct rfc822 *msg);

/* In tok.c */
struct toktable *new_toktable(void);
void free_toktable(struct toktable *x);
void add_token_in_file(int file_index, char *tok_text, struct toktable *table);
void check_and_enlarge_tok_encoding(struct token *tok);
void insert_index_on_token(struct token *tok, int idx);

/* In db.c */
struct database *new_database(void);
struct database *new_database_from_file(char *db_filename);
void free_database(struct database *db);
int update_database(struct database *db, struct msgpath *sorted_paths, int n_paths);
void check_database_integrity(struct database *db);
int cull_dead_messages(struct database *db);

/* In writer.c */
void write_database(struct database *db, char *filename);

/* In search.c */
void search_top(int do_threads, int do_augment, char *database_path, char *folder_base, char *vfolder, char **argv, enum folder_type ft);
  
/* In stats.c */
void get_db_stats(struct database *db);

#endif /* MAIRIX_H */
