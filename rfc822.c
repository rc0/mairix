/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002,2003,2004,2005,2006,2007,2010
 * rfc2047 decode:
 * Copyright (C) Mikael Ylikoski 2002
 * gzip mbox support:
 * Copyright (C) Ico Doornekamp 2005
 * Copyright (C) Felipe Gustavo de Almeida 2005
 * bzip2 mbox support:
 * Copyright (C) Paramjit Oberoi 2005
 * caching uncompressed mbox data:
 * Copyright (C) Chris Mason 2006
 * memory leak fixes:
 * Copyright (C) Samuel Tardieu 2008
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
#include "nvp.h"

#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#ifdef USE_GZIP_MBOX
#  include <zlib.h>
#endif
#ifdef USE_BZIP_MBOX
#  include <bzlib.h>
#endif

struct DLL {/*{{{*/
  struct DLL *next;
  struct DLL *prev;
};
/*}}}*/
static void enqueue(void *head, void *x)/*{{{*/
{
  /* Declare this way so it can be used with any kind of double linked list
   * having next & prev pointers in its first two words. */
  struct DLL *h = (struct DLL *) head;
  struct DLL *xx = (struct DLL *) x;
  xx->next = h;
  xx->prev = h->prev;
  h->prev->next = xx;
  h->prev = xx;
  return;
}
/*}}}*/

enum encoding_type {/*{{{*/
  ENC_UNKNOWN,
  ENC_NONE,
  ENC_BINARY,
  ENC_7BIT,
  ENC_8BIT,
  ENC_QUOTED_PRINTABLE,
  ENC_BASE64,
  ENC_UUENCODE
};
/*}}}*/
struct content_type_header {/*{{{*/
  const char *major; /* e.g. text */
  const char *minor; /* e.g. plain */
  const char *boundary; /* for multipart */
  /* charset? */
};
/*}}}*/
struct line {/*{{{*/
  struct line *next;
  struct line *prev;
  char *text;
};
/*}}}*/

static void init_headers(struct headers *hdrs)/*{{{*/
{
  hdrs->to = NULL;
  hdrs->cc = NULL;
  hdrs->from = NULL;
  hdrs->subject = NULL;
  hdrs->message_id = NULL;
  hdrs->in_reply_to = NULL;
  hdrs->references = NULL;
  hdrs->date = 0;
  hdrs->flags.seen = 0;
  hdrs->flags.replied = 0;
  hdrs->flags.flagged = 0;
};
/*}}}*/
static void splice_header_lines(struct line *header)/*{{{*/
{
  /* Deal with newline then tab in header */
  struct line *x, *next;
  for (x=header->next; x!=header; x=next) {
#if 0
    printf("next header, x->text=%08lx\n", x->text);
    printf("header=<%s>\n", x->text);
#endif
    next = x->next;
    if (isspace(x->text[0] & 0xff)) {
      /* Glue to previous line */
      char *p, *newbuf, *oldbuf;
      struct line *y;
      for (p=x->text; *p; p++) {
        if (!isspace(*(unsigned char *)p)) break;
      }
      p--; /* point to final space */
      y = x->prev;
#if 0
      printf("y=%08lx p=%08lx\n", y->text, p);
#endif
      newbuf = new_array(char, strlen(y->text) + strlen(p) + 1);
      strcpy(newbuf, y->text);
      strcat(newbuf, p);
      oldbuf = y->text;
      y->text = newbuf;
      free(oldbuf);
      y->next = x->next;
      x->next->prev = y;
      free(x->text);
      free(x);
    }
  }
  return;
}
/*}}}*/
static int audit_header(struct line *header)/*{{{*/
{
  /* Check for obvious broken-ness
   * 1st line has no leading spaces, single word then colon
   * following lines have leading spaces or single word followed by colon
   * */
  struct line *x;
  int first=1;
  int count=1;
  for (x=header->next; x!=header; x=x->next) {
    int has_leading_space=0;
    int is_blank;
    int has_word_colon=0;

    if (1 || first) {
      /* Ignore any UUCP or mbox style From line at the start */
      if (!strncmp("From ", x->text, 5)) {
        continue;
      }
      /* Ignore escaped From line at the start */
      if (!strncmp(">From ", x->text, 6)) {
        continue;
      }
    }

    is_blank = !(x->text[0]);
    if (!is_blank) {
      char *p;
      int saw_char = 0;
      has_leading_space = isspace(x->text[0] & 0xff);
      has_word_colon = 0; /* default */
      p = x->text;
      while(*p) {
        if(*p == ':') {
          has_word_colon = saw_char;
          break;
        } else if (isspace(*(unsigned char *) p)) {
          has_word_colon = 0;
          break;
        } else {
          saw_char = 1;
        }
        p++;
      }
    }

    if (( first && (is_blank || has_leading_space || !has_word_colon)) ||
        (!first && (is_blank || !(has_leading_space || has_word_colon)))) {
#if 0
      fprintf(stderr, "Header line %d <%s> fails because:", count, x->text);
      if (first && is_blank) { fprintf(stderr, " [first && is_blank]"); }
      if (first && has_leading_space) { fprintf(stderr, " [first && has_leading_space]"); }
      if (first && !has_word_colon) { fprintf(stderr, " [first && !has_word_colon]"); }
      if (!first && is_blank) { fprintf(stderr, " [!first && is_blank]"); }
      if (!first && !(has_leading_space||has_word_colon)) { fprintf(stderr, " [!first && !has_leading_space||has_word_colon]"); }
      fprintf(stderr, "\n");
#endif
      /* Header fails the audit */
      return 0;
    }
    first = 0;
    count++;
  }
  /* If we get here the header must have been OK */
  return 1;
}/*}}}*/
static int match_string(const char *ref, const char *candidate)/*{{{*/
{
  int len = strlen(ref);
  return !strncasecmp(ref, candidate, len);
}
/*}}}*/

