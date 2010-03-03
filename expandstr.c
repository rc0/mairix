/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2004
 * Copyright (C) Andreas Amann 2010
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
#include <stdlib.h>
#include <sys/types.h>
#include <pwd.h>
#include <ctype.h>
#include <unistd.h>

static int isenv(unsigned char x)/*{{{*/
{
  /* Return true if x is valid as part of an environment variable name. */
  if (isalnum(x))
    return 1;
  else if (x == '_')
    return 1;
  else
    return 0;
}
/*}}}*/
static int home_dir_len(void)/*{{{*/
{
  struct passwd *foo;
  char *lookup;
  lookup = getenv("HOME");
  if (lookup) {
    return strlen(lookup);
  }
  foo = getpwuid(getuid());
  return strlen(foo->pw_dir);
}
/*}}}*/
static char *env_lookup(const char *p, const char *q)/*{{{*/
{
  char *var;
  char *lookup, *result;
  char *s;
  var = new_array(char, (q-p)+1);
  for (s=var; p<q; p++, s++) {
    *s = *p;
  }
  *s = 0;
  lookup = getenv(var);
  if (lookup) {
    result = new_string(lookup);
  } else {
    result = NULL;
  }
  free(var);
  return result;
}
/*}}}*/
static int env_lookup_len(const char *p, const char *q) {/*{{{*/
  char *foo;
  int len;
  foo = env_lookup(p, q);
  if (!foo) len = 0;
  else {
    len = strlen(foo);
    free(foo);
  }
  return len;
}
/*}}}*/
static int compute_length(const char *p)/*{{{*/
{
  const char *q;
  int first;
  int len;
  first = 1;
  len = 0;
  while (*p) {
    if (first && (*p == '~') && (p[1] == '/')) {
      /* Make no attempt to expand ~other_user form */
      len += home_dir_len();
      p++;
    } else if ((*p == '$') && (p[1] == '{')) {
      p += 2;
      q = p;
      while (*q && (*q != '}')) q++;
      len += env_lookup_len(p, q);
      p = *q ? (q + 1) : q;
    } else if (*p == '$') {
      p++;
      q = p;
      while (*q && isenv(*(unsigned char*)q)) q++;
      len += env_lookup_len(p, q);
      p = q;
    } else {
      len++;
      p++;
    }
    first = 0;
  }
  return len;
}
/*}}}*/
static char *append_home_dir(char *to)/*{{{*/
{
  struct passwd *foo;
  int len;
  char *lookup;
  lookup = getenv("HOME");
  if (lookup) {
    len = strlen(lookup);
    strcpy(to, lookup);
  } else {
    foo = getpwuid(getuid());
    len = strlen(foo->pw_dir);
    strcpy(to, foo->pw_dir);
  }
  return to + len;
}
/*}}}*/
static char *append_env(char *to, const char *p, const char *q)/*{{{*/
{
  char *foo;
  int len;
  foo = env_lookup(p, q);
  if (foo) {
    len = strlen(foo);
    strcpy(to, foo);
    free(foo);
  } else {
    len = 0;
  }
  return (to + len);
}
/*}}}*/
static void do_expand(const char *p, char *result)/*{{{*/
{
  const char *q;
  int first;
  first = 1;
  while (*p) {
    if (first && (*p == '~') && (p[1] == '/')) {
      result = append_home_dir(result);
      p++;
    } else if ((*p == '$') && (p[1] == '{')) {
      p += 2;
      q = p;
      while (*q && (*q != '}')) q++;
      result = append_env(result, p, q);
      p = *q ? (q + 1) : q;
    } else if (*p == '$') {
      p++;
      q = p;
      while (*q && isenv(*(unsigned char*)q)) q++;
      result = append_env(result, p, q);
      p = q;
    } else {
      *result++ = *p++;
    }
    first = 0;
  }
  *result = 0;
}
/*}}}*/
char *expand_string(const char *p)/*{{{*/
{
  /* Return a copy of p, but with

     ~ expanded to the user's home directory
     $env expanded to the value of that environment variable
  */

  int len;
  char *result;

  len = compute_length(p);
  result = new_array(char, len+1);
  do_expand(p, result);
  return result;
}
/*}}}*/
