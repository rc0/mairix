/*
  $Header: /cvs/src/mairix/writer.c,v 1.2 2002/07/24 22:50:40 richard Exp $

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

/* Write the database to disc. */

#include "mairix.h"
#include "reader.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

#define HEADER_LENGTH 128
#define HEADER_UI_LEN 32

struct write_map_toktable {/*{{{*/
  
  /* Table of character offsets to null-terminated token texts */
  int tok_offset;

  /* Table of character offsets to byte strings containing compressed
   * delta-encoding of file indices matching the token */
  int enc_offset;
};/*}}}*/

struct write_map {/*{{{*/
/* Contain offset information for the various tables.
   UI stuff in 4 byte units rel to base addr.
   Char stuff in byte units rel to base addr. */

  /* Path information */
  int path_offset;
  int mtime_offset; /* Message file mtimes */
  int date_offset; /* Message dates */
  int size_offset; /* Message sizes */
  int tid_offset;  /* Thread group index table */

  struct write_map_toktable to;
  struct write_map_toktable cc;
  struct write_map_toktable from;
  struct write_map_toktable subject;
  struct write_map_toktable body;
  struct write_map_toktable msg_ids;

  /* To get base address for character data */
  int beyond_last_ui_offset;
};
/*}}}*/

static char *create_file_mapping(char *filename, size_t len)/*{{{*/
{
  int fd;
  char *data;
  struct stat sb;
  
  fd = open(filename, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    perror("open");
    exit(1);
  }

  if (fstat(fd, &sb) < 0) {
    perror("stat");
    exit(1);
  }

  if (sb.st_size < len) {
    /* Extend */
    if (lseek(fd, len - 1, SEEK_SET) < 0) {
      perror("lseek");
      exit(1);
    }
    if (write(fd, "\000", 1) < 0) {
      perror("write");
      exit(1);
    }
  } else if (sb.st_size > len) {
    /* Truncate */
    if (ftruncate(fd, len) < 0) {
      perror("ftruncate");
      exit(1);
    }
  } else {
    /* Exactly the right length already - nothing to do! */
  }

  data = mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if ((int)data <= 0) {
    perror("mmap");
    exit(1);
  }

  if (close(fd) < 0) {
    perror("close");
    exit(1);
  }
  
  return data;
}
/*}}}*/
  
static int toktable_char_length(struct toktable *tab)/*{{{*/
{
  int result = 0;
  int i;
  for (i=0; i<tab->size; i++) {
    if (tab->tokens[i]) {
      result += (1 + strlen(tab->tokens[i]->text));
      result += (1 + tab->tokens[i]->n);
    }
  }
  return result;
}
/*}}}*/
static int char_length(struct database *db)/*{{{*/
{
  /* Return total length of character data to be written. */
  int result;
  int i;

  result = 0;

  for (i=0; i<db->n_paths; i++) {
    if (db->paths[i].path) {
      result += (1 + strlen(db->paths[i].path));
    }
  }

  result += toktable_char_length(db->to);
  result += toktable_char_length(db->cc);
  result += toktable_char_length(db->from);
  result += toktable_char_length(db->subject);
  result += toktable_char_length(db->body);
  result += toktable_char_length(db->msg_ids);

  return result;
}
/*}}}*/