static char equal_table[] = {/*{{{*/
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 00-0f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 10-1f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 20-2f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,  /* 30-3f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 40-4f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 50-5f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 60-6f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 70-7f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 80-8f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 90-9f */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* a0-af */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* b0-bf */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* c0-cf */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* d0-df */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* e0-ef */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   /* f0-ff */
};
/*}}}*/
static int base64_table[] = {/*{{{*/
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* 00-0f */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* 10-1f */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  62,  -1,  -1,  -1,  63,  /* 20-2f */
   52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  -1,  -1,  -1,   0,  -1,  -1,  /* 30-3f */
   -1,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  /* 40-4f */
   15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  -1,  -1,  -1,  -1,  -1,  /* 50-5f */
   -1,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  /* 60-6f */
   41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  -1,  -1,  -1,  -1,  -1,  /* 70-7f */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* 80-8f */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* 90-9f */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* a0-af */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* b0-bf */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* c0-cf */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* d0-df */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /* e0-ef */
   -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1   /* f0-ff */
};
/*}}}*/
static int hex_to_val(char x) {/*{{{*/
  switch (x) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      return (x - '0');
      break;
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
      return 10 + (x - 'a');
      break;
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
      return 10 + (x - 'A');
      break;
    default:
      return 0;
  }
}
/*}}}*/
static void decode_header_value(char *text){/*{{{*/
  /* rfc2047 decode, written by Mikael Ylikoski */

  char *s, *a, *b, *e, *p, *q;

  for (p = q = s = text; (s = strstr(s, "=?")); s = e + 2) {
    if (p == q)
      p = q = s;
    else
      while (q != s)
        *p++ = *q++;
    s += 2;
    a = strchr(s, '?');
    if (!a) break;
    a++;
    b = strchr(a, '?');
    if (!b) break;
    b++;
    e = strstr(b, "?=");
    if (!e) break;
    /* have found an encoded-word */
    if (b - a != 2)
      continue; /* unknown encoding */
    if (*a == 'q' || *a == 'Q') {
      int val;
      q = b;
      while (q < e) {
        if (*q == '_') {
          *p++ = 0x20;
          q++;
        } else if (*q == '=') {
          q++;
          val = hex_to_val(*q++) << 4;
          val += hex_to_val(*q++);
          *p++ = val;
        } else
          *p++ = *q++;
      }
    } else if (*a == 'b' || *a == 'B') {
      int reg, nc, eq; /* register, #characters in reg, #equals */
      int dc; /* decoded character */
      eq = reg = nc = 0;
      for (q = b; q < e; q++) {
        unsigned char cq = *(unsigned char *)q;
        dc = base64_table[cq];
        eq += equal_table[cq];

        if (dc >= 0) {
          reg <<= 6;
          reg += dc;
          nc++;
          if (nc == 4) {
            *p++ = ((reg >> 16) & 0xff);
            if (eq < 2) *p++ = ((reg >> 8) & 0xff);
            if (eq < 1) *p++ = reg & 0xff;
            nc = reg = 0;
            if (eq) break;
          }
        }
      }
    } else {
      continue; /* unknown encoding */
    }
    q = e + 2;
  }
  if (p == q) return;
  while (*q != '\0')
    *p++ = *q++;
  *p = '\0';
}
/*}}}*/
static char *copy_header_value(char *text){/*{{{*/
  char *p;
  for (p = text; *p && (*p != ':'); p++) ;
  if (!*p) return NULL;
  p++;
  p = new_string(p);
  decode_header_value(p);
  return p;
}
/*}}}*/
static void copy_or_concat_header_value(char **previous, char *text){/*{{{*/
  char *p = copy_header_value(text);
  if (*previous)
  {
    *previous = extend_string(*previous, ", ");
    *previous = extend_string(*previous, p);
    free(p);
  }
  else
    *previous = p;
}
/*}}}*/
static enum encoding_type decode_encoding_type(const char *e)/*{{{*/
{
  enum encoding_type result;
  const char *p;
  if (!e) {
    result = ENC_NONE;
  } else {
    for (p=e; *p && isspace(*(unsigned char *)p); p++) ;
    if (   match_string("7bit", p)
        || match_string("7-bit", p)
        || match_string("7 bit", p)) {
      result = ENC_7BIT;
    } else if (match_string("8bit", p)
            || match_string("8-bit", p)
            || match_string("8 bit", p)) {
      result = ENC_8BIT;
    } else if (match_string("quoted-printable", p)) {
      result = ENC_QUOTED_PRINTABLE;
    } else if (match_string("base64", p)) {
      result = ENC_BASE64;
    } else if (match_string("binary", p)) {
      result = ENC_BINARY;
    } else if (match_string("x-uuencode", p)) {
      result = ENC_UUENCODE;
    } else {
      fprintf(stderr, "Warning: unknown encoding type: '%s'\n", e);
      result = ENC_UNKNOWN;
    }
  }
  return result;
}
/*}}}*/
static void parse_content_type(struct nvp *ct_nvp, struct content_type_header *result)/*{{{*/
{
  result->major = NULL;
  result->minor = NULL;
  result->boundary = NULL;

  result->major = nvp_major(ct_nvp);
  if (result->major) {
    result->minor = nvp_minor(ct_nvp);
  } else {
    result->minor = NULL;
    result->major = nvp_first(ct_nvp);
  }

  result->boundary = nvp_lookupcase(ct_nvp, "boundary");
}

/*}}}*/
static char *looking_at_ws_then_newline(char *start)/*{{{*/
{
  char *result;
  result = start;
  do {
         if (*result == '\n')   return result;
    else if (!isspace(*(unsigned char *) result)) return NULL;
    else                        result++;
  } while (1);

  /* Can't get here */
  assert(0);
}
/*}}}*/

