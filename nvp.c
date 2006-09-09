/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2006
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

/* Parse name/value pairs from mail headers into a lookup table. */
#include <stdio.h>
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
struct nvp *make_nvp(char *s)/*{{{*/
{
  int current_state;
  unsigned int tok;
  char *q;
  char name[256];
  char minor[256];
  char value[256];
  enum nvp_action last_action, current_action;
  struct nvp *result;

  char *nn, *mm, *vv;

  result = new(struct nvp);
  result->first = result->last = NULL;

  current_state = nvp_in;

  q = s;
  nn = name;
  mm = minor;
  vv = value;
  last_action = GOT_NOTHING;
  do {
    tok = *(unsigned char *) q;
    if (tok) {
      tok = nvp_char2tok[tok];
    } else {
      tok = nvp_EOS;
    }
    current_state = nvp_next_state(current_state, tok);
#if 0
    printf("char %02x '%c' meta=%2d state'=%2d ",
        *q,
        (*q >= 32 && *q <= 126) ? *q : '.',
        tok,
        current_state
        );
#endif

    switch (nvp_copier[current_state]) {
      case COPY_TO_NAME:
        *nn++ = *q;
        break;
      case COPY_TO_MINOR:
        *mm++ = *q;
        break;
      case COPY_TO_VALUE:
        *vv++ = *q;
        break;
    }

    current_action = nvp_action[current_state];
    switch (current_action) {
      case GOT_NAME:
      case GOT_NAME_TRAILING_SPACE:
      case GOT_MAJORMINOR:
      case GOT_NAMEVALUE:
        last_action = current_action;
        break;
      case GOT_TERMINATOR:
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
          default:
            break;
        }
        nn = name;
        mm = minor;
        vv = value;
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
int main (int argc, char **argv) {
  struct nvp *n;
  n = make_nvp("attachment; filename=\"foo.c\"; prot=ro");
  nvp_dump(n, stderr);
  free_nvp(n);
  n = make_nvp("attachment; filename= \"foo bar.c\" ;prot=ro");
  nvp_dump(n, stderr);
  free_nvp(n);
  n = make_nvp("attachment ; filename= \"foo bar.c\" ;prot= ro");
  nvp_dump(n, stderr);
  free_nvp(n);
  n = make_nvp("attachment ; filename= \"foo bar.c\" ;prot= ro");
  nvp_dump(n, stderr);
  free_nvp(n);
  n = make_nvp("attachment ; filename= \"foo ;  bar.c\" ;prot= ro");
  nvp_dump(n, stderr);
  free_nvp(n);
  n = make_nvp(" text/plain ; name= \"foo bar.c\" ;prot= ro/rw; read/write; read= foo bar");
  nvp_dump(n, stderr);
  free_nvp(n);
  return 0;
}
#endif




