/*
  $Header: /cvs/src/mairix/reader.c,v 1.1 2002/07/03 22:15:59 richard Exp $

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

int read_increment(unsigned char **encpos) {/*{{{*/
  char *j = *encpos;
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
    perror("open");
    exit (1);
  }

  if (fstat(fd, &sb) < 0) {
    perror("stat");
    exit(1);
  }

  len = sb.st_size;

  data = (char *) mmap(0, len, PROT_READ, MAP_SHARED, fd, 0);
  if (!data) {
    perror("mmap");
    exit(1);
  }
  
  if (close(fd) < 0) {
    perror("close");
    exit(1);
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
      exit(1);
    }
  } else {
    fprintf(stderr, "The existing database wasn't produced by this program!  Please rebuild.\n");
    exit(1);
  }
  /*}}}*/
  /* {{{ Endianness check */
  if (uidata[1] == 0x11223344) {
    fprintf(stderr, "The endianness of the database is reversed for this machine\n");
    exit(1);
  } else if (uidata[1] != 0x44332211) {
    fprintf(stderr, "The endianness of this machine is strange (or database is corrupt)\n");
    exit(1);
  }
  /* }}} */

  /* Now build tables of where things are in the file */
  result->n_paths = uidata[2];
  result->path_offsets = uidata + uidata[3];
  result->mtime_table = uidata + uidata[4];
  result->date_table = uidata + uidata[5];
  result->size_table = uidata + uidata[6];
  result->tid_table  = uidata + uidata[7];

  read_toktable_db(data, &result->to, 8, uidata);
  read_toktable_db(data, &result->cc, 11, uidata);
  read_toktable_db(data, &result->from, 14, uidata);
  read_toktable_db(data, &result->subject, 17, uidata);
  read_toktable_db(data, &result->body, 20, uidata);
  read_toktable_db(data, &result->msg_ids, 23, uidata);

  return result;
}
/*}}}*/
static void free_toktable_db(struct toktable_db *x)/*{{{*/
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
  free_toktable_db(&x->msg_ids);

  if (munmap(x->data, x->len) < 0) {
    perror("munmap");
    exit(1);
  }
  free(x);
  return;
}
/*}}}*/

