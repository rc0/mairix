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
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 * 
 **********************************************************************
 */

/* Traverse a directory tree and find maildirs, then list files in them. */

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>
#include "mairix.h"

struct msgpath_array *new_msgpath_array(void)/*{{{*/
{
  struct msgpath_array *result;
  result = new(struct msgpath_array);
  result->paths = NULL;
  result->type = NULL;
  result->n = 0;
  result->max = 0;
  return result;
}
/*}}}*/
void free_msgpath_array(struct msgpath_array *x)/*{{{*/
{
  int i;
  if (x->paths) {
    for (i=0; i<x->n; i++) {
      switch (x->type[i]) {
        case MTY_FILE:
          free(x->paths[i].src.mpf.path);
          break;
        case MTY_MBOX:
          break;
        case MTY_DEAD:
          break;
      }
    }
    free(x->type);
    free(x->paths);
  }
  free(x);
}
/*}}}*/
static void add_file_to_list(char *x, unsigned long mtime, size_t message_size, struct msgpath_array *arr) {/*{{{*/
  char *y = new_string(x);
  if (arr->n == arr->max) {
    arr->max += 1024;
    arr->paths = grow_array(struct msgpath,    arr->max, arr->paths);
    arr->type  = grow_array(enum message_type, arr->max, arr->type);
  }
  arr->type[arr->n] = MTY_FILE;
  arr->paths[arr->n].src.mpf.path = y;
  arr->paths[arr->n].src.mpf.mtime = mtime;
  arr->paths[arr->n].src.mpf.size = message_size;
  ++arr->n;
  return;
}
/*}}}*/
static void get_maildir_message_paths(char *folder, struct msgpath_array *arr)/*{{{*/
{
  char *subdir, *fname;
  int i;
  static char *subdirs[] = {"new", "cur"};
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int folder_len = strlen(folder);

  /* FIXME : just store mdir-rooted paths in array and have common prefix elsewhere. */
  
  subdir = new_array(char, folder_len + 6);
  fname = new_array(char, folder_len + 8 + NAME_MAX);
  for (i=0; i<2; i++) { 
    strcpy(subdir, folder);
    strcat(subdir, "/");
    strcat(subdir, subdirs[i]);
    d = opendir(subdir);
    if (d) {
      while ((de = readdir(d))) {
        /* TODO : Perhaps we ought to do some validation on the path here?
           i.e. check that the filename looks valid for a maildir message. */
        strcpy(fname, subdir);
        strcat(fname, "/");
        strcat(fname, de->d_name);
        if (stat(fname, &sb) >= 0) {
          if (S_ISREG(sb.st_mode)) {
            add_file_to_list(fname, sb.st_mtime, sb.st_size, arr);
          }
        }
      }
      closedir(d);
    }
  }
  free(subdir);
  free(fname);
  return;
}
/*}}}*/
int is_integer_string(char *x)/*{{{*/
{
  char *p;
  
  if (!*x) return 0; /* Must not be empty */
  p = x;
  while (*p) {
    if (!isdigit(*p)) return 0;
    p++;
  }
  return 1;
}
/*}}}*/
static void get_mh_message_paths(char *folder, struct msgpath_array *arr)/*{{{*/
{
  char *fname;
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int folder_len = strlen(folder);

  fname = new_array(char, folder_len + 8 + NAME_MAX);
  d = opendir(folder);
  if (d) {
    while ((de = readdir(d))) {
      strcpy(fname, folder);
      strcat(fname, "/");
      strcat(fname, de->d_name);
      if (stat(fname, &sb) >= 0) {
        if (S_ISREG(sb.st_mode)) {
          if (is_integer_string(de->d_name)) {
            add_file_to_list(fname, sb.st_mtime, sb.st_size, arr);
          }
        }
      }
    }
    closedir(d);
  }
  free(fname);
  return;
}
/*}}}*/
static int has_child_dir(const char *base, const char *child)/*{{{*/
{
  int result = 0;
  struct stat sb;
  char *scratch;
  int len;

  len = strlen(base) + strlen(child) + 2;
  scratch = new_array(char, len);

  strcpy(scratch, base);
  strcat(scratch, "/");
  strcat(scratch, child);
  
  if (stat(scratch,&sb) >= 0) {
    if (S_ISDIR(sb.st_mode)) {
      result = 1;
    }
  }

  free(scratch);
  return result;
}
/*}}}*/
int filter_is_maildir(const char *path, struct stat *sb)/*{{{*/
{
  if (S_ISDIR(sb->st_mode)) {
    if (has_child_dir(path, "new") &&
        has_child_dir(path, "tmp") &&
        has_child_dir(path, "cur")) {
      return 1;
    }
  }
  return 0;
}
/*}}}*/
int filter_is_mh(const char *path, struct stat *sb)/*{{{*/
{
  /* At this stage, just check it's a directory. */
  if (S_ISDIR(sb->st_mode)) {
    return 1;
  } else {
    return 0;
  }
}
/*}}}*/
#if 0
static void scan_directory(char *folder_base, char *this_folder, enum folder_type ft, struct msgpath_array *arr)/*{{{*/
{
  DIR *d;
  struct dirent *de;
  struct stat sb;
  char *fname, *sname;
  char *name;
  int folder_base_len = strlen(folder_base);
  int this_folder_len = strlen(this_folder);

  name = new_array(char, folder_base_len + this_folder_len + 2);
  strcpy(name, folder_base);
  strcat(name, "/");
  strcat(name, this_folder);

  switch (ft) {
    case FT_MAILDIR:
      if (looks_like_maildir(folder_base, this_folder)) {
        get_maildir_message_paths(folder_base, this_folder, arr);
      }
      break;
    case FT_MH:
      get_mh_message_paths(folder_base, this_folder, arr);
      break;
    default:
      break;
  }
  
  fname = new_array(char, strlen(name) + 2 + NAME_MAX);
  sname = new_array(char, this_folder_len + 2 + NAME_MAX);

  d = opendir(name);
  if (d) {
    while ((de = readdir(d))) {
      if (!strcmp(de->d_name, ".") ||
          !strcmp(de->d_name, "..")) {
        continue;
      }

      strcpy(fname, name);
      strcat(fname, "/");
      strcat(fname, de->d_name);

      strcpy(sname, this_folder);
      strcat(sname, "/");
      strcat(sname, de->d_name);

      if (stat(fname, &sb) >= 0) {
        if (S_ISDIR(sb.st_mode)) {
          scan_directory(folder_base, sname, ft, arr);
        }
      }
    }
    closedir(d);
  }

  free(fname);
  free(sname);
  free(name);
  return;
}
/*}}}*/
#endif
static int message_compare(const void *a, const void *b)/*{{{*/
{
  /* FIXME : Is this a sensible way to do this with mbox messages in the picture? */
  struct msgpath *aa = (struct msgpath *) a;  
  struct msgpath *bb = (struct msgpath *) b;
  /* This should only get called on 'file' type messages - TBC! */
  return strcmp(aa->src.mpf.path, bb->src.mpf.path);
}
/*}}}*/
static void sort_message_list(struct msgpath_array *arr)/*{{{*/
{
  qsort(arr->paths, arr->n, sizeof(struct msgpath), message_compare);
}
/*}}}*/
static char *copy_folder_name(const char *start, const char *end)/*{{{*/
{
  /* 'start' points to start of string to copy.
     Any '\:' sequence is replaced by ':' .
     Otherwise \ is treated normally.
     'end' can be 1 beyond the end of the string to copy.  Otherwise it can be
     null, meaning treat 'start' as the start of a normal null-terminated
     string. */
  char *p;
  const char *q;
  int len;
  char *result;
  if (end) {
    len = end - start;
  } else {
    len = strlen(start);
  }
  result = new_array(char, len + 1);
  for (p=result, q=start;
       end ? (q < end) : *q;
       q++) {
    if ((q[0] == '\\') && (q[1] == ':')) {
      /* Escaped colon : drop the backslash */
    } else {
      *p++ = *q;
    }
  }
  *p = '\0';
  return result;
}
/*}}}*/
void string_list_to_array(struct string_list *list, int *n, char ***arr)/*{{{*/
{
  int N, i;
  struct string_list *a, *next_a;
  char **result;
  for (N=0, a=list->next; a!=list; a=a->next, N++) ;

  result = new_array(char *, N);
  for (i=0, a=list->next; i<N; a=next_a, i++) {
    result[i] = a->data;
    next_a = a->next;
    free(a);
  }

  *n = N;
  *arr = result;
}
/*}}}*/
void split_on_colons(const char *str, int *n, char ***arr)/*{{{*/
{
  struct string_list list, *new_cell;
  const char *left_to_do;

  list.next = list.prev = &list;
  left_to_do = str;
  do {
    char *colon;
    char *xx;

    colon = strchr(left_to_do, ':');
    /* Allow backslash-escaped colons in filenames */
    if (colon && (colon > left_to_do) && (colon[-1]=='\\')) {
      int is_escaped;
      do {
        colon = strchr(colon + 1, ':');
        is_escaped = (colon && (colon[-1] == '\\'));
      } while (colon && is_escaped);
    }
    /* 'colon' now points to the first non-escaped colon or is null if there
       were no more such colons in the rest of the line. */

    xx = copy_folder_name(left_to_do, colon);
    if (colon) {
      left_to_do = colon + 1;
    } else {
      while (*left_to_do) ++left_to_do;
    }

    new_cell = new(struct string_list);
    new_cell->data = xx;
    new_cell->next = &list;
    new_cell->prev = list.prev;
    list.prev->next = new_cell;
    list.prev = new_cell;
  } while (*left_to_do);

  string_list_to_array(&list, n, arr);

}
/*}}}*/
/*{{{ void build_message_list */
void build_message_list(char *folder_base, char *folders, enum folder_type ft,
    struct msgpath_array *msgs,
    struct globber_array *omit_globs)
{
  char **raw_paths, **paths;
  int n_raw_paths, n_paths, i;

  split_on_colons(folders, &n_raw_paths, &raw_paths);
  switch (ft) {
    case FT_MAILDIR:
      glob_and_expand_paths(folder_base, raw_paths, n_raw_paths, &paths, &n_paths, filter_is_maildir, omit_globs);
      for (i=0; i<n_paths; i++) {
        get_maildir_message_paths(paths[i], msgs);
      }
      break;
    case FT_MH:
      glob_and_expand_paths(folder_base, raw_paths, n_raw_paths, &paths, &n_paths, filter_is_mh, omit_globs);
      for (i=0; i<n_paths; i++) {
        get_mh_message_paths(paths[i], msgs);
      }
      break;
    default:
      assert(0);
      break;
  }
      
  if (paths) free(paths);

  sort_message_list(msgs);
  return;
}
/*}}}*/

#ifdef TEST
int main (int argc, char **argv)
{
  int i;
  struct msgpath_array *arr;
  
  arr = build_message_list(".");

  for (i=0; i<arr->n; i++) {
    printf("%08lx %s\n", arr->paths[i].mtime, arr->paths[i].path);
  }

  free_msgpath_array(arr);
  
  return 0;
}
#endif