static void compute_mapping(struct database *db, struct write_map *map)/*{{{*/
{
  map->path_offset = HEADER_UI_LEN + 0;
  map->mtime_offset  = map->path_offset + db->n_paths;
  map->date_offset   = map->mtime_offset + db->n_paths;
  map->size_offset   = map->date_offset + db->n_paths;
  map->tid_offset    = map->size_offset + db->n_paths;
  
  map->to.tok_offset = map->tid_offset + db->n_paths;
  map->to.enc_offset = map->to.tok_offset  + db->to->n;
  
  map->cc.tok_offset = map->to.enc_offset + db->to->n;
  map->cc.enc_offset = map->cc.tok_offset  + db->cc->n;
  
  map->from.tok_offset = map->cc.enc_offset + db->cc->n;
  map->from.enc_offset = map->from.tok_offset  + db->from->n;
  
  map->subject.tok_offset = map->from.enc_offset + db->from->n;
  map->subject.enc_offset = map->subject.tok_offset  + db->subject->n;
  
  map->body.tok_offset = map->subject.enc_offset + db->subject->n;
  map->body.enc_offset = map->body.tok_offset  + db->body->n;

  map->msg_ids.tok_offset = map->body.enc_offset + db->body->n;
  map->msg_ids.enc_offset = map->msg_ids.tok_offset  + db->msg_ids->n;

  map->beyond_last_ui_offset = map->msg_ids.enc_offset + db->msg_ids->n;
}
/*}}}*/
static void write_header(char *data, unsigned int *uidata, struct database *db, struct write_map *map)/*{{{*/
{
  /* Endianness-independent writes - at least the magic number will be
   * recognized if the database is read by this program on a machine of
   * opposite endianness. */
  unsigned char *ucdata = (unsigned char *) data;

  ucdata[0] = HEADER_MAGIC0;
  ucdata[1] = HEADER_MAGIC1;
  ucdata[2] = HEADER_MAGIC2;
  ucdata[3] = HEADER_MAGIC3;

  uidata[1] = 0x44332211; /* For checking reversed endianness on read */
  uidata[2] = db->n_paths;
  uidata[3] = map->path_offset; /* offset table of ptrs to filenames */
  uidata[4] = map->mtime_offset; /* offset of mtime table */
  uidata[5] = map->date_offset; /* offset of table of message Date: header lines as time_t */
  uidata[6] = map->size_offset; /* offset of table of message sizes in bytes */
  uidata[7] = map->tid_offset; /* offset of table of thread group numbers */

  uidata[8] = db->to->n;
  uidata[9] = map->to.tok_offset;
  uidata[10] = map->to.enc_offset;
  
  uidata[11] = db->cc->n;
  uidata[12] = map->cc.tok_offset;
  uidata[13] = map->cc.enc_offset;

  uidata[14] = db->from->n;
  uidata[15] = map->from.tok_offset;
  uidata[16] = map->from.enc_offset;

  uidata[17] = db->subject->n;
  uidata[18] = map->subject.tok_offset;
  uidata[19] = map->subject.enc_offset;
  
  uidata[20] = db->body->n;
  uidata[21] = map->body.tok_offset;
  uidata[22] = map->body.enc_offset;
  
  uidata[23] = db->msg_ids->n;
  uidata[24] = map->msg_ids.tok_offset;
  uidata[25] = map->msg_ids.enc_offset;
  
  return;
}
/*}}}*/
static char *write_filenames(struct database *db, struct write_map *map, unsigned int *uidata, char *data, char *cdata)/*{{{*/
{
  int i;
  char *start_cdata = cdata;

  for (i=0; i<db->n_paths; i++) {
    int slen;
    if (db->paths[i].path) {
      /* File still alive */
      slen = strlen(db->paths[i].path);
      uidata[map->path_offset + i] = cdata - data;
      uidata[map->mtime_offset + i] = db->paths[i].mtime;
      uidata[map->date_offset + i] = db->paths[i].date;
      uidata[map->size_offset + i] = db->paths[i].size;
      uidata[map->tid_offset + i]  = db->paths[i].tid;
      memcpy(cdata, db->paths[i].path, 1 + slen); /* include trailing null */
      cdata += (1 + slen);
    } else {
      /* File dead */
      uidata[map->path_offset + i] = 0; /* Can't ever happen for real */
      uidata[map->mtime_offset + i] = 0; /* For cleanliness */
      uidata[map->size_offset + i] = 0;  /* For cleanliness */
      /* The following line is necessary, otherwise 'random' tid
       * information is written to the database, which can crash the search
       * functions. */
      uidata[map->tid_offset + i]  = db->paths[i].tid;
    }
  }
  if (verbose) {
    printf("Wrote %d paths (%d bytes tables, %d bytes text)\n",
           db->n_paths, 4*5*db->n_paths, cdata - start_cdata);
  }
  return cdata; /* new value */
}
/*}}}*/
#if 0
static int compare_tokens(const void *a, const void *b)/*{{{*/
{
  const struct token **aa = (const struct token **) a;
  const struct token **bb = (const struct token **) b;
  return strcmp((*aa)->text, (*bb)->text);
}
/*}}}*/
#endif
static char *write_toktable(struct toktable *tab, struct write_map_toktable *map, unsigned int *uidata, char *data, char *cdata, char *header_name)/*{{{*/
{
  int i, j, n, max;
  char *start_cdata, *mid_cdata;
  struct token **stok;
  stok = new_array(struct token *, tab->n);
  max = tab->size;
  n = tab->n;
  
  for (i=0, j=0; i<max; i++) {
    struct token *tok = tab->tokens[i];
    if (tok) {
      stok[j++] = tok;
    }
  }

  assert(j == n);

#if 0
  /* The search functions don't rely on the tokens being sorted.  So not
   * sorting here will save time. */
  qsort(stok, n, sizeof(struct token *), compare_tokens);
#endif

  start_cdata = cdata;

  /* FIXME : Eventually, the tokens have to be sorted - need to feed them from
   * a different data structure (array with no holes) */
  for (i=0; i<n; i++) {
    int slen;
    uidata[map->tok_offset + i] = cdata - data;
    slen = strlen(stok[i]->text);
    memcpy(cdata, stok[i]->text, 1 + slen);
    cdata += (1 + slen);
  }

  mid_cdata = cdata;

  for (i=0; i<n; i++) {
    int dlen;
    dlen = stok[i]->n;
    uidata[map->enc_offset + i] = cdata - data;
    memcpy(cdata, stok[i]->msginfo, dlen);
    cdata += dlen;
    *cdata++ = 0xff; /* termination character */
  }

  if (verbose) {
    printf("%s: wrote %d tokens (%d bytes tables, %d bytes of text, %d bytes of hit encoding)\n",
            header_name, n, 2*4*n, mid_cdata - start_cdata, cdata - mid_cdata);
  }

  free(stok);
  return cdata;
}
/*}}}*/
void write_database(struct database *db, char *filename)/*{{{*/
{
  int file_len;
  char *data, *cdata;
  unsigned int *uidata;
  struct write_map map;

  check_database_integrity(db);
  
  /* Work out mappings */
  compute_mapping(db, &map);
  
  file_len = char_length(db) + (4 * map.beyond_last_ui_offset);
  
  data = create_file_mapping(filename, file_len);
  uidata = (unsigned int *) data; /* align(int) < align(page)! */
  cdata = data + (4 * map.beyond_last_ui_offset);

  write_header(data, uidata, db, &map);
  cdata = write_filenames(db, &map, uidata, data, cdata);
  cdata = write_toktable(db->to, &map.to, uidata, data, cdata, "To");
  cdata = write_toktable(db->cc, &map.cc, uidata, data, cdata, "Cc");
  cdata = write_toktable(db->from, &map.from, uidata, data, cdata, "From");
  cdata = write_toktable(db->subject, &map.subject, uidata, data, cdata, "Subject");
  cdata = write_toktable(db->body, &map.body, uidata, data, cdata, "Body");
  cdata = write_toktable(db->msg_ids, &map.msg_ids, uidata, data, cdata, "(Threading)");
  
  /* Write data */
  /* Unmap / close file */
  if (munmap(data, file_len) < 0) {
    perror("munmap");
    exit(1);
  }
}
  /*}}}*/