static char *unencode_data(struct msg_src *src, char *input, int input_len, const char *enc, int *output_len)/*{{{*/
{
  enum encoding_type encoding;
  char *result, *end_result;
  char *end_input;

  encoding = decode_encoding_type(enc);
  end_input = input + input_len;

  /* All mime encodings result in expanded data, so this is guaranteed to
   * safely oversize the output array */
  result = new_array(char, input_len + 1);

  /* Now decode */
  switch (encoding) {
    case ENC_7BIT:/*{{{*/
    case ENC_8BIT:
    case ENC_BINARY:
    case ENC_NONE:
      {
        memcpy(result, input, input_len);
        end_result = result + input_len;
      }
      break;
/*}}}*/
    case ENC_QUOTED_PRINTABLE:/*{{{*/
      {
        char *p, *q;
        p = result;
        for (p=result, q=input;
             q<end_input; ) {

          if (*q == '=') {
            /* followed by optional whitespace then \n?  discard them. */
            char *r;
            int val;
            q++;
            r = looking_at_ws_then_newline(q);
            if (r) {
              q = r + 1; /* Point into next line */
              continue;
            }
            /* not that case. */
            val =  hex_to_val(*q++) << 4;
            val += hex_to_val(*q++);
            *p++ = val;

          } else {
            /* Normal character */
            *p++ = *q++;
          }
        }
        end_result = p;
      }
      break;
/*}}}*/
    case ENC_BASE64:/*{{{*/
      {
        char *p, *q;
        int reg, nc, eq; /* register, #characters in reg, #equals */
        int dc; /* decoded character */
        eq = reg = nc = 0;
        for (q=input, p=result; q<end_input; q++) {
          unsigned char cq =  * (unsigned char *)q;
          /* Might want a 256 entry array instead of this sub-optimal mess
           * eventually. */
          dc = base64_table[cq];
          eq += equal_table[cq];

          if (dc >= 0) {
            reg <<= 6;
            reg += dc;
            nc++;
            if (nc == 4) {
              *p++ = ((reg >> 16) & 0xff);
              if (eq < 2) *p++ = ((reg >> 8) & 0xff);
              if (eq < 1) *p++ = reg & 0xff;
              nc = reg = 0;
              if (eq) goto done_base_64;
            }
          }
        }
      done_base_64:
        end_result = p;
      }
      break;
        /*}}}*/
    case ENC_UUENCODE:/*{{{*/
      {
        char *p, *q;
        /* Find 'begin ' */
        for (q = input; q < end_input - 6 && memcmp(q, "begin ", 6); q++)
          ;
        q += 6;
        /* skip to EOL */
        while (q < end_input && *q != '\n')
          q++;
        p = result;
        while (q < end_input) { /* process line */
#define DEC(c) (((c) - ' ') & 077)
          int len = DEC(*q++);
          if (len == 0)
            break;
          for (; len > 0; q += 4, len -= 3) {
            if (len >= 3) {
              *p++ = DEC(q[0]) << 2 | DEC(q[1]) >> 4;
              *p++ = DEC(q[1]) << 4 | DEC(q[2]) >> 2;
              *p++ = DEC(q[2]) << 6 | DEC(q[3]);
            } else {
              if (len >= 1)
                *p++ = DEC(q[0]) << 2 | DEC(q[1]) >> 4;
              if (len >= 2)
                *p++ = DEC(q[1]) << 4 | DEC(q[2]) >> 2;
            }
          }
          while (q < end_input && *q != '\n')
            q++;
        }
        end_result = p;
      }
      break;
        /*}}}*/
    case ENC_UNKNOWN:/*{{{*/
      fprintf(stderr, "Unknown encoding type in %s\n", format_msg_src(src));
      /* fall through - ignore this data */
    /*}}}*/
    default:/*{{{*/
      end_result = result;
      break;
      /*}}}*/
  }
  *output_len = end_result - result;
  result[*output_len] = '\0'; /* for convenience with text/plain etc to make it printable */
  return result;
}
/*}}}*/
char *format_msg_src(struct msg_src *src)/*{{{*/
{
  static char *buffer = NULL;
  static int buffer_len = 0;
  char *result;
  int len;
  switch (src->type) {
    case MS_FILE:
      result = src->filename;
      break;
    case MS_MBOX:
      len = strlen(src->filename);
      len += 32;
      if (!buffer || (len > buffer_len)) {
        free(buffer);
        buffer = new_array(char, len);
        buffer_len = len;
      }
      sprintf(buffer, "%s[%d,%d)", src->filename,
          (int) src->start, (int) (src->start + src->len));
      result = buffer;
      break;
    default:
      result = NULL;
      break;
  }
  return result;
}
/*}}}*/
static int split_and_splice_header(struct msg_src *src, char *data, struct line *header, char **body_start)/*{{{*/
{
  char *sol, *eol;
  int blank_line;
  header->next = header->prev = header;
  sol = data;
  do {
    if (!*sol) break;
    blank_line = 1; /* until proven otherwise */
    eol = sol;
    while (*eol && (*eol != '\n')) {
      if (!isspace(*(unsigned char *) eol)) blank_line = 0;
      eol++;
    }
    if (*eol == '\n') {
      if (!blank_line) {
        int line_length = eol - sol;
        char *line_text = new_array(char, 1 + line_length);
        struct line *new_header;

        strncpy(line_text, sol, line_length);
        line_text[line_length] = '\0';
        new_header = new(struct line);
        new_header->text = line_text;
        enqueue(header, new_header);
      }
      sol = eol + 1; /* Start of next line */
    } else { /* must be null char */
      fprintf(stderr, "Got null character whilst processing header of %s\n",
          format_msg_src(src));
      return -1; /* & leak memory */
    }
  } while (!blank_line);

  *body_start = sol;

  if (audit_header(header)) {
    splice_header_lines(header);
    return 0;
  } else {
#if 0
    /* Caller generates message */
    fprintf(stderr, "Message had bad rfc822 headers, ignoring\n");
#endif
    return -1;
  }
}
/*}}}*/

