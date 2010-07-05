/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2006,2007
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

#ifdef VERBOSE_TEST
#define TEST 1
#endif

/* Parse name/value pairs from mail headers into a lookup table. */
#include <stdio.h>
#include <ctype.h>
#include "mairix.h"
#include "nvptypes.h"
#include "nvpscan.h"
#include "nvp.h"

enum nvp_type {/*{{{*/
  NVP_NAME,
  NVP_MAJORMINOR,
  NVP_NAMEVALUE
};
/*}}}*/
struct nvp_entry {/*{{{*/
  struct nvp_entry *next;
  struct nvp_entry *prev;
  enum nvp_type type;
  char *lhs;
  char *rhs;
};
/*}}}*/
struct nvp {/*{{{*/
  struct nvp_entry *first, *last;
};
/*}}}*/
static void append(struct nvp *nvp, struct nvp_entry *ne)/*{{{*/
{
  ne->next = NULL;
  ne->prev = nvp->last;
  if (nvp->last) nvp->last->next = ne;
  else nvp->first = ne;
  nvp->last = ne;
}
/*}}}*/
static void append_name(struct nvp *nvp, char *name)/*{{{*/
{
  struct nvp_entry *ne;
  ne = new(struct nvp_entry);
  ne->type = NVP_NAME;
  ne->lhs = new_string(name);
  append(nvp, ne);
}
/*}}}*/
static void append_majorminor(struct nvp *nvp, char *major, char *minor)/*{{{*/
{
  struct nvp_entry *ne;
  ne = new(struct nvp_entry);
  ne->type = NVP_MAJORMINOR;
  ne->lhs = new_string(major);
  ne->rhs = new_string(minor);
  append(nvp, ne);

}
/*}}}*/
static void append_namevalue(struct nvp *nvp, char *name, char *value)/*{{{*/
{
  struct nvp_entry *ne;
  ne = new(struct nvp_entry);
  ne->type = NVP_NAMEVALUE;
  ne->lhs = new_string(name);
  ne->rhs = new_string(value);
  append(nvp, ne);
}
/*}}}*/
static void combine_namevalue(struct nvp *nvp, char *name, char *value)/*{{{*/
{
  struct nvp_entry *n;
  for (n=nvp->first; n; n=n->next) {
    if (n->type == NVP_NAMEVALUE) {
      if (!strcmp(n->lhs, name)) {
        char *new_rhs;
        new_rhs = new_array(char, strlen(n->rhs) + strlen(value) + 1);
        strcpy(new_rhs, n->rhs);
        strcat(new_rhs, value);
        free(n->rhs);
        n->rhs = new_rhs;
        return;
      }
    }
  }
  /* No match : it's the first one */
  append_namevalue(nvp, name, value);
}
/*}}}*/
static void release_nvp(struct nvp *nvp)/*{{{*/
{
  struct nvp_entry *e, *ne;
  for (e=nvp->first; e; e=ne) {
    ne = e->next;
    switch (e->type) {
      case NVP_NAME:
        free(e->lhs);
        break;
      case NVP_MAJORMINOR:
      case NVP_NAMEVALUE:
        free(e->lhs);
        free(e->rhs);
        break;
    }
    free(e);
  }
  free(nvp);
}
/*}}}*/
struct nvp *make_nvp(struct msg_src *src, char *s, const char *pfx)/*{{{*/
{
  int current_state;
  unsigned int tok;
  char *q;
  unsigned char qq;
  char name[256];
  char minor[256];
  char value[256];
  enum nvp_action last_action, current_action;
  struct nvp *result;
  size_t pfxlen;
  char *nn, *mm, *vv;

  pfxlen = strlen(pfx);
  if (strncasecmp(pfx, s, pfxlen))
    return NULL;
  s += pfxlen;

  result = new(struct nvp);
  result->first = result->last = NULL;

  current_state = nvp_in;

  q = s;
  nn = name;
  mm = minor;
  vv = value;
  last_action = GOT_NOTHING;
  do {
    qq = *(unsigned char *) q;
    if (qq) {
      tok = nvp_char2tok[qq];
    } else {
      tok = nvp_EOS;
    }
    current_state = nvp_next_state(current_state, tok);
#ifdef VERBOSE_TEST
    fprintf(stderr, "Char %02x (%c) tok=%d new_current_state=%d\n",
        qq, ((qq>=32) && (qq<=126)) ? qq : '.',
        tok, current_state);
#endif

    if (current_state < 0) {
#ifdef TEST
      fprintf(stderr, "'%s' could not be parsed\n", s);
#else
      fprintf(stderr, "Header '%s%s' in %s could not be parsed\n",
          pfx, s, format_msg_src(src));
#endif
      release_nvp(result);
      return NULL;
    }

    switch (nvp_copier[current_state]) {
      case COPY_TO_NAME:
#ifdef VERBOSE_TEST
        fprintf(stderr, "  COPY_TO_NAME\n");
#endif
        *nn++ = *q;
        break;
      case COPY_TO_MINOR:
#ifdef VERBOSE_TEST
        fprintf(stderr, "  COPY_TO_MINOR\n");
#endif
        *mm++ = *q;
        break;
      case COPY_TO_VALUE:
#ifdef VERBOSE_TEST
        fprintf(stderr, "  COPY_TO_VALUE\n");
#endif
        *vv++ = *q;
        break;
      case COPY_NOWHERE:
        break;
    }

    current_action = nvp_action[current_state];
    switch (current_action) {
      case GOT_NAME:
      case GOT_NAME_TRAILING_SPACE:
      case GOT_MAJORMINOR:
      case GOT_NAMEVALUE:
      case GOT_NAMEVALUE_CONT:
#ifdef VERBOSE_TEST
        fprintf(stderr, "   Setting last action to %d\n", current_action);
#endif
        last_action = current_action;
        break;
      case GOT_TERMINATOR:
#ifdef VERBOSE_TEST
        fprintf(stderr, "   Hit terminator; last_action=%d\n", last_action);
#endif
        switch (last_action) {
          case GOT_NAME:
            *nn = 0;
            append_name(result, name);
            break;
          case GOT_NAME_TRAILING_SPACE:
            while (isspace(*--nn)) {}
            *++nn = 0;
            append_name(result, name);
            break;
          case GOT_MAJORMINOR:
            *nn = 0;
            *mm = 0;
            append_majorminor(result, name, minor);
            break;
          case GOT_NAMEVALUE:
            *nn = 0;
            *vv = 0;
            append_namevalue(result, name, value);
            break;
          case GOT_NAMEVALUE_CONT:
            *nn = 0;
            *vv = 0;
            combine_namevalue(result, name, value);
            break;
          default:
            break;
        }
        nn = name;
        mm = minor;
        vv = value;
        break;
      case GOT_NOTHING:
        break;
    }

    q++;
  } while (tok != nvp_EOS);

