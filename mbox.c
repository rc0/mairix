/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2003-2004
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

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "mairix.h"
#include "fromcheck.h"
#include "md5.h"

struct extant_mbox {/*{{{*/
  char *full_path;
  time_t mtime;
  size_t size;
  int db_index;
  /* + stuff to store positions etc of individual messages. */
};
/*}}}*/
static int compare_extant_mboxen(const void *a, const void *b)/*{{{*/
{
  const struct extant_mbox *aa = (const struct extant_mbox *) a;
  const struct extant_mbox *bb = (const struct extant_mbox *) b;
  return strcmp(aa->full_path, bb->full_path);
}
/*}}}*/
static int lookup_extant_mbox(struct extant_mbox *sorted_mboxen, int n_extant, char *key)/*{{{*/
{
  /* Implement bisection search */
 int l, h, m, r;
 l = 0, h = n_extant;
 m = -1;
 while (h > l) {
   m = (h + l) >> 1;
   /* Should only get called on 'file' type messages - TBC */
   r = strcmp(sorted_mboxen[m].full_path, key);
   if (r == 0) break;
   if (l == m) return -1;
   if (r > 0) h = m;
   else       l = m;
 }
 return m;
}
/*}}}*/
static void append_new_mboxen_to_db(struct database *db, struct extant_mbox *extant_mboxen, int n_extant)/*{{{*/
{
  int N, n_reqd;
  int i, j;

  for (i=N=0; i<n_extant; i++) {
    if (extant_mboxen[i].db_index < 0) N++;
  }
  
  n_reqd = db->n_mboxen + N;
  if (n_reqd > db->max_mboxen) {
    db->max_mboxen = n_reqd;
    db->mboxen = grow_array(struct mbox, n_reqd, db->mboxen);
  }
  /* Init new entries. */
  for (j=0, i=db->n_mboxen; j<n_extant; j++) {
    if (extant_mboxen[j].db_index < 0) {
      db->mboxen[i].path = new_string(extant_mboxen[j].full_path);
      db->mboxen[i].current_mtime = extant_mboxen[j].mtime;
      db->mboxen[i].current_size = extant_mboxen[j].size;
      db->mboxen[i].file_mtime = 0;
      db->mboxen[i].file_size = 0;
      db->mboxen[i].n_msgs = 0;
      db->mboxen[i].n_old_msgs_valid = 0;
      db->mboxen[i].max_msgs = 0;
      db->mboxen[i].start = NULL;
      db->mboxen[i].len = NULL;
      db->mboxen[i].check_all = NULL;
      i++;
    }
  }

  db->n_mboxen = n_reqd;
}
/*}}}*/
void compute_checksum(const unsigned char *data, size_t len, checksum_t *csum)/*{{{*/
{
  MD5_CTX md5;
  MD5Init(&md5);
  MD5Update(&md5, data, len);
  MD5Final(&md5);
  memcpy(csum, md5.digest, sizeof(md5.digest));
  return;
}
/*}}}*/
static int message_is_intact(struct mbox *mb, int idx, char *va, size_t len)/*{{{*/
{
  /* TODO : later, look at whether to optimise this in some way, e.g. by doing
     an initial check on just the first 1k of a message, this will detect
     failures much faster at the cost of extra storage. */

  if (mb->start[idx] + mb->len[idx] > len) {
    /* Message overruns the end of the file - can't possibly be intact. */
    return 0;
  } else {
    checksum_t csum;
    compute_checksum(va + mb->start[idx], mb->len[idx], &csum);
    if (!memcmp(mb->check_all[idx], &csum, sizeof(checksum_t))) {
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}
/*}}}*/
static int find_number_intact(struct mbox *mb, unsigned char *va, size_t len)/*{{{*/
{
  /* Pick up the common obvious case first - where new messages have been appended to the
     end of the mbox */
  if (mb->n_msgs == 0) {
    return 0;
  } else if (message_is_intact(mb, mb->n_msgs - 1, va, len)) {
    return mb->n_msgs; /* The lot */
  } else if (!message_is_intact(mb, 0, va, len)) {
    return 0; /* None of them. */
  } else {
    /* Looks like a deletion has occurred earlier in the file => binary chop
       search to find the last message that's still valid.  Assume that
       everything below a valid message is still valid itself (possibly
       dangerous assumption, time will tell.) */

    int l, m, h;
    l = 0;
    h = mb->n_msgs;
    /* Loop invariant : always, mesasage[l] is intact, message[h] isn't. */
    while (l < h) {
      m = (h + l) >> 1;
      if (m==l) break;
      if (message_is_intact(mb, m, va, len)) {
        l = m;
      } else {
        h = m;
      }
    }
    /* By loop invariant, message[l] is the highest valid one. */
    return (l + 1);
  }
}
/*}}}*/

struct message_list {/*{{{*/
  struct message_list *next;
  off_t start;
  size_t len;
  checksum_t csum;
};
/*}}}*/

static int fromtab_inited = 0;
static signed char fromtab[256];

static void init_fromtab(void)/*{{{*/
{
  memset(fromtab, 0xff, 256);
  fromtab[(int)(unsigned char)'\n'] = ~(1<<0);
  fromtab[(int)(unsigned char)'F']  = ~(1<<1);
  fromtab[(int)(unsigned char)'r']  = ~(1<<2);
  fromtab[(int)(unsigned char)'o']  = ~(1<<3);
  fromtab[(int)(unsigned char)'m']  = ~(1<<4);
  fromtab[(int)(unsigned char)' ']  = ~(1<<5);
}
/*}}}*/

static signed char fromcheck_table[256];

/* REAL CHECKING : need to see if the line looks like this:
 * From [ <return-path> ] <weekday> <month> <day> <time> [ <timezone> ] <year>
   (from the mutt sources).
 * where timezone can be two words rather than one sometimes. */

#undef DEBUG_DFA

static int looks_like_from_separator(off_t n, unsigned char *va, size_t len, int verbose)/*{{{*/
{
  unsigned char p;
  int current_state = 0;
  int result = 0;

  n++; /* look beyond the space. */

  while (n < len) {
    p = va[n++];
    if (verbose) {
      printf("current_state=%d, p=%02x (%1c) ", current_state, (int)p, ((p>=32)&&(p<=126))?p:'.');
    }
    current_state = fromcheck_next_state(current_state, (int)fromcheck_table[(int)p]);
    if (verbose) {
      printf("next_state=%d\n", current_state);
    }
    if (current_state < 0) {
      /* not matched */
      break;
    }
    if (fromcheck_exitval[current_state] == FROMCHECK_PASS) {
      result = 1; /* matched good separator */
      break;
    }
  }

  /* If we hit the end of the file, it doesn't look like a real 'From' line. */
#ifdef DEBUG_DFA
  exit(0);
#endif
  return result;
}
/*}}}*/

static void init_fromcheck_table()/*{{{*/
{
  int i;
  for (i=0; i<256; i++)   fromcheck_table[i] = -1;
  for (i='A'; i<='Z'; i++) fromcheck_table[i] = FS_UPPER;
  for (i='a'; i<='z'; i++) fromcheck_table[i] = FS_LOWER;
  for (i='0'; i<='9'; i++) fromcheck_table[i] = FS_DIGIT;

  fromcheck_table['+'] = FS_PLUSMINUS;
  fromcheck_table['-'] = FS_PLUSMINUS;
  fromcheck_table['@'] = FS_AT;
  fromcheck_table[':'] = FS_COLON;
  fromcheck_table['\n'] = FS_LF;
  fromcheck_table['\r'] = FS_CR;
  fromcheck_table[' '] = FS_WHITE;
  fromcheck_table['\t'] = FS_WHITE;
  fromcheck_table['_'] = FS_OTHEREMAIL;
  fromcheck_table['.'] = FS_OTHEREMAIL;
  fromcheck_table['='] = FS_OTHEREMAIL;
}
/*}}}*/
static off_t find_next_from(off_t n, unsigned char *va, size_t len)/*{{{*/
{
  unsigned char c;
  unsigned long hit;
  unsigned long reg;
  unsigned long mask;

  if (!n) {
    if ((len >= 5) && !strncmp(va, "From ", 5)) {
      return 0;
    }
  }

scan_again:

  reg = (unsigned long) -1;
  hit = ~(1<<5);
  while (n < len) {
    c = va[n];
    mask = (unsigned long)(signed long) fromtab[(int)c];
    reg = (reg << 1) | mask;
    if (~(reg|hit)) {
      if (looks_like_from_separator(n, va, len, 0)) {
        return (n-4);
      } else {
#if 0
        int nn;
        printf("Rejecting from line at %d\n", n);
        nn = n;
        printf(" >> ");
        while (nn < len) {
          unsigned char c = va[nn++];
          putchar(c);
          if (c=='\n') break;
        }
        looks_like_from_separator(n, va, len, 1);
#endif
        goto scan_again;
      }
    }
    n++;
  }
  return -1;
}
/*}}}*/
static off_t start_of_next_line(off_t n, unsigned char *va, size_t len)/*{{{*/
{
  unsigned char c;
  /* We are always starting from 'From ' so we can advance before testing */
  do {
    c = va[n];
    n++;
  }
  while ((n < len) && (c != '\n'));
  if (n == len) {
    return -1;
  } else {
    return n;
  }
}
/*}}}*/


static struct message_list *build_new_message_list(struct mbox *mb, unsigned char *va, size_t len, int *n_messages)/*{{{*/
{
  struct message_list *result, *here, *next;
  off_t start_from, start_pos, end_from;
  int old_percent = -100;
  int N;

#define PERCENT_GRAN 2

  *n_messages = 0;

  result = here = NULL;
  N = mb->n_old_msgs_valid;
  if (N == 0) {
    start_from = 0;
  } else {
    /* Must point to the \n at the end of the preceding message, otherwise the
       'From ' at the start of the first message in the section to be rescanned
       won't get detected and that message won't get indexed. */
    start_from = mb->start[N - 1] + mb->len[N - 1] - 1;
  }

  if (!fromtab_inited) {
    init_fromtab();
    fromtab_inited = 1;
  }

  /* Locate next 'From ' at the start of a line */
  start_from = find_next_from(start_from, va, len);
  while (start_from != -1) {
    start_pos = start_of_next_line(start_from, va, len);
    if (start_pos == -1) {
      /* Something is awry. */
      goto done;
    }
    if (verbose) {
      int percent;
      percent = (int)(0.5 + 100.0 * (double) start_pos / (double) len);
      if (percent > (old_percent+PERCENT_GRAN)) {
        printf("Scanning mbox %s : %3d%% done\r", mb->path, percent);
        fflush(stdout);
        old_percent = percent;
      }
    }

    end_from = find_next_from(start_pos, va, len);
    next = new(struct message_list);
    next->next = NULL;
    next->start = start_pos;
    if (end_from == -1) {
      /* message runs through to end of file. */
      next->len = len - start_pos;
    } else {
      next->len = end_from - start_pos;
    }
    compute_checksum(va + start_pos, next->len, &next->csum);
    if (!result) {
      result = here = next;
    } else {
      here->next = next;
      here = next;
    }
    ++*n_messages;
    start_from = end_from;
  }

done:
  if (verbose) {
    printf("Scanning mbox %s : 100%% done\n", mb->path);
    fflush(stdout);
  }
  return result;

}
/*}}}*/
static void append_new_messages(struct mbox *mb, struct message_list *list, int n, unsigned char *va)/*{{{*/
{
  int j;
  struct message_list *here, *next;
  int N;

  N = mb->n_old_msgs_valid + n;
  if (N > mb->max_msgs) {
    mb->max_msgs = N;
    mb->start = grow_array(off_t, N, mb->start);
    mb->len = grow_array(size_t, N, mb->len);
    mb->check_all = grow_array(checksum_t, N, mb->check_all);
  }
  for (j=mb->n_old_msgs_valid, here=list; j<N; j++, here=next) {
    next = here->next;
    mb->start[j] = here->start;
    mb->len[j] = here->len;
    memcpy(&mb->check_all[j], &here->csum, sizeof(checksum_t));

    free(here);
  }
  mb->n_msgs = N;
}
/*}}}*/
static void rescan_mbox(struct mbox *mb, unsigned char *va, size_t len)/*{{{*/
{
  /* We get here if it's determined that
   * 1. the mbox file still exists
   * 2. the mtime or size has changed, i.e. it's been modified in some way
        since the last mairix run.
  */

  struct message_list *new_messages;
  int n_new_messages;

  /* Find the last message in the box that appears to be intact. */
  mb->n_old_msgs_valid = find_number_intact(mb, va, len);
  new_messages = build_new_message_list(mb, va, len, &n_new_messages);
  append_new_messages(mb, new_messages, n_new_messages, va);
}
/*}}}*/
static void deaden_mbox(struct mbox *mb)/*{{{*/
{
  mb->n_old_msgs_valid = 0;
  mb->n_msgs = 0;
  
  free(mb->path);
  mb->path = NULL;

  if (mb->max_msgs > 0) {
    free(mb->start);
    free(mb->len);
    free(mb->check_all);
    mb->max_msgs = 0;
  }
}
/*}}}*/
static void marry_up_mboxen(struct database *db, struct extant_mbox *extant_mboxen, int n_extant)/*{{{*/
{
  int *old_to_new_idx;
  int i;

  for (i=0; i<n_extant; i++) extant_mboxen[i].db_index = -1;

  old_to_new_idx = NULL;
  if (db->n_mboxen > 0) {
    old_to_new_idx = new_array(int, db->n_mboxen);
    for (i=0; i<db->n_mboxen; i++) old_to_new_idx[i] = -1;

    for (i=0; i<db->n_mboxen; i++) {
      if (db->mboxen[i].path) {
        int idx;
        idx = lookup_extant_mbox(extant_mboxen, n_extant, db->mboxen[i].path);
        if (idx >= 0) {
          struct mbox *mb = &db->mboxen[i];
          old_to_new_idx[i] = idx;
          extant_mboxen[idx].db_index = i;
          mb->current_mtime = extant_mboxen[idx].mtime;
          mb->current_size  = extant_mboxen[idx].size;
        }
      }
    }
  }

  for (i=0; i<db->n_mboxen; i++) {
    if (old_to_new_idx[i] < 0) {
      /* old mbox is no more. */
      deaden_mbox(&db->mboxen[i]);
    }
  }

  /* Append entries for newly discovered mboxen */
  append_new_mboxen_to_db(db, extant_mboxen, n_extant);

  /* From here on, everything we need is in the db */
  if (old_to_new_idx)
    free(old_to_new_idx);

}
/*}}}*/
static void check_duplicates(struct extant_mbox *extant_mboxen, int n_extant)/*{{{*/
{
  /* Note, list is sorted at this point */
  int i;
  int any_dupl = 0;
  for (i=0; i<n_extant-1; i++) {
    if (!strcmp(extant_mboxen[i].full_path, extant_mboxen[i+1].full_path)) {
      printf("mbox %s is listed twice in the mairixrc file\n", extant_mboxen[i].full_path);
      any_dupl = 1;
    }
  }
  if (any_dupl) {
    printf("Exiting, the mairixrc file needs fixing\n");
    exit(1);
  }
}
/*}}}*/
static char *find_last_slash(char *in)/*{{{*/
{
  char *p = in;
  char *result = NULL;
  while (*p) {
    if (*p == '/') result = p;
    p++;
  }
  return result;
}
/*}}}*/
static void append_shallow(char *path, struct stat *sb, struct string_list *list, int (*filter)(const char *, struct stat *))/*{{{*/
{
  if ((*filter)(path, sb)) {
    struct string_list *nn = new(struct string_list);
    nn->data = new_string(path);
    nn->next = list;
    nn->prev = list->prev;
    list->prev->next = nn;
    list->prev = nn;
  }
}
/*}}}*/
static void append_deep(char *path, struct stat *sb, struct string_list *list, int (*filter)(const char *, struct stat *))/*{{{*/
{
  /* path is dir : read its contents, call append_shallow or self accordingly. */
  /* path is file : call append_shallow. */
  struct stat sb2;
  char *xpath;
  DIR *d;
  struct dirent *de;

  append_shallow(path, sb, list, filter);
  
  if (S_ISDIR(sb->st_mode)) {
    xpath = new_array(char, strlen(path) + 2 + NAME_MAX);
    d = opendir(path);
    if (d) {
      while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".")) continue;
        if (!strcmp(de->d_name, "..")) continue;
        strcpy(xpath, path);
        strcat(xpath, "/");
        strcat(xpath, de->d_name);
        if (stat(xpath, &sb2) >= 0) {
          if (S_ISREG(sb2.st_mode)) {
            append_shallow(xpath, &sb2, list, filter);
          } else if (S_ISDIR(sb2.st_mode)) {
            append_deep(xpath, &sb2, list, filter);
          }
        }
      }
      closedir(d);
    }
    free(xpath);
  }
}
/*}}}*/
static void handle_wild(char *path, char *last_comp, struct string_list *list,/*{{{*/
                        void (*append)(char *, struct stat *, struct string_list *, int(*)(const char*, struct stat *)),
                        int (*filter)(const char *, struct stat *))
{
  /* last_comp is the character within 'path' where the wildcard stuff starts. */
  struct globber *gg;
  char *temp_path, *xpath;
  DIR *d;
  struct dirent *de;

  gg = make_globber(last_comp);

  /* Null-terminate parent directory, i.e. null the character where the trailing / is */
  if (last_comp > path) {
    int len = last_comp - path;
    temp_path = new_array(char, len);
    memcpy(temp_path, path, len-1);
    temp_path[len-1] = '\0';
    xpath = new_array(char, len + 2 + NAME_MAX);
  } else {
    temp_path = new_string(".");
    xpath = new_array(char, 3 + NAME_MAX);
  }

  d = opendir(temp_path);
  if (d) {
    while ((de = readdir(d))) {
      if (!strcmp(de->d_name, ".")) continue;
      if (!strcmp(de->d_name, "..")) continue;
      if (is_glob_match(gg, de->d_name)) {
        struct stat xsb;
        strcpy(xpath, temp_path);
        strcat(xpath, "/");
        strcat(xpath, de->d_name);
        if (stat(xpath, &xsb) >= 0) {
          (*append)(xpath, &xsb, list, filter);
        }
      }
    }
    closedir(d);
  }
  
  free(temp_path);
  free(xpath);
  free(gg);
}
/*}}}*/
static void handle_single(char *path, struct string_list *list,/*{{{*/
                          void (*append)(char *, struct stat *, struct string_list *, int(*)(const char*, struct stat *)),
                          int (*filter)(const char *, struct stat *))
{
  struct stat sb;
  if (stat(path, &sb) >= 0) {
    (*append)(path, &sb, list, filter);
  }
}
/*}}}*/
static int filter_is_file(const char *x, struct stat *sb)/*{{{*/
{
  if (S_ISREG(sb->st_mode))
    return 1;
  else
    return 0;
}
/*}}}*/
static int is_wild(const char *x)/*{{{*/
{
  const char *p;
  p = x;
  while (*p) {
    switch (*p) {
      case '[':
      case '*':
      case '?':
        return 1;
    }
    p++;
  }
  return 0;
}
/*}}}*/
static void handle_one_path(const char *folder_base, const char *path, struct string_list *list, int (*filter)(const char *, struct stat *))/*{{{*/
{
  /* Valid syntaxen ([.]=optional):
   * [xxx/]foo : single path
   * [xxx/]foo... : if foo is a file, as before; if a directory, every ordinary file under it
   * [xxx/]wild : any single path matching the wildcard
   * [xxx/]wild... : consider each match of the wildcard by the rule 2 lines above

   * <wild> contains any of these shell-like metacharacters
   * * : any string of 1 or more arbitrary characters
   * ? : any 1 arbitrary character
   * [a-z] : character class
   * [^a-z] : negated character class.

  */
  int folder_base_len = strlen(folder_base);
  char *full_path;
  int is_abs;
  int len;
  char *last_slash;
  char *last_comp;

  is_abs = (path[0] == '/') ? 1 : 0;
  if (is_abs) {
    full_path = new_string(path);
  } else {
    full_path = new_array(char, folder_base_len + strlen(path) + 2);
    strcpy(full_path, folder_base);
    strcat(full_path, "/");
    strcat(full_path, path);
  }
  len = strlen(full_path);
  last_slash = find_last_slash(full_path);
  last_comp = last_slash ? (last_slash + 1) : full_path;
  if ((len >= 4) && !strcmp(full_path + (len - 3), "...")) {
    full_path[len - 3] = '\0';
    if (is_wild(last_comp)) {
      handle_wild(full_path, last_comp, list, append_deep, filter);
    } else {
      handle_single(full_path, list, append_deep, filter);
    }
  } else {
    if (is_wild(last_comp)) {
      handle_wild(full_path, last_comp, list, append_shallow, filter);
    } else {
      handle_single(full_path, list, append_shallow, filter);
    }
  }
  free(full_path);
}
/*}}}*/
void glob_and_expand_paths(const char *folder_base, char **paths_in, int n_in, char ***paths_out, int *n_out, int (*filter)(const char *, struct stat *))/*{{{*/
{
  struct string_list list;
  int i;

  /* Clear it. */
  list.next = list.prev = &list;

  for (i=0; i<n_in; i++) {
    char *path = paths_in[i];
    handle_one_path(folder_base, path, &list, filter); 
  }

  string_list_to_array(&list, n_out, paths_out);
}
/*}}}*/