/* Forward prototypes */
static void do_multipart(struct msg_src *src, char *input, int input_len,
    const char *boundary, struct attachment *atts,
    enum data_to_rfc822_error *error);

/*{{{ do_body() */
static void do_body(struct msg_src *src,
    char *body_start, int body_len,
    struct nvp *ct_nvp, struct nvp *cte_nvp,
    struct nvp *cd_nvp,
    struct attachment *atts,
    enum data_to_rfc822_error *error)
{
  char *decoded_body;
  int decoded_body_len;
  const char *content_transfer_encoding;
  content_transfer_encoding = NULL;
  if (cte_nvp) {
    content_transfer_encoding = nvp_first(cte_nvp);
    if (!content_transfer_encoding) {
      fprintf(stderr, "Giving up on %s, content_transfer_encoding header not parseable\n",
          format_msg_src(src));
      return;
    }
  }

  decoded_body = unencode_data(src, body_start, body_len, content_transfer_encoding, &decoded_body_len);

  if (ct_nvp) {
    struct content_type_header ct;
    parse_content_type(ct_nvp, &ct);
    if (ct.major && !strcasecmp(ct.major, "multipart")) {
      do_multipart(src, decoded_body, decoded_body_len, ct.boundary, atts, error);
      /* Don't need decoded body any longer - copies have been taken if
       * required when handling multipart attachments. */
      free(decoded_body);
      if (error && (*error == DTR8_MISSING_END)) return;
    } else {
      /* unipart */
      struct attachment *new_att;
      const char *disposition;
      new_att = new(struct attachment);
      disposition = cd_nvp ? nvp_first(cd_nvp) : NULL;
      if (disposition && !strcasecmp(disposition, "attachment")) {
        const char *lookup;
        lookup = nvp_lookupcase(cd_nvp, "filename");
        if (lookup) {
          new_att->filename = new_string(lookup);
        } else {
          /* Some messages have name=... in content-type: instead of
           * filename=... in content-disposition. */
          lookup = nvp_lookup(ct_nvp, "name");
          if (lookup) {
            new_att->filename = new_string(lookup);
          } else {
            new_att->filename = NULL;
          }
        }
      } else {
        new_att->filename = NULL;
      }
      if (ct.major && !strcasecmp(ct.major, "text")) {
        if (ct.minor && !strcasecmp(ct.minor, "plain")) {
          new_att->ct = CT_TEXT_PLAIN;
        } else if (ct.minor && !strcasecmp(ct.minor, "html")) {
          new_att->ct = CT_TEXT_HTML;
        } else {
          new_att->ct = CT_TEXT_OTHER;
        }
      } else if (ct.major && !strcasecmp(ct.major, "message") &&
                 ct.minor && !strcasecmp(ct.minor, "rfc822")) {
        new_att->ct = CT_MESSAGE_RFC822;
      } else {
        new_att->ct = CT_OTHER;
      }

      if (new_att->ct == CT_MESSAGE_RFC822) {
        new_att->data.rfc822 = data_to_rfc822(src, decoded_body, decoded_body_len, error);
        free(decoded_body); /* data no longer needed */
      } else {
        new_att->data.normal.len = decoded_body_len;
        new_att->data.normal.bytes = decoded_body;
      }
      enqueue(atts, new_att);
    }
  } else {
    /* Treat as text/plain {{{*/
    struct attachment *new_att;
    new_att = new(struct attachment);
    new_att->filename = NULL;
    new_att->ct = CT_TEXT_PLAIN;
    new_att->data.normal.len = decoded_body_len;
    /* Add null termination on the end */
    new_att->data.normal.bytes = new_array(char, decoded_body_len + 1);
    memcpy(new_att->data.normal.bytes, decoded_body, decoded_body_len + 1);
    free(decoded_body);
    enqueue(atts, new_att);/*}}}*/
  }
}
/*}}}*/
/*{{{ do_attachment() */
static void do_attachment(struct msg_src *src,
    char *start, char *after_end,
    struct attachment *atts)
{
  /* decode attachment and add to attachment list */
  struct line header, *x, *nx;
  char *body_start;
  int body_len;

  struct nvp *ct_nvp, *cte_nvp, *cd_nvp, *nvp;

  if (split_and_splice_header(src, start, &header, &body_start) < 0) {
    fprintf(stderr, "Giving up on attachment with bad header in %s\n",
        format_msg_src(src));
    return;
  }

  /* Extract key headers */
  ct_nvp = cte_nvp = cd_nvp = NULL;
  for (x=header.next; x!=&header; x=x->next) {
    if ((nvp = make_nvp(src, x->text, "content-type:"))) {
      ct_nvp = nvp;
    } else if ((nvp = make_nvp(src, x->text, "content-transfer-encoding:"))) {
      cte_nvp = nvp;
    } else if ((nvp = make_nvp(src, x->text, "content-disposition:"))) {
      cd_nvp = nvp;
    }
  }

#if 0
  if (ct_nvp) {
    fprintf(stderr, "======\n");
    fprintf(stderr, "Dump of content-type hdr\n");
    nvp_dump(ct_nvp, stderr);
    free(ct_nvp);
  }

  if (cte_nvp) {
    fprintf(stderr, "======\n");
    fprintf(stderr, "Dump of content-transfer-encoding hdr\n");
    nvp_dump(cte_nvp, stderr);
    free(cte_nvp);
  }
#endif