  return result;
}
/*}}}*/
void free_nvp(struct nvp *nvp)/*{{{*/
{
  struct nvp_entry *ne, *nne;
  for (ne = nvp->first; ne; ne=nne) {
    nne = ne->next;
    switch (ne->type) {
      case NVP_NAME:
        free(ne->lhs);
        break;
      case NVP_MAJORMINOR:
      case NVP_NAMEVALUE:
        free(ne->lhs);
        free(ne->rhs);
        break;
    }
    free(ne);
  }
  free(nvp);
}
/*}}}*/
const char *nvp_lookup(struct nvp *nvp, const char *name)/*{{{*/
{
  struct nvp_entry *ne;
  for (ne = nvp->first; ne; ne=ne->next) {
    if (ne->type == NVP_NAMEVALUE) {
      if (!strcmp(ne->lhs, name)) {
        return ne->rhs;
      }
    }
  }
  return NULL;
}
/*}}}*/
const char *nvp_lookupcase(struct nvp *nvp, const char *name)/*{{{*/
{
  struct nvp_entry *ne;
  for (ne = nvp->first; ne; ne=ne->next) {
    if (ne->type == NVP_NAMEVALUE) {
      if (!strcasecmp(ne->lhs, name)) {
        return ne->rhs;
      }
    }
  }
  return NULL;
}
/*}}}*/

void nvp_dump(struct nvp *nvp, FILE *out)/*{{{*/
{
  struct nvp_entry *ne;
  fprintf(out, "----\n");
  for (ne = nvp->first; ne; ne=ne->next) {
    switch (ne->type) {
      case NVP_NAME:
        fprintf(out, "NAME: %s\n", ne->lhs);
        break;
      case NVP_MAJORMINOR:
        fprintf(out, "MAJORMINOR: %s/%s\n", ne->lhs, ne->rhs);
        break;
      case NVP_NAMEVALUE:
        fprintf(out, "NAMEVALUE: %s=%s\n", ne->lhs, ne->rhs);
        break;
    }
  }
}
/*}}}*/

/* In these cases, we only look at the first entry */
const char *nvp_major(struct nvp *nvp)/*{{{*/
{
  struct nvp_entry *ne;
  ne = nvp->first;
  if (ne) {
    if (ne->type == NVP_MAJORMINOR) {
      return ne->lhs;
    } else {
      return NULL;
    }
  } else {
    return NULL;
  }
}
/*}}}*/
const char *nvp_minor(struct nvp *nvp)/*{{{*/
{
  struct nvp_entry *ne;
  ne = nvp->first;
  if (ne) {
    if (ne->type == NVP_MAJORMINOR) {
      return ne->rhs;
    } else {
      return NULL;
    }
  } else {
    return NULL;
  }
}
/*}}}*/
const char *nvp_first(struct nvp *nvp)/*{{{*/
{
  struct nvp_entry *ne;
  ne = nvp->first;
  if (ne) {
    if (ne->type == NVP_NAME) {
      return ne->lhs;
    } else {
      return NULL;
    }
  } else {
    return NULL;
  }
}
/*}}}*/

#ifdef TEST

static void do_test(char *s)
{
  struct nvp *n;
  n = make_nvp(NULL, s, "");
  if (n) {
    nvp_dump(n, stderr);
    free_nvp(n);
  }
}


int main (int argc, char **argv) {
  struct nvp *n;
#if 0
  do_test("attachment; filename=\"foo.c\"; prot=ro");
  do_test("attachment; filename= \"foo bar.c\" ;prot=ro");
  do_test("attachment ; filename= \"foo bar.c\" ;prot= ro");
  do_test("attachment ; filename= \"foo bar.c\" ;prot= ro");
  do_test("attachment ; filename= \"foo ;  bar.c\" ;prot= ro");
  do_test("attachment ; x*0=\"hi \"; x*1=\"there\"");
#endif

  do_test("application/vnd.ms-excel;       name=\"thequiz.xls\"");
#if 0
  do_test("inline; filename*0=\"aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj\t kkkkllll\"");
  do_test(" text/plain ; name= \"foo bar.c\" ;prot= ro/rw; read/write; read= foo bar");
#endif
  return 0;
}
#endif