void build_mbox_lists(struct database *db, const char *folder_base, const char *mboxen_paths)/*{{{*/
{
  char **raw_paths, **paths;
  int n_raw_paths, i;
  int n_paths;
  struct stat sb;

  int n_extant;
  struct extant_mbox *extant_mboxen;

  n_extant = 0;

  if (mboxen_paths) {
    split_on_colons(mboxen_paths, &n_raw_paths, &raw_paths);
    glob_and_expand_paths(folder_base, raw_paths, n_raw_paths, &paths, &n_paths, filter_is_file);
    extant_mboxen = new_array(struct extant_mbox, n_paths);
  } else {
    n_paths = 0;
    paths = NULL;
    extant_mboxen = NULL;
  }

  /* Assume maximal size array. TODO : new strategy when globbing is included.
   * */

  /* TODO TODO ::: Build a sorted list of the paths and check that there aren't
     any duplicates!! */

  for (i=0; i<n_paths; i++) {
    char *path = paths[i];
    if (stat(path, &sb) < 0) {
      /* can't stat */
    } else {
      extant_mboxen[n_extant].full_path = new_string(path);
      extant_mboxen[n_extant].mtime = sb.st_mtime;
      extant_mboxen[n_extant].size = sb.st_size;
      n_extant++;
    }
    free(paths[i]);
  }
  if (paths) {
    free(paths);
    paths=NULL;
  }

  /* Reconcile list against that in the db. : sort, match etc. */
  if (n_extant) {
    qsort(extant_mboxen, n_extant, sizeof(struct extant_mbox), compare_extant_mboxen);
  }

  check_duplicates(extant_mboxen, n_extant);

  marry_up_mboxen(db, extant_mboxen, n_extant);

  init_fromcheck_table();

  /* Now look for new/modified mboxen, find how many of the old messages are
   * still valid and scan the remainder. */

  for (i=0; i<db->n_mboxen; i++) {
    struct mbox *mb = &db->mboxen[i];
    if (mb->path) {
      if ((mb->current_mtime == mb->file_mtime) &&
          (mb->current_size  == mb->file_size)) {
        mb->n_old_msgs_valid = mb->n_msgs;
      } else {
        unsigned char *va;
        int len;
        create_ro_mapping(mb->path, &va, &len);
        if (va) {
          rescan_mbox(mb, va, len);
          munmap(va, (size_t) len);
        } else {
          /* Treat as dead mbox */
          deaden_mbox(mb);
        }
      }
    }
  }

  /* At the end of this, we want the db->mboxen table to contain up to date info about
   * the mboxen, together with how much of the old info was still current. */
}
/*}}}*/

