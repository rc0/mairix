/*
  $Header: /cvs/src/mairix/mairix.c,v 1.1 2002/07/03 22:15:59 richard Exp $

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

#include "mairix.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <ctype.h>

int verbose = 0;

static char *folder_base = NULL;
static char *folders = NULL;
static char *vfolder = NULL;
static char *database_path = NULL;

static int file_exists(char *name)/*{{{*/
{
  struct stat sb;

  if (stat(name, &sb) < 0) {
    return 0;
  }
  return 1;
}
/*}}}*/
static char *copy_value(char *text)/*{{{*/
{
  char *p;
  for (p = text; *p && (*p != '='); p++) ;
  if (!*p) return NULL;
  p++;
  return new_string(p);
}
/*}}}*/
static void parse_rc_file(char *name)/*{{{*/
{
  FILE *in;
  char line[1024], *p;
  int len, lineno;
  int all_blank;
  int used_default_name = 0;

  if (!name) {
    /* open default file */
    struct passwd *pw;
    char *home;
    pw = getpwuid(getuid());
    home = pw->pw_dir;
    if (!pw) {
      fprintf(stderr, "Cannot lookup passwd entry for this user\n");
      exit(1);
    }
    home = pw->pw_dir;
    name = new_array(char, strlen(home) + 12);
    strcpy(name, home);
    strcat(name, "/.mairixrc");
    used_default_name = 1;
  }
    
  in = fopen(name, "r");
  if (!in) {
    fprintf(stderr, "Cannot open %s, exiting\n", name);
    exit(1);
  }

  lineno = 0;
  while(fgets(line, sizeof(line), in)) {
    lineno++;
    len = strlen(line);
    if (len > sizeof(line) - 4) {
      fprintf(stderr, "Line %d in %s too long, exiting\n", lineno, name);
      exit(1);
    }

    if (line[len-1] == '\n') {
      line[len-1] = '\0';
    }
    
    /* Strip trailing comments. */
    for (p=line; *p && !strchr("#!;%", *p); p++) ;
    if (*p) *p = '\0';

    /* Discard blank lines */
    all_blank = 1;
    for (p=line; *p; p++) {
      if (!isspace(*p)) {
        all_blank = 0;
        break;
      }
    }
    
    if (all_blank) continue;
    
    /* Now a real line to parse */
    if (!strncasecmp(p, "base", 4)) folder_base = copy_value(p);
    else if (!strncasecmp(p, "folders", 7)) folders = copy_value(p);
    else if (!strncasecmp(p, "vfolder", 7)) vfolder = copy_value(p);
    else if (!strncasecmp(p, "database", 8)) database_path = copy_value(p);
    else {
      fprintf(stderr, "Unrecognized option at line %d in %s\n", lineno, name);
    }
  }
  
  fclose(in);

  if (used_default_name) free(name);
}
/*}}}*/
static int compare_strings(const void *a, const void *b)/*{{{*/
{
  const char **aa = (const char **) a;
  const char **bb = (const char **) b;
  return strcmp(*aa, *bb);
}
/*}}}*/
static int check_message_list_for_duplicates(struct msgpath_array *msgs)/*{{{*/
{
  char **sorted_paths;
  int i, n;
  int result;

  n = msgs->n;
  sorted_paths = new_array(char *, n);
  for (i=0; i<n; i++) {
    sorted_paths[i] = msgs->paths[i].path;
  }
  qsort(sorted_paths, n, sizeof(char *), compare_strings);
#if 0
  for (i=0; i<n; i++) {
    printf("%4d : %s\n", i, sorted_paths[i]);
  }
#endif
  result = 0;
  for (i=1; i<n; i++) {
    if (!strcmp(sorted_paths[i-1], sorted_paths[i])) {
      result = 1;
      break;
    }
  }
  
  free(sorted_paths);
  return result;
}
/*}}}*/

