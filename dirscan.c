/*
  $Header: /cvs/src/mairix/dirscan.c,v 1.2 2002/07/29 23:03:47 richard Exp $

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

/* Traverse a directory tree and find maildirs, then list files in them. */

#include "mairix.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <assert.h>

struct msgpath_array *new_msgpath_array(void)/*{{{*/
{
  struct msgpath_array *result;
  result = new(struct msgpath_array);
  result->paths = NULL;
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
      if (x->paths[i].path) free(x->paths[i].path);
    }
    free(x->paths);
  }
  free(x);
}
/*}}}*/
static void add_file_to_list(char *x, unsigned long mtime, size_t message_size, struct msgpath_array *arr) {/*{{{*/
  char *y = new_string(x);
  if (arr->n == arr->max) {
    arr->max += 1024;
    arr->paths = grow_array(struct msgpath, arr->max, arr->paths);
  }
  arr->paths[arr->n].path = y;
  arr->paths[arr->n].mtime = mtime;
  arr->paths[arr->n].size = message_size;
  ++arr->n;
  return;
}
/*}}}*/
static void get_maildir_message_paths(char *folder_base, char *mdir, struct msgpath_array *arr)/*{{{*/
{
  char *subdir, *fname;
  int i;
  static char *subdirs[] = {"new", "cur"};
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int folder_base_len = strlen(folder_base);
  int mdir_len = strlen(mdir);

  /* FIXME : just store mdir-rooted paths in array and have common prefix elsewhere. */
  
  subdir = new_array(char, folder_base_len + mdir_len + 6);
  fname = new_array(char, folder_base_len + mdir_len + 8 + NAME_MAX);
  for (i=0; i<2; i++) { 
    strcpy(subdir, folder_base);
    strcat(subdir, "/");
    strcat(subdir, mdir);
    strcat(subdir, "/");
    strcat(subdir, subdirs[i]);
    d = opendir(subdir);
    if (d) {
      while ((de = readdir(d))) {
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
static void get_mh_message_paths(char *folder_base, char *mdir, struct msgpath_array *arr)/*{{{*/
{
  char *subdir, *fname;
  DIR *d;
  struct dirent *de;
  struct stat sb;
  int folder_base_len = strlen(folder_base);
  int mdir_len = strlen(mdir);

  /* FIXME : just store mdir-rooted paths in array and have common prefix elsewhere. */
  
  subdir = new_array(char, folder_base_len + mdir_len + 2);
  fname = new_array(char, folder_base_len + mdir_len + 8 + NAME_MAX);
  strcpy(subdir, folder_base);
  strcat(subdir, "/");
  strcat(subdir, mdir);
  d = opendir(subdir);
  if (d) {
    while ((de = readdir(d))) {
      strcpy(fname, subdir);
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
  free(subdir);
  free(fname);
  return;
}
/*}}}*/
static int has_child_dir(char *folder_base, char *parent, char *buffer, char *child)/*{{{*/
{
  struct stat sb;
  int result = 0;
  
  strcpy(buffer, folder_base);
  strcat(buffer, "/");
  strcat(buffer, parent);
  strcat(buffer, "/");
  strcat(buffer, child);
  
  if (stat(buffer,&sb) >= 0) {
    if (S_ISDIR(sb.st_mode)) {
      result = 1;
    }
  }

  return result;

}
/*}}}*/
static int looks_like_maildir(char *folder_base, char *name)/*{{{*/
{
  char *child_name;
  char *full_path;
  struct stat sb;
  int result = 0;
  
  child_name = (char *) malloc(strlen(folder_base) + strlen(name) + 6);
  full_path = new_array(char, strlen(folder_base) + strlen(name) + 2);
  strcpy(full_path, folder_base);
  strcat(full_path, "/");
  strcat(full_path, name);
  
  if (stat(full_path, &sb) >= 0) {
    if (S_ISDIR(sb.st_mode)) {
      if (has_child_dir(folder_base, name, child_name, "new") &&
          has_child_dir(folder_base, name, child_name, "cur") &&
          has_child_dir(folder_base, name, child_name, "tmp")) {
        result = 1;
      }
    }
  }

  free(child_name);
  free(full_path);

  return result;
}
/*}}}*/
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

  free(fname);
  free(sname);
  closedir(d);
  free(name);
  return;
}
/*}}}*/
static int message_compare(const void *a, const void *b)/*{{{*/
{
  struct msgpath *aa = (struct msgpath *) a;  
  struct msgpath *bb = (struct msgpath *) b;  
  return strcmp(aa->path, bb->path);
}
/*}}}*/
static void sort_message_list(struct msgpath_array *arr)/*{{{*/
{
  qsort(arr->paths, arr->n, sizeof(struct msgpath), message_compare);
}
/*}}}*/
void build_message_list(char *folder_base, char *folders, enum folder_type ft, struct msgpath_array *msgs)/*{{{*/
{
  char *left_to_do;
  
  left_to_do = folders;
  do {
    char *colon;
    char *this_folder;
    int len;
    
    colon = strchr(left_to_do, ':');
    if (colon) {
      len = colon - left_to_do;
      this_folder = new_array(char, len + 1);
      memcpy(this_folder, left_to_do, len);
      this_folder[len] = '\0';
      left_to_do = colon + 1;
    } else {
      this_folder = new_string(left_to_do);
      while (*left_to_do) ++left_to_do;
    }

    len = strlen(this_folder);
    if ((len >= 4) &&
        !strcmp(this_folder + (len - 3), "...")) {
      /* Multiple folder */
      this_folder[len - 3] = '\0';
      scan_directory(folder_base, this_folder, ft, msgs);
    } else {
      /* Single folder */
      switch (ft) {
        case FT_MAILDIR:
          if (looks_like_maildir(folder_base, this_folder)) {
            get_maildir_message_paths(folder_base, this_folder, msgs);
          }
          break;
        case FT_MH:
          get_mh_message_paths(folder_base, this_folder, msgs);
          break;
        default:
          assert(0);
          break;
      }
    }

    free(this_folder);

  } while (*left_to_do);

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