int add_mbox_messages(struct database *db)/*{{{*/
{
  int i, j;
  int any_new = 0;
  unsigned char *va;
  int valen;

  for (i=0; i<db->n_mboxen; i++) {
    struct mbox *mb = &db->mboxen[i];
    va = NULL; /* lazy mmap */
    for (j=mb->n_old_msgs_valid; j<mb->n_msgs; j++) {
      int n;
      off_t start;
      size_t len;
      struct rfc822 *r8;
      maybe_grow_message_arrays(db);
      n = db->n_msgs;
      db->type[n] = MTY_MBOX;
      db->msgs[n].src.mbox.file_index = i;
      db->msgs[n].src.mbox.msg_index = j;

      if (!va) {
        create_ro_mapping(mb->path, &va, &valen);
      }
      if (!va) {
        fprintf(stderr, "Couldn't create mapping of file %s\n", mb->path);
        exit(1);
      }

      start = mb->start[j];
      len   = mb->len[j];
      r8 = data_to_rfc822(va + start, len);
      if (r8) {
        if (verbose) {
          printf("Scanning %s[%d] at [%d,%d)\n", mb->path, j, (int)start, (int)start + len);
        }
        db->msgs[n].date = r8->hdrs.date;
        tokenise_message(n, db, r8);
        free_rfc822(r8);
      } else {
        printf("Message in %s at [%d,%d) is misformatted\n", mb->path, (int)start, (int)start + len);
      }

      ++db->n_msgs;
      any_new = 1;
    }
    if (va) munmap(va, (size_t) valen);
  }
  return any_new;
}
/*}}}*/

