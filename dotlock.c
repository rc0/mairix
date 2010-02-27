/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2005
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

#include "mairix.h"
#include <sys/utsname.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

static char *lock_file_name = NULL;

/* This locking code was originally written for tdl */

void lock_database(char *path, int forced_unlock)/*{{{*/
{
  struct utsname uu;
  struct passwd *pw;
  int pid;
  int len;
  char *tname;
  struct stat sb;
  FILE *out;

  if (uname(&uu) < 0) {
    perror("uname");
    exit(1);
  }
  pw = getpwuid(getuid());
  if (!pw) {
    perror("getpwuid");
    exit(1);
  }
  pid = getpid();
  len = 1 + strlen(path) + 5;
  lock_file_name = new_array(char, len);
  sprintf(lock_file_name, "%s.lock", path);

  if (forced_unlock) {
    unlock_database();
    forced_unlock = 0;
  }

  len += strlen(uu.nodename);
  /* add on max width of pid field (allow up to 32 bit pid_t) + 2 '.' chars */
  len += (10 + 2);
  tname = new_array(char, len);
  sprintf(tname, "%s.%d.%s", lock_file_name, pid, uu.nodename);
  out = fopen(tname, "w");
  if (!out) {
    fprintf(stderr, "Cannot open lock file %s for writing\n", tname);
    exit(1);
  }
  fprintf(out, "%d,%s,%s\n", pid, uu.nodename, pw->pw_name);
  fclose(out);

  if (link(tname, lock_file_name) < 0) {
    /* check if link count==2 */
    if (stat(tname, &sb) < 0) {
      fprintf(stderr, "Could not stat the lock file\n");
      unlink(tname);
      exit(1);
    } else {
      if (sb.st_nlink != 2) {
        FILE *in;
        in = fopen(lock_file_name, "r");
        if (in) {
          char line[2048];
          fgets(line, sizeof(line), in);
          line[strlen(line)-1] = 0; /* strip trailing newline */
          fprintf(stderr, "Database %s appears to be locked by (pid,node,user)=(%s)\n", path, line);
          unlink(tname);
          exit(1);
        }
      } else {
        /* lock succeeded apparently */
      }
    }
  } else {
    /* lock succeeded apparently */
  }
  unlink(tname);
  free(tname);
  return;
}
/*}}}*/
void unlock_database(void)/*{{{*/
{
  if (lock_file_name) unlink(lock_file_name);
  return;
}
/*}}}*/
void unlock_and_exit(int code)/*{{{*/
{
  unlock_database();
  exit(code);
}
/*}}}*/
