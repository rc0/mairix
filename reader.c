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

void read_db_int_list_reader_init(
  struct int_list_reader *ilr,
  const struct read_db *db,
  unsigned int start_offset
) {
  ilr->accumulator = 0;
  ilr->pos = ((unsigned char *)db->data) + start_offset;
  ilr->end = ((unsigned char *)db->data) + db->len;
}

void matches_int_list_reader_init(
  struct int_list_reader *ilr, const struct matches *m
) {
  ilr->accumulator = 0;
  ilr->pos = m->msginfo;
  ilr->end = m->msginfo + m->n;
}

int int_list_reader_read(struct int_list_reader *ilr, int *result) {
  unsigned char x0, x1, x2, x3;
  unsigned int incr;

  if (ilr->pos >= ilr->end) return 0;
  x0 = *ilr->pos;
  if (x0 == 0xff) {
    return 0;
  } else if ((x0 & 0xc0) == 0xc0) {
    /* 4 byte encoding */
    if ((ilr->pos + 4) > ilr->end) return 0;
    x1 = ilr->pos[1];
    x2 = ilr->pos[2];
    x3 = ilr->pos[3];
    incr = ((x0 & 0x3f) << 24) + (x1 << 16) + (x2 << 8) + x3;
    ilr->pos += 4;
  } else if (x0 & 0x80) {
    /* 2 byte encoding */
    if ((ilr->pos + 2) > ilr->end) return 0;
    x1 = ilr->pos[1];
    incr = ((x0 & 0x7f) << 8) + x1;
    ilr->pos += 2;
  } else {
    /* Single byte encoding */
    incr = x0;
    ilr->pos++;
  }
  ilr->accumulator += incr;
  *result = ilr->accumulator;
  return 1;
}

void int_list_reader_copy(struct int_list_reader *ilr, struct matches *out) {
  const unsigned char *base = ilr->pos;
  int size, index;
  while (int_list_reader_read(ilr, &index));
  size = ilr->pos - base;

  out->n = size;
  out->highest = index;

  /* Allow a bit of headroom for adding more entries later */
  size += size / 2;
  if (size < 16) size = 16;

  out->max = size;
  out->msginfo = new_array(unsigned char, size);
  memcpy(out->msginfo, base, out->n);
}

const char *get_db_token(const struct read_db *db, unsigned int token_offset) {
  const char *token = db->data + token_offset;
  const char *db_end = db->data + db->len;
  const char *p = token;
  while (p < db_end) {
    if ((*(p++)) == 0) return token;
  }
  return NULL;
}

static int read_toktable_db(char *data, int len, struct toktable_db *toktable, int start, unsigned int *uidata)/*{{{*/
{
  char *file_end = data + len;
  toktable->n = uidata[start];
  toktable->tok_offsets = uidata + uidata[start+1];
  if (((char *)(&(toktable->tok_offsets[toktable->n]))) > file_end) {
    return 0;
  }
  toktable->enc_offsets = uidata + uidata[start+2];
  if (((char *)(&(toktable->enc_offsets[toktable->n]))) > file_end) {
    return 0;
  }
  return 1;
}
/*}}}*/
static int read_toktable2_db(char *data, int len, struct toktable2_db *toktable, int start, unsigned int *uidata)/*{{{*/
{
  char *file_end = data + len;
  toktable->n = uidata[start];
  toktable->tok_offsets = uidata + uidata[start+1];
  if (((char *)(&(toktable->tok_offsets[toktable->n]))) > file_end) {
    return 0;
  }
  toktable->enc0_offsets = uidata + uidata[start+2];
  if (((char *)(&(toktable->enc0_offsets[toktable->n]))) > file_end) {
    return 0;
  }
  toktable->enc1_offsets = uidata + uidata[start+3];
  if (((char *)(&(toktable->enc1_offsets[toktable->n]))) > file_end) {
    return 0;
  }
  return 1;
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
  size_t uioffset_max;  /* ui offset that lies beyond bounds */

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

  if (len < UC_HEADER_LEN) {
    fprintf(stderr, "Database file %s is too short, possibly corrupted (actual size %d, minimum size %d). Please rebuild.\n", filename, len, UC_HEADER_LEN);
    unlock_and_exit(2);
  }

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
  uioffset_max = len / sizeof(unsigned int);
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

#define GET_TABLE(dest, start_index, table_size) {\
  unsigned int table_offset = uidata[(start_index)];\
  if ((table_offset + (table_size)) > uioffset_max) goto corrupt;\
  (dest) = uidata + table_offset;\
}
#define GET_MSG_TABLE(dest, start_index) GET_TABLE((dest), (start_index), result->n_msgs)
#define GET_MBOX_TABLE(dest, start_index) GET_TABLE((dest), (start_index), result->n_mboxen)

  /* Now build tables of where things are in the file */
  result->n_msgs = uidata[UI_N_MSGS];
  result->msg_type_and_flags = ucdata + uidata[UI_MSG_TYPE_AND_FLAGS];
  if (0 && (
    (result->msg_type_and_flags + result->n_msgs) >
    (ucdata + len)
  )) {
corrupt:
    fprintf(stderr, "Database is corrupt. Please rebuild.\n");
    unlock_and_exit(2);
  }
  GET_MSG_TABLE(result->path_offsets, UI_MSG_CDATA);
  GET_MSG_TABLE(result->mtime_table, UI_MSG_MTIME);
  GET_MSG_TABLE(result->size_table, UI_MSG_SIZE);
  GET_MSG_TABLE(result->date_table, UI_MSG_DATE);
  GET_MSG_TABLE(result->tid_table, UI_MSG_TID);

  result->n_mboxen = uidata[UI_MBOX_N];
  GET_MBOX_TABLE(result->mbox_paths_table, UI_MBOX_PATHS);
  GET_MBOX_TABLE(result->mbox_entries_table, UI_MBOX_ENTRIES);
  GET_MBOX_TABLE(result->mbox_mtime_table, UI_MBOX_MTIME);
  GET_MBOX_TABLE(result->mbox_size_table, UI_MBOX_SIZE);
  GET_MBOX_TABLE(result->mbox_checksum_table, UI_MBOX_CKSUM);

  result->hash_key = uidata[UI_HASH_KEY];

  if (!(
    read_toktable_db(data, len, &result->to, UI_TO_BASE, uidata) &&
    read_toktable_db(data, len, &result->cc, UI_CC_BASE, uidata) &&
    read_toktable_db(data, len, &result->from, UI_FROM_BASE, uidata) &&
    read_toktable_db(data, len, &result->subject, UI_SUBJECT_BASE, uidata) &&
    read_toktable_db(data, len, &result->body, UI_BODY_BASE, uidata) &&
    read_toktable_db(data, len, &result->attachment_name, UI_ATTACHMENT_NAME_BASE, uidata) &&
    read_toktable2_db(data, len, &result->msg_ids, UI_MSGID_BASE, uidata)
  )) {
    goto corrupt;
  }

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