  if (body_start > after_end) {
    /* This is a (maliciously?) b0rken attachment, e.g. maybe empty */
    if (verbose) {
      fprintf(stderr, "Message %s contains an invalid attachment, length=%d bytes\n",
          format_msg_src(src), (int)(after_end - start));
    }
  } else {
    body_len = after_end - body_start;
    /* Ignore errors in nested body parts. */
    do_body(src, body_start, body_len, ct_nvp, cte_nvp, cd_nvp, atts, NULL);
  }

  /* Free header memory */
  for (x=header.next; x!=&header; x=nx) {
    nx = x->next;
    free(x->text);
    free(x);
  }

  if (ct_nvp) free_nvp(ct_nvp);
  if (cte_nvp) free_nvp(cte_nvp);
  if (cd_nvp) free_nvp(cd_nvp);
}
/*}}}*/
/*{{{ do_multipart() */
static void do_multipart(struct msg_src *src,
    char *input, int input_len,
    const char *boundary,
    struct attachment *atts,
    enum data_to_rfc822_error *error)
{
  char *b0, *b1, *be, *bx;
  char *line_after_b0, *start_b1_search_from;
  int boundary_len;
  int looking_at_end_boundary;

  if (!boundary) {
    fprintf(stderr, "Can't process multipart message %s with no boundary string\n",
        format_msg_src(src));
    if (error) *error = DTR8_MULTIPART_SANS_BOUNDARY;
    return;
  }

  boundary_len = strlen(boundary);

  b0 = NULL;
  line_after_b0 = input;
  be = input + input_len;

  do {
    int boundary_ok;
    start_b1_search_from = line_after_b0;
    do {
      /* reject boundaries that aren't a whole line */
      b1 = NULL;
      for (bx = start_b1_search_from; bx < be - (boundary_len + 2); bx++) {
        if (bx[0] == '-' && bx[1] == '-' &&
            !strncmp(bx+2, boundary, boundary_len)) {
          b1 = bx;
          break;
        }
      }
      if (!b1) {
        if (error)
          *error = DTR8_MISSING_END;
        return;
      }

      looking_at_end_boundary = (b1+boundary_len+3 < be) && (b1[boundary_len+2] == '-' &&
          b1[boundary_len+3] == '-');
      boundary_ok = 1;
      if ((b1 > input) && (*(b1-1) != '\n'))
        boundary_ok = 0;
      if (!looking_at_end_boundary && (b1 + boundary_len + 3 < be) && !(
          ((b1 + boundary_len + 2 < input + input_len) && (*(b1 + boundary_len + 2) == '\n')) ||
          ((b1 + boundary_len + 3 < input + input_len) && (*(b1 + boundary_len + 2) == '\r') && (*(b1 + boundary_len + 3) == '\n'))
      ))
        boundary_ok = 0;
      if (!boundary_ok) {
        char *eol = strchr(b1, '\n');
        if (!eol) {
          fprintf(stderr, "Oops, didn't find another normal boundary in %s\n",
              format_msg_src(src));
          return;
        }
        start_b1_search_from = 1 + eol;
      }
    } while (!boundary_ok);

    /* b1 is now looking at a good boundary, which might be the final one */

    if (b0) {
      /* don't treat preamble as an attachment */
      do_attachment(src, line_after_b0, b1, atts);
    }

    b0 = b1;
    line_after_b0 = strchr(b0, '\n');
    if (line_after_b0 == 0)
      line_after_b0 = b0 + strlen(b0);
    else
      ++line_after_b0;
  } while (b1 < be && !looking_at_end_boundary);
}
/*}}}*/
static time_t parse_rfc822_date(char *date_string)/*{{{*/
{
  struct tm tm;
  char *s, *z;
  /* Format [weekday ,] day-of-month month year hour:minute:second timezone.

     Some of the ideas, sanity checks etc taken from parse.c in the mutt
     sources, credit to Michael R. Elkins et al
     */

  s = date_string;
  z = strchr(s, ',');
  if (z) s = z + 1;
  while (*s && isspace(*s)) s++;
  /* Should now be looking at day number */
  if (!isdigit(*s)) goto tough_cheese;
  tm.tm_mday = atoi(s);
  if (tm.tm_mday > 31) goto tough_cheese;

  while (isdigit(*s)) s++;
  while (*s && isspace(*s)) s++;
  if (!*s) goto tough_cheese;
  if      (!strncasecmp(s, "jan", 3)) tm.tm_mon =  0;
  else if (!strncasecmp(s, "feb", 3)) tm.tm_mon =  1;
  else if (!strncasecmp(s, "mar", 3)) tm.tm_mon =  2;
  else if (!strncasecmp(s, "apr", 3)) tm.tm_mon =  3;
  else if (!strncasecmp(s, "may", 3)) tm.tm_mon =  4;
  else if (!strncasecmp(s, "jun", 3)) tm.tm_mon =  5;
  else if (!strncasecmp(s, "jul", 3)) tm.tm_mon =  6;
  else if (!strncasecmp(s, "aug", 3)) tm.tm_mon =  7;
  else if (!strncasecmp(s, "sep", 3)) tm.tm_mon =  8;
  else if (!strncasecmp(s, "oct", 3)) tm.tm_mon =  9;
  else if (!strncasecmp(s, "nov", 3)) tm.tm_mon = 10;
  else if (!strncasecmp(s, "dec", 3)) tm.tm_mon = 11;
  else goto tough_cheese;

  while (!isspace(*s)) s++;
  while (*s && isspace(*s)) s++;
  if (!isdigit(*s)) goto tough_cheese;
  tm.tm_year = atoi(s);
  if (tm.tm_year < 70) {
    tm.tm_year += 100;
  } else if (tm.tm_year >= 1900) {
    tm.tm_year -= 1900;
  }

  while (isdigit(*s)) s++;
  while (*s && isspace(*s)) s++;
  if (!*s) goto tough_cheese;

  /* Now looking at hms */
  /* For now, forget this.  The searching will be vague enough that nearest day is good enough. */

  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = 0;
  return mktime(&tm);

tough_cheese:
  return (time_t) -1; /* default value */
}
/*}}}*/

