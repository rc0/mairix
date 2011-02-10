/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2004, 2005
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

/* Database dumper */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

#include "mairix.h"
#include "reader.h"
#include "memmac.h"

static void dump_token_chain(struct read_db *db, unsigned int n, unsigned int *tok_offsets, unsigned int *enc_offsets)
{
  int i, j, incr;
  int on_line;
  unsigned char *foo;
  printf("%d entries\n", n);
  for (i=0; i<n; i++) {
    printf("Word %d : <%s>\n", i, db->data + tok_offsets[i]);
    foo = (unsigned char *) db->data + enc_offsets[i];
    j = 0;
    on_line = 0;
    printf("  ");
    while (*foo != 0xff) {
      if (on_line > 15) {
        printf("\n");
        on_line = 0;
      }
      incr = read_increment(&foo);
      j += incr;
      printf("%d ", j);
      on_line++;
    }
    printf("\n");
  }
}

static void dump_toktable(struct read_db *db, struct toktable_db *tbl, const char *title)
{
  printf("Contents of <%s> table\n", title);
  dump_token_chain( db, tbl->n, tbl->tok_offsets, tbl->enc_offsets);
}

static void dump_toktable2(struct read_db *db, struct toktable2_db *tbl, const char *title)
{
  unsigned int n;
  n = tbl->n;
  printf("Contents of <%s> table\n", title);
  printf("Chain 0\n");
  dump_token_chain( db, n, tbl->tok_offsets, tbl->enc0_offsets);
  printf("Chain 1\n");
  dump_token_chain( db, n, tbl->tok_offsets, tbl->enc1_offsets);
}

void dump_database(char *filename)
{
  struct read_db *db;
  int i;

  db = open_db(filename);

  printf("Dump of %s\n", filename);
  printf("%d messages\n", db->n_msgs);
  for (i=0; i<db->n_msgs; i++) {
    printf("%6d: ", i);
    switch (rd_msg_type(db, i)) {
      case DB_MSG_DEAD:
        printf("DEAD");
        break;
      case DB_MSG_FILE:
        printf("FILE %s, size=%d, tid=%d",
               db->data + db->path_offsets[i], db->size_table[i], db->tid_table[i]);
        break;
      case DB_MSG_MBOX:
        {
          unsigned int mbix, msgix;
          decode_mbox_indices(db->path_offsets[i], &mbix, &msgix);

          printf("MBOX %d, msg %d, offset=%d, size=%d, tid=%d",
                 mbix, msgix, db->mtime_table[i], db->size_table[i], db->tid_table[i]);
        }
        break;
    }
    if (db->msg_type_and_flags[i] & FLAG_SEEN) printf(" seen");
    if (db->msg_type_and_flags[i] & FLAG_REPLIED) printf(" replied");
    if (db->msg_type_and_flags[i] & FLAG_FLAGGED) printf(" flagged");
    printf("\n");
  }
  printf("\n");
  if (db->n_mboxen > 0) {
    printf("\nMBOX INFORMATION\n");
    printf("%d mboxen\n", db->n_mboxen);
    for (i=0; i<db->n_mboxen; i++) {
      if (db->mbox_paths_table[i]) {
        printf("%4d: %d msgs in %s\n", i, db->mbox_entries_table[i], db->data + db->mbox_paths_table[i]);
      } else {
        printf("%4d: dead\n", i);
      }
    }
    printf("\n");
  }

  printf("Hash key %08x\n\n", db->hash_key);
  printf("--------------------------------\n");
  dump_toktable(db, &db->to, "To");
  printf("--------------------------------\n");
  dump_toktable(db, &db->cc, "Cc");
  printf("--------------------------------\n");
  dump_toktable(db, &db->from, "From");
  printf("--------------------------------\n");
  dump_toktable(db, &db->subject, "Subject");
  printf("--------------------------------\n");
  dump_toktable(db, &db->body, "Body");
  printf("--------------------------------\n");
  dump_toktable(db, &db->attachment_name, "Attachment names");
  printf("--------------------------------\n");
  dump_toktable2(db, &db->msg_ids, "Message Ids");
  printf("--------------------------------\n");

  close_db(db);
  return;
}

