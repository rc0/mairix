/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004,2006
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

#ifndef READER_H
#define READER_H

/* MX, then a high byte, then the version no. */
#define HEADER_MAGIC0 'M'
#define HEADER_MAGIC1 'X'
#define HEADER_MAGIC2 0xA5
#define HEADER_MAGIC3 0x03

/*{{{ Constants for file data positions */
#define UI_ENDIAN          1
#define UI_N_MSGS          2

/* Offset to byte-per-message table encoding message types */
#define UI_MSG_TYPE_AND_FLAGS 3

/* Header positions containing offsets to the per-message tables. */
/* Character data:
 * for maildir/MH : the path of the box.
 * for mbox : index of mbox containing the message */

#define UI_MSG_CDATA       4
/* For maildir/MH : mtime of file containing message */
#define UI_MSG_MTIME       5
/* For mbox msgs : the offset into the file */
#define UI_MSG_OFFSET      5
/* For all formats : message size */
#define UI_MSG_SIZE        6
/* For mbox msgs : offset into file */
#define UI_MSG_START       6
/* These are common to Maildir,MH,mbox messages */
#define UI_MSG_DATE        7
#define UI_MSG_TID         8

/* Header positions for mbox (file-level) information */
/* Number of mboxes */
#define UI_MBOX_N          9
#define UI_MBOX_PATHS     10
#define UI_MBOX_ENTRIES   11
/* mtime of mboxes */
#define UI_MBOX_MTIME     12
/* Size in bytes */
#define UI_MBOX_SIZE      13
/* Base of checksums for messages in each mbox */
#define UI_MBOX_CKSUM     14

#define UI_HASH_KEY       15

/* Header positions for token tables */
#define UI_TO_BASE        16
#define UI_CC_BASE        19
#define UI_FROM_BASE      22
#define UI_SUBJECT_BASE   25
#define UI_BODY_BASE      28
#define UI_ATTACHMENT_NAME_BASE 31
#define UI_MSGID_BASE     34

/* Larger than the last table offset. */
#define UI_HEADER_LEN     40
#define UC_HEADER_LEN     ((UI_HEADER_LEN) << 2)

#define UI_N_OFFSET        0
#define UI_TOK_OFFSET      1
#define UI_ENC_OFFSET      2

#define UI_TO_N           (UI_TO_BASE + UI_N_OFFSET)
#define UI_TO_TOK         (UI_TO_BASE + UI_TOK_OFFSET)
#define UI_TO_ENC         (UI_TO_BASE + UI_ENC_OFFSET)
#define UI_CC_N           (UI_CC_BASE + UI_N_OFFSET)
#define UI_CC_TOK         (UI_CC_BASE + UI_TOK_OFFSET)
#define UI_CC_ENC         (UI_CC_BASE + UI_ENC_OFFSET)
#define UI_FROM_N         (UI_FROM_BASE + UI_N_OFFSET)
#define UI_FROM_TOK       (UI_FROM_BASE + UI_TOK_OFFSET)
#define UI_FROM_ENC       (UI_FROM_BASE + UI_ENC_OFFSET)
#define UI_SUBJECT_N      (UI_SUBJECT_BASE + UI_N_OFFSET)
#define UI_SUBJECT_TOK    (UI_SUBJECT_BASE + UI_TOK_OFFSET)
#define UI_SUBJECT_ENC    (UI_SUBJECT_BASE + UI_ENC_OFFSET)
#define UI_BODY_N         (UI_BODY_BASE + UI_N_OFFSET)
#define UI_BODY_TOK       (UI_BODY_BASE + UI_TOK_OFFSET)
#define UI_BODY_ENC       (UI_BODY_BASE + UI_ENC_OFFSET)
#define UI_ATTACHMENT_NAME_N    (UI_ATTACHMENT_NAME_BASE + UI_N_OFFSET)
#define UI_ATTACHMENT_NAME_TOK  (UI_ATTACHMENT_NAME_BASE + UI_TOK_OFFSET)
#define UI_ATTACHMENT_NAME_ENC  (UI_ATTACHMENT_NAME_BASE + UI_ENC_OFFSET)
#define UI_MSGID_N        (UI_MSGID_BASE + UI_N_OFFSET)
#define UI_MSGID_TOK      (UI_MSGID_BASE + UI_TOK_OFFSET)
#define UI_MSGID_ENC0     (UI_MSGID_BASE + UI_ENC_OFFSET)
#define UI_MSGID_ENC1     (UI_MSGID_ENC0 + 1)

/*}}}*/

/*{{{ Literals used for encoding messages types in database file */
#define DB_MSG_DEAD 0
/* maildir/MH : one file per message */
#define DB_MSG_FILE 1
/* mbox : multiple files per message */
#define DB_MSG_MBOX 2
/*}}}*/

#define FLAG_SEEN    (1<<3)
#define FLAG_REPLIED (1<<4)
#define FLAG_FLAGGED (1<<5)

struct toktable_db {/*{{{*/
  unsigned int n; /* number of entries in this table */
  unsigned int *tok_offsets; /* offset to table of token offsets */
  unsigned int *enc_offsets; /* offset to table of encoding offsets */
};
/*}}}*/
struct toktable2_db {/*{{{*/
  unsigned int n; /* number of entries in this table */
  unsigned int *tok_offsets; /* offset to table of token offsets */
  unsigned int *enc0_offsets; /* offset to table of encoding offsets */
  unsigned int *enc1_offsets; /* offset to table of encoding offsets */
};
/*}}}*/
struct read_db {/*{{{*/
  /* Raw file parameters, needed later for munmap */
  char *data;
  int len;

  /* Pathname information */
  int n_msgs;
  unsigned char *msg_type_and_flags;
  unsigned int *path_offsets; /* or (mbox index, msg index) */
  unsigned int *mtime_table; /* or offset into mbox */
  unsigned int *size_table;  /* either file size or span inside mbox */
  unsigned int *date_table;
  unsigned int *tid_table;

  int n_mboxen;
  unsigned int *mbox_paths_table;
  unsigned int *mbox_entries_table; /* table of number of messages per mbox */
  unsigned int *mbox_mtime_table;
  unsigned int *mbox_size_table;
  unsigned int *mbox_checksum_table;

  unsigned int hash_key;

  struct toktable_db to;
  struct toktable_db cc;
  struct toktable_db from;
  struct toktable_db subject;
  struct toktable_db body;
  struct toktable_db attachment_name;
  struct toktable2_db msg_ids;

};
/*}}}*/

struct read_db *open_db(char *filename);
void close_db(struct read_db *x);

static inline int rd_msg_type(struct read_db *db, int i) {
  return db->msg_type_and_flags[i] & 0x7;
}

/* Common to search and db reader. */
int read_increment(unsigned char **encpos);

#endif /* READER_H */