static void scan_status_flags(const char *s, struct headers *hdrs)/*{{{*/
{
  const char *p;
  for (p=s; *p; p++) {
    switch (*p) {
      case 'R': hdrs->flags.seen = 1; break;
      case 'A': hdrs->flags.replied = 1; break;
      case 'F': hdrs->flags.flagged = 1; break;
      default: break;
    }
  }
}
/*}}}*/

/*{{{ data_to_rfc822() */
struct rfc822 *data_to_rfc822(struct msg_src *src,
    char *data, int length,
    enum data_to_rfc822_error *error)
{
  struct rfc822 *result;
  char *body_start;
  struct line header;
  struct line *x, *nx;
  struct nvp *ct_nvp, *cte_nvp, *cd_nvp, *nvp;
  int body_len;

  if (error) *error = DTR8_OK; /* default */
  result = new(struct rfc822);
  init_headers(&result->hdrs);
  result->atts.next = result->atts.prev = &result->atts;

  if (split_and_splice_header(src, data, &header, &body_start) < 0) {
    if (verbose) {
      fprintf(stderr, "Giving up on message %s with bad header\n",
          format_msg_src(src));
    }
    if (error) *error = DTR8_BAD_HEADERS;
    return NULL;
  }

  /* Extract key headers {{{*/
  ct_nvp = cte_nvp = cd_nvp = NULL;
  for (x=header.next; x!=&header; x=x->next) {
    if      (match_string("to:", x->text))
      copy_or_concat_header_value(&result->hdrs.to, x->text);
    else if (match_string("cc:", x->text))
      copy_or_concat_header_value(&result->hdrs.cc, x->text);
    else if (!result->hdrs.from && match_string("from:", x->text))
      result->hdrs.from = copy_header_value(x->text);
    else if (!result->hdrs.subject && match_string("subject:", x->text))
      result->hdrs.subject = copy_header_value(x->text);
    else if (!ct_nvp && (nvp = make_nvp(src, x->text, "content-type:")))
      ct_nvp = nvp;
    else if (!cte_nvp && (nvp = make_nvp(src, x->text, "content-transfer-encoding:")))
      cte_nvp = nvp;
    else if (!cd_nvp && (nvp = make_nvp(src, x->text, "content-disposition:")))
      cd_nvp = nvp;
    else if (!result->hdrs.date && match_string("date:", x->text)) {
      char *date_string = copy_header_value(x->text);
      result->hdrs.date = parse_rfc822_date(date_string);
      free(date_string);
    } else if (!result->hdrs.message_id && match_string("message-id:", x->text))
      result->hdrs.message_id = copy_header_value(x->text);
    else if (!result->hdrs.in_reply_to && match_string("in-reply-to:", x->text))
      result->hdrs.in_reply_to = copy_header_value(x->text);
    else if (!result->hdrs.references && match_string("references:", x->text))
      result->hdrs.references = copy_header_value(x->text);
    else if (match_string("status:", x->text))
      scan_status_flags(x->text + sizeof("status:"), &result->hdrs);
    else if (match_string("x-status:", x->text))
      scan_status_flags(x->text + sizeof("x-status:"), &result->hdrs);
  }
/*}}}*/

  /* Process body */
  body_len = length - (body_start - data);
  do_body(src, body_start, body_len, ct_nvp, cte_nvp, cd_nvp, &result->atts, error);

  /* Free header memory */
  for (x=header.next; x!=&header; x=nx) {
    nx = x->next;
    free(x->text);
    free(x);
  }

  if (ct_nvp) free_nvp(ct_nvp);
  if (cte_nvp) free_nvp(cte_nvp);
  if (cd_nvp) free_nvp(cd_nvp);

  return result;

}
/*}}}*/

#define ALLOC_NONE   1
#define ALLOC_MMAP   2
#define ALLOC_MALLOC 3

int data_alloc_type;

#if USE_GZIP_MBOX || USE_BZIP_MBOX

#define SIZE_STEP (8 * 1024 * 1024)

#define COMPRESSION_NONE 0
#define COMPRESSION_GZIP 1
#define COMPRESSION_BZIP 2

static int get_compression_type(const char *filename) {/*{{{*/
  size_t len = strlen(filename);
  int ptr;

#ifdef USE_GZIP_MBOX
  ptr = len - 3;
  if (len > 3 && strncasecmp(filename + ptr, ".gz", 3) == 0) {
    return COMPRESSION_GZIP;
  }
#endif

#ifdef USE_BZIP_MBOX
  ptr = len - 4;
  if (len > 3 && strncasecmp(filename + ptr, ".bz2", 4) == 0) {
    return COMPRESSION_BZIP;
  }
#endif

  return COMPRESSION_NONE;
}
/*}}}*/

static int is_compressed(const char *filename) {/*{{{*/
  return (get_compression_type(filename) != COMPRESSION_NONE);
}
/*}}}*/

struct zFile {/*{{{*/
  union {
    /* Both gzFile and BZFILE* are defined as void pointers
     * in their respective header files.
     */
#ifdef USE_GZIP_MBOX
    gzFile gzf;
#endif
#ifdef USE_BZIP_MBOX
    BZFILE *bzf;
#endif
    void *zptr;
  } foo;
  int type;
};
/*}}}*/

static struct zFile * xx_zopen(const char *filename, const char *mode) {/*{{{*/
  struct zFile *zf = new(struct zFile);

