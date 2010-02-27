/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002,2003,2004,2005
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

/* Database reader */

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

#include "reader.h"
#include "memmac.h"
#include "mairix.h"

int read_increment(unsigned char **encpos) {/*{{{*/
  unsigned char *j = *encpos;
  int result;
  unsigned char x0, x1, x2, x3;

  x0 = *j++;
  if ((x0 & 0xc0) == 0xc0) {
    /* 4 byte encoding */
    x1 = *j++;
    x2 = *j++;
    x3 = *j++;
    result = ((x0 & 0x3f) << 24) + (x1 << 16) + (x2 << 8) + x3;
  } else if (x0 & 0x80) {
    /* 2 byte encoding */
    x1 = *j++;
    result = ((x0 & 0x7f) << 8) + x1;
  } else {
    /* Single byte encoding */
    result = x0;
  }

  *encpos = j;
  return result;
}
/*}}}*/
static void read_toktable_db(char *data, struct toktable_db *toktable, int start, unsigned int *uidata)/*{{{*/
{
  int n;
  n = toktable->n = uidata[start];
  toktable->tok_offsets = uidata + uidata[start+1];
  toktable->enc_offsets = uidata + uidata[start+2];
  return;
}
/*}}}*/
static void read_toktable2_db(char *data, struct toktable2_db *toktable, int start, unsigned int *uidata)/*{{{*/
{
  int n;
  n = toktable->n = uidata[start];
  toktable->tok_offsets = uidata + uidata[start+1];
  toktable->enc0_offsets = uidata + uidata[start+2];
  toktable->enc1_offsets = uidata + uidata[start+3];
  return;
}
/*}}}*/
struct read_db *open_db(char *filename)/*{{{*/
{
  int fd, len;
  char *data;
  struct stat sb;
  struct read_db *result;
  unsigned int *uidata;
  unsigned char *ucdata;

  fd = open(filename, O_RDONLY);
  if (fd < 0) {
    report_error("open", filename);
    unlock_and_exit (2);
  }

  if (fstat(fd, &sb) < 0) {
    report_error("stat", filename);
    unlock_and_exit(2);
  }

  len = sb.st_size;

  data = (char *) mmap(0, len, PROT_READ, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    report_error("reader:mmap", filename);
    unlock_and_exit(2);
  }

  if (!data) {
    /* Empty file opened => database corrupt for sure */
    if (close(fd) < 0) {
      report_error("close", filename);
      unlock_and_exit(2);
    }
    return NULL;
  }

  if (close(fd) < 0) {
    report_error("close", filename);
    unlock_and_exit(2);
  }

  result = new(struct read_db);
  uidata = (unsigned int *) data; /* alignment is assured */
  ucdata = (unsigned char *) data;
  result->len = len;
  result->data = data;

  /*{{{ Magic number check */
  if (ucdata[0] == HEADER_MAGIC0 ||
      ucdata[1] == HEADER_MAGIC1 ||
      ucdata[2] == HEADER_MAGIC2) {
    if (ucdata[3] != HEADER_MAGIC3) {
      fprintf(stderr, "Another version of this program produced the existing database!  Please rebuild.\n");
      unlock_and_exit(2);
    }
  } else {
    fprintf(stderr, "The existing database wasn't produced by this program!  Please rebuild.\n");
    unlock_and_exit(2);
  }
  /*}}}*/
  /* {{{ Endianness check */
  if (uidata[UI_ENDIAN] == 0x11223344) {
    fprintf(stderr, "The endianness of the database is reversed for this machine\n");
    unlock_and_exit(2);
  } else if (uidata[UI_ENDIAN] != 0x44332211) {
    fprintf(stderr, "The endianness of this machine is strange (or database is corrupt)\n");
    unlock_and_exit(2);
  }
  /* }}} */

  /* Now build tables of where things are in the file */
  result->n_msgs = uidata[UI_N_MSGS];
  result->msg_type_and_flags = ucdata + uidata[UI_MSG_TYPE_AND_FLAGS];
  result->path_offsets = uidata + uidata[UI_MSG_CDATA];
  result->mtime_table = uidata + uidata[UI_MSG_MTIME];
  result->size_table = uidata + uidata[UI_MSG_SIZE];
  result->date_table = uidata + uidata[UI_MSG_DATE];
  result->tid_table  = uidata + uidata[UI_MSG_TID];

  result->n_mboxen            = uidata[UI_MBOX_N];
  result->mbox_paths_table    = uidata + uidata[UI_MBOX_PATHS];
  result->mbox_entries_table  = uidata + uidata[UI_MBOX_ENTRIES];
  result->mbox_mtime_table    = uidata + uidata[UI_MBOX_MTIME];
  result->mbox_size_table     = uidata + uidata[UI_MBOX_SIZE];
  result->mbox_checksum_table = uidata + uidata[UI_MBOX_CKSUM];

  result->hash_key = uidata[UI_HASH_KEY];

  read_toktable_db(data, &result->to, UI_TO_BASE, uidata);
  read_toktable_db(data, &result->cc, UI_CC_BASE, uidata);
  read_toktable_db(data, &result->from, UI_FROM_BASE, uidata);
  read_toktable_db(data, &result->subject, UI_SUBJECT_BASE, uidata);
  read_toktable_db(data, &result->body, UI_BODY_BASE, uidata);
  read_toktable_db(data, &result->attachment_name, UI_ATTACHMENT_NAME_BASE, uidata);
  read_toktable2_db(data, &result->msg_ids, UI_MSGID_BASE, uidata);

  return result;
}
/*}}}*/
static void free_toktable_db(struct toktable_db *x)/*{{{*/
{
  /* Nothing to do */
}
/*}}}*/
static void free_toktable2_db(struct toktable2_db *x)/*{{{*/
{
  /* Nothing to do */
}
/*}}}*/
void close_db(struct read_db *x)/*{{{*/
{
  free_toktable_db(&x->to);
  free_toktable_db(&x->cc);
  free_toktable_db(&x->from);
  free_toktable_db(&x->subject);
  free_toktable_db(&x->body);
  free_toktable_db(&x->attachment_name);
  free_toktable2_db(&x->msg_ids);

  if (munmap(x->data, x->len) < 0) {
    perror("munmap");
    unlock_and_exit(2);
  }
  free(x);
  return;
}
/*}}}*/