/* OTHER */
void cull_dead_mboxen(struct database *db)/*{{{*/
{
  int n_alive, i, j, n;
  int *old_to_new;
  struct mbox *newtab;

  n = db->n_mboxen;
  for (i=0, n_alive=0; i<n; i++) {
    if (db->mboxen[i].path) n_alive++;
  }

  /* Simple case - no dead mboxen */
  if (n_alive == n) return;

  newtab = new_array(struct mbox, n_alive);
  old_to_new = new_array(int, n);
  for (i=0, j=0; i<n; i++) {
    if (db->mboxen[i].path) {
      old_to_new[i] = j;
      newtab[j] = db->mboxen[i];
      printf("Copying mbox[%d] to [%d], path=%s\n", i, j, db->mboxen[i].path);
      j++;
    } else {
      printf("Pruning old mbox[%d], dead\n", i);
      old_to_new[i] = -1;
    }
  }

  /* Renumber file indices in messages */
  n = db->n_msgs;
  for (i=0; i<n; i++) {
    if (db->type[i] == MTY_MBOX) {
      int old_idx = db->msgs[i].src.mbox.file_index;
      assert(old_to_new[old_idx] != -1);
      db->msgs[i].src.mbox.file_index = old_to_new[old_idx];
    }
  }

  /* Fix up pointers */
  db->n_mboxen = db->max_mboxen = n_alive;
  free(db->mboxen);
  db->mboxen = newtab;
  free(old_to_new);
  return;
}
/*}}}*/

unsigned int encode_mbox_indices(unsigned int mb, unsigned int msg)/*{{{*/
{
  unsigned int result;
  result = ((mb & 0xffff) << 16) | (msg & 0xffff);
  return result;
}
/*}}}*/
void decode_mbox_indices(unsigned int index, unsigned int *mb, unsigned int *msg)/*{{{*/
{
  *mb = (index >> 16) & 0xffff;
  *msg = (index & 0xffff);
}
/*}}}*/

int verify_mbox_size_constraints(struct database *db)
{
  int i;
  int fail;
  if (db->n_mboxen > 65536) {
    fprintf(stderr, "Too many mboxes (max 65536, you have %d)\n", db->n_mboxen);
    return 0;
  }
  fail = 0;
  for (i=0; i<db->n_mboxen; i++) {
    if (db->mboxen[i].n_msgs > 65536) {
      fprintf(stderr, "Too many messages in mbox %s (max 65536, you have %d)\n",
              db->mboxen[i].path, db->mboxen[i].n_msgs);
      fail = 1;
    }
  }
  if (fail) return 0;
  else      return 1;
}