  zf->type = get_compression_type(filename);
  switch (zf->type) {
#ifdef USE_GZIP_MBOX
    case COMPRESSION_GZIP:
      zf->foo.gzf = gzopen(filename, "rb");
      break;
#endif
#ifdef USE_BZIP_MBOX
    case COMPRESSION_BZIP:
      zf->foo.bzf = BZ2_bzopen(filename, "rb");
      break;
#endif
    default:
      zf->foo.zptr = NULL;
      break;
  }

  if (!zf->foo.zptr) {
    free(zf);
    return 0;
  }

  return zf;
}
/*}}}*/
static void xx_zclose(struct zFile *zf) {/*{{{*/
  switch (zf->type) {
#ifdef USE_GZIP_MBOX
    case COMPRESSION_GZIP:
      gzclose(zf->foo.gzf);
      break;
#endif
#ifdef USE_BZIP_MBOX
    case COMPRESSION_BZIP:
      BZ2_bzclose(zf->foo.bzf);
      break;
#endif
    default:
      zf->foo.zptr = NULL;
      break;
  }
  free(zf);
}
/*}}}*/
static int xx_zread(struct zFile *zf, void *buf, int len) {/*{{{*/
  switch (zf->type) {
#ifdef USE_GZIP_MBOX
    case COMPRESSION_GZIP:
      return gzread(zf->foo.gzf, buf, len);
      break;
#endif
#ifdef USE_BZIP_MBOX
    case COMPRESSION_BZIP:
      return BZ2_bzread(zf->foo.bzf, buf, len);
      break;
#endif
    default:
      return 0;
      break;
  }
}
/*}}}*/
#endif

#if USE_GZIP_MBOX || USE_BZIP_MBOX
/* do we need ROCACHE_SIZE > 1? the code supports any number here */
#define ROCACHE_SIZE 1
struct ro_mapping {
  char *filename;
  unsigned char *map;
  size_t len;
};
static int ro_cache_init = 0;
static struct ro_mapping ro_mapping_cache[ROCACHE_SIZE];

/* find a temp file in the mapping cache.  If nothing is found lasti is
 * set to the next slot to use for insertion.  You have to check that slot
 * to see if it is currently in use
 */
static struct ro_mapping *find_ro_cache(const char *filename, int *lasti)
{
  int i = 0;
  struct ro_mapping *ro = NULL;
  if (lasti)
    *lasti = 0;
  if (!ro_cache_init)
    return NULL;
  for (i = 0 ; i < ROCACHE_SIZE ; i++) {
    ro = ro_mapping_cache + i;
    if (!ro->map) {
      if (lasti)
        *lasti = i;
      return NULL;
    }
    if (strcmp(filename, ro->filename) == 0)
      return ro;
  }
  /* if we're here, the map is full.  They will reuse slot 0 */
  return NULL;
}

/*
 * put a new tempfile into the cache.  It is mmaped as part of this function
 * so you can safely close the file handle after calling this.
 */
static struct ro_mapping *add_ro_cache(const char *filename, int fd, size_t len)
{
  int i = 0;
  struct ro_mapping *ro = NULL;
  if (!ro_cache_init) {
    memset(&ro_mapping_cache, 0, sizeof(ro_mapping_cache));
    ro_cache_init = 1;
  }
  ro = find_ro_cache(filename, &i);
  if (ro) {
    fprintf(stderr, "%s already in ro cache\n", filename);
    return NULL;
  }
  ro = ro_mapping_cache + i;
  if (ro->map) {
    munmap(ro->map, ro->len);
    ro->map = NULL;
    free(ro->filename);
  }
  ro->map = (unsigned char *)mmap(0, len, PROT_READ, MAP_SHARED, fd, 0);
  if (ro->map == MAP_FAILED) {
    ro->map = NULL;
    perror("rfc822:mmap");
    return NULL;
  }
  ro->len = len;
  ro->filename = new_string(filename);
  return ro;
}
#endif /* USE_GZIP_MBOX || USE_BZIP_MBOX */

void create_ro_mapping(const char *filename, unsigned char **data, int *len)/*{{{*/
{
  struct stat sb;
  int fd;

#if USE_GZIP_MBOX || USE_BZIP_MBOX
  struct zFile *zf;
#endif

  if (stat(filename, &sb) < 0)
  {
    report_error("stat", filename);
    *data = NULL;
    return;
  }

#if USE_GZIP_MBOX || USE_BZIP_MBOX
  if(is_compressed(filename)) {
    unsigned char *p;
    size_t cur_read;
    struct ro_mapping *ro;
    FILE *tmpf;

    /* this branch never returns things that are freeable */
    data_alloc_type = ALLOC_NONE;
    ro = find_ro_cache(filename, NULL);
    if (ro) {
      *data = ro->map;
      *len = ro->len;
      return;
    }

    if(verbose) {
      fprintf(stderr, "Decompressing %s...\n", filename);
    }

    tmpf = tmpfile();
    if (!tmpf) {
      perror("tmpfile");
      goto comp_error;
    }
    zf = xx_zopen(filename, "rb");
    if (!zf) {
      fprintf(stderr, "Could not open %s\n", filename);
      goto comp_error;
    }
    p = new_array(unsigned char, SIZE_STEP);
    cur_read = xx_zread(zf, p, SIZE_STEP);
    if (fwrite(p, cur_read, 1, tmpf) != 1) {
      fprintf(stderr, "failed writing to temp file for %s\n", filename);
      goto comp_error;
    }
    *len = cur_read;
    if (cur_read >= SIZE_STEP) {
      while(1) {
        int ret;
        cur_read = xx_zread(zf, p, SIZE_STEP);
        if (cur_read <= 0)
          break;
        *len += cur_read;
        ret = fwrite(p, cur_read, 1, tmpf);
        if (ret != 1) {
          fprintf(stderr, "failed writing to temp file for %s\n", filename);
          goto comp_error;
        }
      }
    }
    free(p);
    xx_zclose(zf);

    if(*len > 0) {
      ro = add_ro_cache(filename, fileno(tmpf), *len);
      if (!ro)
        goto comp_error;
      *data = ro->map;
      *len = ro->len;
    } else {
      *data = NULL;
    }
    fclose(tmpf);
    return;

comp_error:
    *data = NULL;
    *len = 0;
    if (tmpf)
      fclose(tmpf);
    return;
  }
#endif /* USE_GZIP_MBOX || USE_BZIP_MBOX */

  *len = sb.st_size;
  if (*len == 0) {
    *data = NULL;
    return;
  }

  if (!S_ISREG(sb.st_mode)) {
    *data = NULL;
    return;
  }

  fd = open(filename, O_RDONLY);
  if (fd < 0)
  {
    report_error("open", filename);
    *data = NULL;
    return;
  }

  *data = (unsigned char *) mmap(0, *len, PROT_READ, MAP_SHARED, fd, 0);
  if (close(fd) < 0)
    report_error("close", filename);
  if (*data == MAP_FAILED) {
    report_error("rfc822:mmap", filename);
    *data = NULL;
    return;
  }
  data_alloc_type = ALLOC_MMAP;
}
/*}}}*/
void free_ro_mapping(unsigned char *data, int len)/*{{{*/
{
  int r;

  if(data_alloc_type == ALLOC_MALLOC) {
    free(data);
  }

  if(data_alloc_type == ALLOC_MMAP) {
    r = munmap(data, len);
    if(r < 0) {
      fprintf(stderr, "munmap() errord\n");
      exit(1);
    }
  }
}
/*}}}*/