/* Notes on folder management:
 
   Assumption is that the user wants to keep the 'vfolder' directories under a
   common root with the real maildir folders.  This allows a common value for
   mutt's 'folder' variable => the '+' and '=' prefixes work better.  This
   means the indexer here can't just scan down all subdirectories of a single
   ancestor, because it'll pick up its own vfolders.  So, use environment
   variables to tailor the folders.
 
   MAIRIX_FOLDER_BASE is the common ancestor directory of the folders (aka
   mutt's 'folder' variable)

   MAIRIX_FOLDERS is a colon-separated list of folders underneath that, with
   the feature that '...' after a component means any maildir underneath that,
   e.g.

   MAIRIX_VFOLDER is the parent of the vfolders underneath the base
   
   MAIRIX_FOLDER_BASE = "/home/foobar/mail"
   MAIRIX_FOLDERS = "inbox:lists...:action:archive..."
   MAIRIX_VFOLDER = "vf"

   so /home/foobar/mail/vf/search1/{new,cur,tmp} contain the output for search1 etc.
   */

int main (int argc, char **argv)
{
  struct msgpath_array *msgs;
  struct database *db;

  char *arg_folder_base = NULL;
  char *arg_folders = NULL;
  char *arg_vfolder = NULL;
  char *arg_database_path = NULL;
  char *arg_rc_file_path = NULL;
  int do_augment = 0;
  int do_threads = 0;
  int do_search = 0;
  int do_purge = 0;
  int any_updates = 0;
  int any_purges = 0;

  while (++argv, --argc) {
    if (!*argv) {
      break;
    } else if (!strcmp(*argv, "-f")) {
      ++argv, --argc;
      arg_rc_file_path = *argv;
    } else if (!strcmp(*argv, "-t")) {
      do_search = 1;
      do_threads = 1;
    } else if (!strcmp(*argv, "-a")) {
      do_search = 1;
      do_augment = 1;
    } else if (!strcmp(*argv, "-p")) {
      do_purge = 1;
    } else if (!strcmp(*argv, "-v")) {
      verbose = 1;
    } else if ((*argv)[0] == '-') {
      fprintf(stderr, "Unrecognized option %s\n", *argv);
    } else if (!strcmp(*argv, "--")) {
      /* End of args */
      break;
    } else {
      /* standard args start */
      break;
    }
  }

  if (*argv) {
    /* There are still args to process */
    do_search = 1;
  }
      
  parse_rc_file(arg_rc_file_path);

  if (getenv("MAIRIX_FOLDER_BASE")) {
    folder_base = getenv("MAIRIX_FOLDER_BASE");
  }

  if (getenv("MAIRIX_FOLDERS")) {
    folders = getenv("MAIRIX_FOLDERS");
  }

  if (getenv("MAIRIX_VFOLDER")) {
    vfolder = getenv("MAIRIX_VFOLDER");
  }

  if (getenv("MAIRIX_DATABASE")) {
    database_path = getenv("MAIRIX_DATABASE");
  }

  if (!folder_base) {
    fprintf(stderr, "No folder_base/MAIRIX_FOLDER_BASE set\n");
    exit(1);
  }
  
  if (!database_path) {
    fprintf(stderr, "No database/MAIRIX_DATABASE set\n");
    exit(1);
  }
  
  if (do_search) {
    if (!vfolder) {
      fprintf(stderr, "No vfolder/MAIRIX_VFOLDER set\n");
      exit(1);
    }

    search_top(do_threads, do_augment, database_path, folder_base, vfolder, argv);
    
  } else {
    if (!folders) {
      fprintf(stderr, "No folders/MAIRIX_FOLDERS set\n");
      exit(1);
    }
    
    msgs = build_message_list(folder_base, folders);
    if (check_message_list_for_duplicates(msgs)) {
      fprintf(stderr, "Message list contains duplicates - check your 'folders' setting\n");
      exit(1);
    }

    /* Try to open existing database */
    if (file_exists(database_path)) {
      if (verbose) printf("Reading existing database...\n");
      db = new_database_from_file(database_path);
      if (verbose) printf("Loaded %d existing messages\n", db->n_paths);
    } else {
      if (verbose) printf("Starting new database\n");
      db = new_database();
    }
    
    any_updates = update_database(db, msgs->paths, msgs->n);
    if (do_purge) {
      any_purges = cull_dead_messages(db);
    }
    if (1 || any_updates || any_purges) {
      /* For now write it every time.  This is obviously the most reliable method. */
      write_database(db, database_path);
    }

#if 0
    get_db_stats(db);
#endif

    free_database(db);
    free_msgpath_array(msgs);
  }
  
  return 0;
  
}

