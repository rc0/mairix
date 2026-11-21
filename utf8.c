/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004, 2005
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

/* Locale-independent UTF-8 aware case folding.
 *
 * The mappings here are deterministic on every system; no use is made of
 * setlocale() or the wide-character libc functions. */

#include "mairix.h"

static int decode_utf8(const unsigned char *s, unsigned long *cp)/*{{{*/
{
  /* Decode one UTF-8 sequence.  Returns the number of bytes it occupies.
   * For an invalid (including overlong) sequence, returns 1 and sets *cp
   * to the raw first byte so it passes through unchanged.  Continuation
   * bytes are checked before use, so the terminating NUL is never read
   * past. */
  unsigned char c = s[0];
  if (c < 0x80) {
    *cp = c;
    return 1;
  } else if ((c & 0xe0) == 0xc0) {
    if ((c >= 0xc2) && ((s[1] & 0xc0) == 0x80)) {
      *cp = ((c & 0x1f) << 6) | (s[1] & 0x3f);
      return 2;
    }
  } else if ((c & 0xf0) == 0xe0) {
    if (((s[1] & 0xc0) == 0x80) && ((s[2] & 0xc0) == 0x80)) {
      unsigned long v = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
      if (v >= 0x800) {
        *cp = v;
        return 3;
      }
    }
  } else if ((c & 0xf8) == 0xf0) {
    if (((s[1] & 0xc0) == 0x80) && ((s[2] & 0xc0) == 0x80) && ((s[3] & 0xc0) == 0x80)) {
      unsigned long v = ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) | ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
      if (v >= 0x10000) {
        *cp = v;
        return 4;
      }
    }
  }
  *cp = c;
  return 1;
}
/*}}}*/
static int encode_utf8(unsigned char *s, unsigned long cp)/*{{{*/
{
  /* Re-encode a (possibly case-folded) codepoint.  Returns its length. */
  if (cp < 0x80) {
    s[0] = cp;
    return 1;
  } else if (cp < 0x800) {
    s[0] = 0xc0 | (cp >> 6);
    s[1] = 0x80 | (cp & 0x3f);
    return 2;
  } else if (cp < 0x10000) {
    s[0] = 0xe0 | (cp >> 12);
    s[1] = 0x80 | ((cp >> 6) & 0x3f);
    s[2] = 0x80 | (cp & 0x3f);
    return 3;
  } else {
    s[0] = 0xf0 | (cp >> 18);
    s[1] = 0x80 | ((cp >> 12) & 0x3f);
    s[2] = 0x80 | ((cp >> 6) & 0x3f);
    s[3] = 0x80 | (cp & 0x3f);
    return 4;
  }
}
/*}}}*/
static unsigned long utf8_cp_tolower(unsigned long cp)/*{{{*/
{
  if ((cp >= 'A') && (cp <= 'Z')) return cp + ('a' - 'A');

  /* Latin-1 Supplement : U+00C0..U+00DE -> U+00E0..U+00FE, except
   * the multiplication sign U+00D7 which has no case. */
  if ((cp >= 0xc0) && (cp <= 0xde) && (cp != 0xd7)) return cp + 0x20;

  /* Latin Extended-A : U+0100..U+017F.  The case pairs mostly alternate
   * upper (even) / lower (odd), with a flipped stretch in U+0139..U+0148
   * and a few singletons. */
  if ((cp >= 0x100) && (cp <= 0x137)) {
    if (!(cp & 1) && (cp != 0x130)) return cp + 1;
  } else if ((cp >= 0x139) && (cp <= 0x148)) {
    if (cp & 1) return cp + 1;
  } else if (cp == 0x14a) {
    return cp + 1;
  } else if ((cp >= 0x14c) && (cp <= 0x177)) {
    if (!(cp & 1)) return cp + 1;
  } else if (cp == 0x178) {
    return 0xff; /* LATIN CAPITAL LETTER Y WITH DIAERESIS -> U+00FF */
  } else if ((cp >= 0x179) && (cp <= 0x17e)) {
    if (cp & 1) return cp + 1;
  } else if (cp == 0x17f) {
    return 's'; /* LATIN SMALL LETTER LONG S */
  }

  return cp;
}
/*}}}*/
void utf8_tolower(char *s)/*{{{*/
{
  /* Case-fold a string in place.  Folding never grows a codepoint's
   * encoding, so the output can overwrite the input as we scan it. */
  unsigned char *in = (unsigned char *) s;
  unsigned char *out = (unsigned char *) s;
  while (*in) {
    unsigned long cp;
    int in_len = decode_utf8(in, &cp);
    if (in_len == 1) {
      /* ASCII or an invalid byte : only ASCII letters fold, the rest
       * pass through unchanged. */
      cp = *in++;
      if ((cp >= 'A') && (cp <= 'Z')) cp += ('a' - 'A');
      *out++ = cp;
    } else {
      out += encode_utf8(out, utf8_cp_tolower(cp));
      in += in_len;
    }
  }
  *out = 0;
}
/*}}}*/
