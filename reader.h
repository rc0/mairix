/*
  $Header: /cvs/src/mairix/reader.h,v 1.1 2002/07/03 22:15:59 richard Exp $

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


#ifndef READER_H
#define READER_H

/* MX, then a high byte, then the version no. */
#define HEADER_MAGIC0 'M'
#define HEADER_MAGIC1 'X'
#define HEADER_MAGIC2 0xA5
#define HEADER_MAGIC3 0x01

struct toktable_db {/*{{{*/
  unsigned int n; /* number of entries in this table */
  unsigned int *tok_offsets; /* offset to table of token offsets */
  unsigned int *enc_offsets; /* offset to table of encoding offsets */
};
/*}}}*/
struct read_db {/*{{{*/
  /* Raw file parameters, needed later for munmap */
  char *data;
  int len;
  
  /* Pathname information */
  int n_paths;
  unsigned int *path_offsets;
  unsigned int *mtime_table;
  unsigned int *date_table;
  unsigned int *size_table;
  unsigned int *tid_table;
  
  struct toktable_db to;
  struct toktable_db cc;
  struct toktable_db from;
  struct toktable_db subject;
  struct toktable_db body;
  struct toktable_db msg_ids;

};
/*}}}*/

struct read_db *open_db(char *filename);
void close_db(struct read_db *x);

/* Common to search and db reader. */
int read_increment(unsigned char **encpos);

#endif /* READER_H */
