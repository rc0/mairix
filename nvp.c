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
  if (!ne->rhs) {
    ne->rhs = Malloc(1);
    ne->rhs[0] = 0;
  }
  if (!ne->lhs) {
    ne->lhs = Malloc(1);
    ne->lhs[0] = 0;
  }
  ne->next = NULL;
  ne->prev = nvp->last;
  if (nvp->last) nvp->last->next = ne;
  else nvp->first = ne;
  nvp->last = ne;
}
/*}}}*/
static void append_name(struct nvp *nvp, char **name)/*{{{*/
{
  struct nvp_entry *ne;
  ne = new(struct nvp_entry);
  ne->type = NVP_NAME;
  ne->lhs = *name;
  *name = NULL;
  append(nvp, ne);
}
/*}}}*/
static void append_majorminor(struct nvp *nvp, char **major, char **minor)/*{{{*/
{
  struct nvp_entry *ne;
  ne = new(struct nvp_entry);
  ne->type = NVP_MAJORMINOR;
  ne->lhs = *major;
  ne->rhs = *minor;
  *major = *minor = NULL;
  append(nvp, ne);

}
/*}}}*/
static void append_namevalue(struct nvp *nvp, char **name, char **value)/*{{{*/
{
  struct nvp_entry *ne;
  ne = new(struct nvp_entry);
  ne->type = NVP_NAMEVALUE;
  ne->lhs = *name;
  ne->rhs = *value;
  *name = *value = NULL;
  append(nvp, ne);
}
/*}}}*/
static void combine_namevalue(struct nvp *nvp, char **name, char **value)/*{{{*/
{
  struct nvp_entry *n;
  for (n=nvp->first; n; n=n->next) {
    if (n->type == NVP_NAMEVALUE) {
      if (!strcmp(n->lhs, *name)) {
        char *new_rhs;
        new_rhs = new_array(char, strlen(n->rhs) + strlen(*value) + 1);
        strcpy(new_rhs, n->rhs);
        strcat(new_rhs, *value);
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
static int hex_to_val(int ch)/*{{{*/
{
    if (isdigit(ch))
	return (ch - '0');
    if (ch >= 'a' && ch <= 'f')
	return (10 + ch - 'a');
    if (ch >= 'A' && ch <= 'F')
	return (10 + ch - 'A');
    return (-1);
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
  char *q, *tempsrc, *tempdst;
  unsigned char qq;
  char *name = NULL;
  char *minor = NULL;
  char *value = NULL;
  char *copy_start;
  enum nvp_action last_action, current_action;
  enum nvp_copier last_copier;
  struct nvp *result;
  size_t pfxlen;

  pfxlen = strlen(pfx);
  if (strncasecmp(pfx, s, pfxlen))
    return NULL;
  s += pfxlen;

  result = new(struct nvp);
  result->first = result->last = NULL;

  current_state = nvp_in;

  q = s;
  last_action = GOT_NOTHING;
  last_copier = COPY_NOWHERE;
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

    if (nvp_copier[current_state] != last_copier) {
      if (last_copier != COPY_NOWHERE) {
        char *newstring = Malloc(q - copy_start + 1);
	memcpy(newstring, copy_start, q - copy_start);
	newstring[q - copy_start] = 0;
        switch (last_copier) {
          case COPY_TO_NAME:
            free(name);
            name = newstring;
#ifdef VERBOSE_TEST
            fprintf(stderr, "  COPY_TO_NAME \"%s\"\n", name);
#endif
            break;
          case COPY_TO_MINOR:
            free(minor);
            minor = newstring;
#ifdef VERBOSE_TEST
            fprintf(stderr, "  COPY_TO_MINOR \"%s\"\n", minor);
#endif
            break;
          case COPY_TO_VALUE:
            free(value);
            value = newstring;
#ifdef VERBOSE_TEST
            fprintf(stderr, "  COPY_TO_VALUE \"%s\"\n", value);
#endif
            break;
          case COPY_NOWHERE:
            /* NOTREACHED */
            break;
        }
      }
      last_copier = nvp_copier[current_state];
      copy_start = q;
    }

    current_action = nvp_action[current_state];
    switch (current_action) {
      case GOT_NAME:
      case GOT_NAME_TRAILING_SPACE:
      case GOT_MAJORMINOR:
      case GOT_NAMEVALUE:
      case GOT_NAMEVALUE_CONT:
      case GOT_NAMEVALUE_CSET:
      case GOT_NAMEVALUE_CCONT:
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
            append_name(result, &name);
            break;
          case GOT_NAME_TRAILING_SPACE:
            tempdst = name + strlen(name);
            while (isspace(*--tempdst)) {}
            *++tempdst = 0;
            append_name(result, &name);
            break;
          case GOT_MAJORMINOR:
            append_majorminor(result, &name, &minor);
            break;
          case GOT_NAMEVALUE:
            append_namevalue(result, &name, &value);
            break;
          case GOT_NAMEVALUE_CSET:
          case GOT_NAMEVALUE_CCONT:
	    for(tempsrc = tempdst = value; *tempsrc; tempsrc++) {
		if (*tempsrc == '%') {
		    int val = hex_to_val(*++tempsrc) << 4;
		    val |= hex_to_val(*++tempsrc);
		    if (val < 0) {
#ifdef TEST
			fprintf(stderr, "'%s' could not be parsed (%%)\n", s);
#else
			fprintf(stderr, "Header '%s%s' in %s could not be parsed\n",
				pfx, s, format_msg_src(src));
#endif
			release_nvp(result);
			result = NULL;
			goto out;
		    }
		    *tempdst++ = val;
		} else
		    *tempdst++ = *tempsrc;
	    }
            *tempdst = 0;
            if (current_action == GOT_NAMEVALUE_CSET)
              append_namevalue(result, &name, &value);
            else
              combine_namevalue(result, &name, &value);
            break;
          case GOT_NAMEVALUE_CONT:
            combine_namevalue(result, &name, &value);
            break;
          default:
            break;
        }
        break;
      case GOT_NOTHING:
        break;
    }

    q++;
  } while (tok != nvp_EOS);

out:
  /* Not all productions consume these values */
  free(name);
  free(value);
  free(minor);
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

  if (argc > 1) {
      while (*++argv)
	  do_test(*argv);
      return 0;
  }

#if 0
  do_test("attachment; filename=\"foo.c\"; prot=ro");
  do_test("attachment; filename= \"foo bar.c\" ;prot=ro");
  do_test("attachment ; filename= \"foo bar.c\" ;prot= ro");
  do_test("attachment ; filename= \"foo bar.c\" ;prot= ro");
  do_test("attachment ; filename= \"foo ;  bar.c\" ;prot= ro");
  do_test("attachment ; x*0=\"hi \"; x*1=\"there\"");
#endif
  do_test("attachment; filename*=utf-8''Section%204-1%20%E2%80%93%20Response%20Matrix%20PartIIA%2Edoc");
#if 0
  do_test("application/vnd.ms-excel;       name=\"thequiz.xls\"");
  do_test("inline; filename*0=\"aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj\t kkkkllll\"");
  do_test(" text/plain ; name= \"foo bar.c\" ;prot= ro/rw; read/write; read= foo bar");
#endif
  return 0;
}
#endif
