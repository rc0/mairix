/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004
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
#include "version.h"
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <unistd.h>
#include <ctype.h>
#include <locale.h>

#ifdef TEST_OOM
int total_bytes=0;
#endif

int verbose = 0;

static char *folder_base = NULL;
static char *maildir_folders = NULL;
static char *mh_folders = NULL;
static char *mboxen = NULL;
static char *mfolder = NULL;
static char *database_path = NULL;
static enum folder_type output_folder_type = FT_MAILDIR;
static int skip_integrity_checks = 0;

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
  char *result;
  for (p = text; *p && (*p != '='); p++) ;
  if (!*p) return NULL;
  p++;
  result = expand_string(p);
  return result;
}
/*}}}*/
static void add_folders(char **folders, char *extra_folders)/*{{{*/
{
  /* note : extra_pointers is stale after this routine exits. */
  
  if (!*folders) {
    *folders = extra_folders;
  } else {
    char *old_folders = *folders;
    char *new_folders;
    int old_len, extra_len;
    old_len = strlen(old_folders);
    extra_len = strlen(extra_folders);
    new_folders = new_array(char, old_len + extra_len + 2);
    strcpy(new_folders, old_folders);
    strcpy(new_folders + old_len, ":");
    strcpy(new_folders + old_len + 1, extra_folders);
    *folders = new_folders;
    free(old_folders);
  }
}
/*}}}*/
static void parse_output_folder(char *p)/*{{{*/
{
  char *temp;
  temp = copy_value(p);
  if (!strncasecmp(temp, "mh", 2)) {
    output_folder_type = FT_MH;
  } else if (!strncasecmp(temp, "maildir", 7)) {
    output_folder_type = FT_MAILDIR;
  } else if (!strncasecmp(temp, "raw", 3)) {
    output_folder_type = FT_RAW;
  } else if (!strncasecmp(temp, "mbox", 4)) {
    output_folder_type = FT_MBOX;
  }
  else {
    fprintf(stderr, "Unrecognized mformat <%s>\n", temp);
  }
  free(temp);
}
/*}}}*/
static void parse_rc_file(char *name)/*{{{*/
{
  FILE *in;
  char line[4096], *p;
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
      exit(2);
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
    exit(2);
  }

  lineno = 0;
  while(fgets(line, sizeof(line), in)) {
    lineno++;
    len = strlen(line);
    if (len > sizeof(line) - 4) {
      fprintf(stderr, "Line %d in %s too long, exiting\n", lineno, name);
      exit(2);
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
      if (!isspace(*(unsigned char *)p)) {
        all_blank = 0;
        break;
      }
    }
    
    if (all_blank) continue;
    
    /* Now a real line to parse */
    if (!strncasecmp(p, "base", 4)) folder_base = copy_value(p);
    else if (!strncasecmp(p, "folders", 7)) {
      fprintf(stderr, "'folders=' option in rc file is depracated, use 'maildir='\n");
      add_folders(&maildir_folders, copy_value(p));
    }
    else if (!strncasecmp(p, "maildir=", 8)) add_folders(&maildir_folders, copy_value(p));
    else if (!strncasecmp(p, "mh_folders=", 11)) {
      fprintf(stderr, "'mh_folders=' option in rc file is depracated, use 'mh='\n");
      add_folders(&mh_folders, copy_value(p));
    }
    else if (!strncasecmp(p, "mh=", 3)) add_folders(&mh_folders, copy_value(p));
    else if (!strncasecmp(p, "mbox=", 5)) add_folders(&mboxen, copy_value(p));
      
    else if (!strncasecmp(p, "mformat=", 8)) {
      parse_output_folder(p);
    }
    else if (!strncasecmp(p, "mfolder=", 8)) mfolder = copy_value(p);
    else if (!strncasecmp(p, "database=", 9)) database_path = copy_value(p);
    else if (!strncasecmp(p, "nochecks", 8)) skip_integrity_checks = 1;
    else {
      if (verbose) {
        fprintf(stderr, "Unrecognized option at line %d in %s\n", lineno, name);
      }
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
  /* Caveat : only examines the file-per-message case */
  char **sorted_paths;
  int i, n, nn;
  int result;

  n = msgs->n;
  sorted_paths = new_array(char *, n);
  for (i=0, nn=0; i<n; i++) {
    switch (msgs->type[i]) {
      case MTY_MBOX:
        break;
      case MTY_DEAD:
        assert(0);
        break;
      case MTY_FILE:
        sorted_paths[nn++] = msgs->paths[i].src.mpf.path;
        break;
    }
  }
  qsort(sorted_paths, nn, sizeof(char *), compare_strings);

  result = 0;
  for (i=1; i<nn; i++) {
    if (!strcmp(sorted_paths[i-1], sorted_paths[i])) {
      result = 1;
      break;
    }
  }
  
  free(sorted_paths);
  return result;
}
/*}}}*/

static void emit_int(int x)/*{{{*/
{
  char buf1[20], buf2[20];
  char *p, *q;
  int neg=0;
  p = buf1;
  *p = '0'; /* In case x is zero */
  if (x < 0) {
    neg = 1;
    x = -x;
  }
  while (x) {
    *p++ = '0' + (x % 10);
    x /= 10;
  }
  p--;
  q = buf2;
  if (neg) *q++ = '-';
  while (p >= buf1) {
    *q++ = *p--;
  }
  write(2, buf2, q-buf2);  
  return;
}
/*}}}*/
volatile void out_of_mem(char *file, int line, size_t size)/*{{{*/
{
  /* Hairy coding ahead - can't use any [s]printf, itoa etc because
   * those might try to use the heap! */

  int filelen;
  char *p;
  
  static char msg1[] = "Out of memory (at ";
  static char msg2[] = " bytes)\n";
  /* Perhaps even strlen is unsafe in this situation? */
  p = file;
  while (*p) p++;
  filelen = p - file;
  write(2, msg1, sizeof(msg1));
  write(2, file, filelen);
  write(2, ":", 1);
  emit_int(line);
  write(2, ", ", 2);
  emit_int(size);
  write(2, msg2, sizeof(msg2));
  exit(2); 
}
/*}}}*/
static void print_copyright(void)/*{{{*/
{
  fprintf(stderr,
          "mairix %s, Copyright (C) 2002-2004 Richard P. Curnow\n"
          "mairix comes with ABSOLUTELY NO WARRANTY.\n"
          "This is free software, and you are welcome to redistribute it\n"
          "under certain conditions; see the GNU General Public License for details.\n\n",
          PROGRAM_VERSION);
}
/*}}}*/
static void usage(void)/*{{{*/
{
  print_copyright();
  
  printf("mairix [-h]                                    : Show help\n"
         "mairix [-f <rcfile>] [-v] [-p]                 : Build index\n"
         "mairix [-f <rcfile>] [-a] [-t] expr1 ... exprN : Run search\n"
         "mairix [-f <rcfile>] -d                        : Dump database to stdout\n"
         "-h           : show this help\n"
         "-f <rcfile>  : use alternative rc file (default ~/.mairixrc)\n"
         "-v           : be verbose\n"
         "-p           : purge messages that no longer exist\n"
         "-a           : add new matches to match folder (default : clear it first)\n"
         "-t           : include all messages in same threads as matching messages\n"
         "-o <mfolder> : override setting of mfolder from mairixrc file\n"
         "-r           : force raw output regardless of mformat setting in mairixrc file\n"
         "expr_i       : search expression (all expr's AND'ed together):\n"
         "    word          : match word in whole message\n"
         "    t:word        : match word in To: header\n"
         "    c:word        : match word in Cc: header\n"
         "    f:word        : match word in From: header\n"
         "    a:word        : match word in To:, Cc: or From: headers (address)\n"
         "    s:word        : match word in Subject: header\n"
         "    b:word        : match word in message body\n"
         "    m:word        : match word in Message-ID: header\n"
         "    p:substring   : match substring of path\n"
         "    d:start-end   : match date range\n"
         "    z:low-high    : match messages in size range\n"
         "    bs:word       : match word in Subject: header or body (or any other group of prefixes)\n"
         "    s:word1+word2 : match both words in Subject:\n"
         "    s:word1,word2 : match either word or both words in Subject:\n"
         "    s:~word       : match messages not containing word in Subject:\n"
         "    s:substring=  : match substring in any word in Subject:\n"
         "    s:substring=2 : match substring with <=2 errors in any word in Subject:\n"
         "\n"
         "    (See documentation for more examples)\n"
         );
}
    /*}}}*/
/* Notes on folder management: {{{
 
   Assumption is that the user wants to keep the 'mfolder' directories under a
   common root with the real maildir folders.  This allows a common value for
   mutt's 'folder' variable => the '+' and '=' prefixes work better.  This
   means the indexer here can't just scan down all subdirectories of a single
   ancestor, because it'll pick up its own mfolders.  So, use environment
   variables to tailor the folders.
 
   MAIRIX_FOLDER_BASE is the common ancestor directory of the folders (aka
   mutt's 'folder' variable)

   MAIRIX_FOLDERS is a colon-separated list of folders underneath that, with
   the feature that '...' after a component means any maildir underneath that,
   e.g.

   MAIRIX_MFOLDER is the parent of the mfolders underneath the base
   
   MAIRIX_FOLDER_BASE = "/home/foobar/mail"
   MAIRIX_FOLDERS = "inbox:lists...:action:archive..."
   MAIRIX_MFOLDER = "mf"

   so /home/foobar/mail/vf/search1/{new,cur,tmp} contain the output for search1 etc.
   }}} */

int main (int argc, char **argv)/*{{{*/
{
  struct msgpath_array *msgs;
  struct database *db;

  char *arg_rc_file_path = NULL;
  char *arg_mfolder = NULL;
  char *e;
  int do_augment = 0;
  int do_threads = 0;
  int do_search = 0;
  int do_purge = 0;
  int any_updates = 0;
  int any_purges = 0;
  int do_help = 0;
  int do_raw_output = 0;
  int do_dump = 0;
  int do_integrity_checks = 1;

  setlocale(LC_CTYPE, "");

  while (++argv, --argc) {
    if (!*argv) {
      break;
    } else if (!strcmp(*argv, "-f") || !strcmp(*argv, "--rcfile")) {
      ++argv, --argc;
      arg_rc_file_path = *argv;
    } else if (!strcmp(*argv, "-t") || !strcmp(*argv, "--threads")) {
      do_search = 1;
      do_threads = 1;
    } else if (!strcmp(*argv, "-a") || !strcmp(*argv, "--augment")) {
      do_search = 1;
      do_augment = 1;
    } else if (!strcmp(*argv, "-o") || !strcmp(*argv, "--mfolder")) {
      ++argv, --argc;
      arg_mfolder = *argv;
    } else if (!strcmp(*argv, "-p") || !strcmp(*argv, "--purge")) {
      do_purge = 1;
    } else if (!strcmp(*argv, "-d") || !strcmp(*argv, "--dump")) {
      do_dump = 1;
    } else if (!strcmp(*argv, "-r") || !strcmp(*argv, "--raw-output")) {
      do_raw_output = 1;
    } else if (!strcmp(*argv, "-Q") || !strcmp(*argv, "--no-integrity-checks")) {
      do_integrity_checks = 0;
    } else if (!strcmp(*argv, "-v") || !strcmp(*argv, "--verbose")) {
      verbose = 1;
    } else if (!strcmp(*argv, "-h") ||
               !strcmp(*argv, "--help")) {
      do_help = 1;
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

  if (do_help) {
    usage();
    exit(0);
  }

  if (verbose) {
    print_copyright();
  }
  
  if (*argv) {
    /* There are still args to process */
    do_search = 1;
  }
      
  parse_rc_file(arg_rc_file_path);

  if (getenv("MAIRIX_FOLDER_BASE")) {
    folder_base = getenv("MAIRIX_FOLDER_BASE");
  }

  if (getenv("MAIRIX_MAILDIR_FOLDERS")) {
    maildir_folders = getenv("MAIRIX_MAIDIR_FOLDERS");
  }

  if (getenv("MAIRIX_MH_FOLDERS")) {
    mh_folders = getenv("MAIRIX_MH_FOLDERS");
  }

  if ((e = getenv("MAIRIX_MBOXEN"))) {
    mboxen = e;
  }

  if (getenv("MAIRIX_MFOLDER")) {
    mfolder = getenv("MAIRIX_MFOLDER");
  }

  if (getenv("MAIRIX_DATABASE")) {
    database_path = getenv("MAIRIX_DATABASE");
  }

  if (arg_mfolder) {
    mfolder = arg_mfolder;
  }
  
  if (skip_integrity_checks) {
    do_integrity_checks = 0;
  }
  
  if (!folder_base) {
    fprintf(stderr, "No folder_base/MAIRIX_FOLDER_BASE set\n");
    exit(2);
  }
  
  if (!database_path) {
    fprintf(stderr, "No database/MAIRIX_DATABASE set\n");
    exit(2);
  }

  if (do_raw_output) {
    output_folder_type = FT_RAW;
  }

  if (do_dump) {
    dump_database(database_path);
    return 0;

  } else if (do_search) {
    if (!mfolder) {
      if (output_folder_type != FT_RAW) {
        fprintf(stderr, "No mfolder/MAIRIX_MFOLDER set\n");
        exit(2);
      }
      mfolder = new_string("");
    }

    return search_top(do_threads, do_augment, database_path, folder_base, mfolder, argv, output_folder_type, verbose);
    
  } else {
    if (!maildir_folders && !mh_folders && !mboxen) {
      fprintf(stderr, "No [mh_]folders/mboxen/MAIRIX_[MH_]FOLDERS set\n");
      exit(2);
    }
    
    if (verbose) printf("Finding all currently existing messages...\n");
    msgs = new_msgpath_array();
    if (maildir_folders) {
      build_message_list(folder_base, maildir_folders, FT_MAILDIR, msgs);
    }
    if (mh_folders) {
      build_message_list(folder_base, mh_folders, FT_MH, msgs);
    }

    /* The next call sorts the msgs array as part of looking for duplicates. */
    if (check_message_list_for_duplicates(msgs)) {
      fprintf(stderr, "Message list contains duplicates - check your 'folders' setting\n");
      exit(2);
    }

    /* Try to open existing database */
    if (file_exists(database_path)) {
      if (verbose) printf("Reading existing database...\n");
      db = new_database_from_file(database_path, do_integrity_checks);
      if (verbose) printf("Loaded %d existing messages\n", db->n_msgs);
    } else {
      if (verbose) printf("Starting new database\n");
      db = new_database();
    }

    build_mbox_lists(db, folder_base, mboxen);
    
    any_updates = update_database(db, msgs->paths, msgs->n);
    if (do_purge) {
      any_purges = cull_dead_messages(db, do_integrity_checks);
    }
    if (1 || any_updates || any_purges) {
      /* For now write it every time.  This is obviously the most reliable method. */
      write_database(db, database_path, do_integrity_checks);
    }

#if 0
    get_db_stats(db);
#endif

    free_database(db);
    free_msgpath_array(msgs);

    return 0;
  }
  
}
/*}}}*/