static struct msg_src *setup_msg_src(char *filename)/*{{{*/
{
  static struct msg_src result;
  result.type = MS_FILE;
  result.filename = filename;
  return &result;
}
/*}}}*/
struct rfc822 *make_rfc822(char *filename)/*{{{*/
{
  int len;
  unsigned char *data;
  struct rfc822 *result;

  create_ro_mapping(filename, &data, &len);

  /* Don't process empty files */
  result = NULL;

  if (data)
  {
    struct msg_src *src;
    /* Now process the data */
    src = setup_msg_src(filename);
    /* For one message per file, ignore missing end boundary condition. */
    result = data_to_rfc822(src, (char *) data, len, NULL);

    free_ro_mapping(data, len);
  }

  return result;
}
/*}}}*/
void free_rfc822(struct rfc822 *msg)/*{{{*/
{
  struct attachment *a, *na;

  if (!msg) return;

  if (msg->hdrs.to) free(msg->hdrs.to);
  if (msg->hdrs.cc) free(msg->hdrs.cc);
  if (msg->hdrs.from) free(msg->hdrs.from);
  if (msg->hdrs.subject) free(msg->hdrs.subject);
  if (msg->hdrs.message_id) free(msg->hdrs.message_id);
  if (msg->hdrs.in_reply_to) free(msg->hdrs.in_reply_to);
  if (msg->hdrs.references) free(msg->hdrs.references);

  for (a = msg->atts.next; a != &msg->atts; a = na) {
    na = a->next;
    if (a->filename) free(a->filename);
    if (a->ct == CT_MESSAGE_RFC822) {
      free_rfc822(a->data.rfc822);
    } else {
      free(a->data.normal.bytes);
    }
    free(a);
  }
  free(msg);
}
/*}}}*/

#ifdef TEST

static void do_indent(int indent)/*{{{*/
{
  int i;
  for (i=indent; i>0; i--) {
    putchar(' ');
  }
}
/*}}}*/
static void show_header(char *tag, char *x, int indent)/*{{{*/
{
  if (x) {
    do_indent(indent);
    printf("%s: %s\n", tag, x);
  }
}
/*}}}*/
static void show_rfc822(struct rfc822 *msg, int indent)/*{{{*/
{
  struct attachment *a;
  show_header("From", msg->hdrs.from, indent);
  show_header("To", msg->hdrs.to, indent);
  show_header("Cc", msg->hdrs.cc, indent);
  show_header("Date", msg->hdrs.date, indent);
  show_header("Subject", msg->hdrs.subject, indent);

  for (a = msg->atts.next; a != &msg->atts; a=a->next) {
    printf("========================\n");
    switch (a->ct) {
      case CT_TEXT_PLAIN: printf("Attachment type text/plain\n"); break;
      case CT_TEXT_HTML: printf("Attachment type text/html\n"); break;
      case CT_TEXT_OTHER: printf("Attachment type text/non-plain\n"); break;
      case CT_MESSAGE_RFC822: printf("Attachment type message/rfc822\n"); break;
      case CT_OTHER: printf("Attachment type other\n"); break;
    }
    if (a->ct != CT_MESSAGE_RFC822) {
      printf("%d bytes\n", a->data.normal.len);
    }
    if ((a->ct == CT_TEXT_PLAIN) || (a->ct == CT_TEXT_HTML) || (a->ct == CT_TEXT_OTHER)) {
      printf("----------\n");
      printf("%s\n", a->data.normal.bytes);
    }
    if (a->ct == CT_MESSAGE_RFC822) {
      show_rfc822(a->data.rfc822, indent + 4);
    }
  }
}
/*}}}*/

int main (int argc, char **argv)/*{{{*/
{
  struct rfc822 *msg;

  if (argc < 2) {
    fprintf(stderr, "Need a path\n");
    unlock_and_exit(2);
  }

  msg = make_rfc822(argv[1]);
  show_rfc822(msg, 0);
  free_rfc822(msg);

  /* Print out some stuff */

  return 0;
}
/*}}}*/
#endif /* TEST */
