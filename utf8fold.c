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

/* Unicode simple case folding for UTF-8 strings.
 *
 * Tokens are folded with this both when a message is indexed (tok.c)
 * and when a search word is canonicalised (search.c), so a search for
 * a word matches regardless of the capitalisation used in the message
 * or in the query, for non-ASCII scripts as well as for ASCII.
 *
 * The folding is Unicode *simple* case folding (CaseFolding.txt,
 * statuses C and S): each character maps to at most one character.
 * Full foldings are never applied, so characters whose full folding
 * expands them (U+00DF LATIN SMALL LETTER SHARP S -> "ss", U+0130
 * LATIN CAPITAL LETTER I WITH DOT ABOVE -> "i" + combining dot) keep
 * their exact bytes on both sides and only match themselves.
 *
 * The string is folded in place and its byte length never changes.
 * A simple fold whose UTF-8 encoding would be shorter or longer than
 * the original character's (e.g. U+212A KELVIN SIGN -> "k") is
 * therefore not applied; again, this happens symmetrically on both
 * sides, so such characters still match themselves exactly.
 *
 * Decoding is strict: overlong encodings, UTF-16 surrogate code
 * points and code points above U+10FFFF are rejected.  A byte that
 * does not form part of a valid UTF-8 sequence is passed through
 * unchanged, so arbitrary non-UTF-8 bytes in a message survive
 * folding untouched (and identically on both sides).
 *
 * Note: databases indexed by older versions of mairix hold unfolded
 * non-ASCII tokens, so searches with non-ASCII words will not match
 * messages in them until the database is rebuilt (delete the database
 * file and re-run mairix to re-index).
 */

#include "utf8fold.h"
#include "utf8fold_table.h"

/* Decode the first UTF-8 character in s.  On success, return its
 * length in bytes (1..4) and store its code point in *cp.  Return 0
 * if the bytes at s do not form one strictly valid UTF-8 character;
 * the caller then treats the first byte as opaque.  Never reads past
 * the terminating NUL: a continuation byte is only inspected after
 * the preceding byte has been checked to be a non-NUL lead or
 * continuation byte. */
static int utf8_decode(const unsigned char *s, unsigned long *cp)/*{{{*/
{
  unsigned char c = s[0];

  if (c < 0x80) {
    *cp = c;
    return 1;
  }
  if (c < 0xc2) {
    /* Continuation byte, or 0xc0/0xc1 which only start overlong forms. */
    return 0;
  }
  if (c < 0xe0) {
    if ((s[1] & 0xc0) != 0x80) return 0;
    *cp = ((unsigned long)(c & 0x1f) << 6) | (s[1] & 0x3f);
    return 2;
  }
  if (c < 0xf0) {
    if (c == 0xe0) {
      /* Second byte 0xa0..0xbf: below that the form is overlong. */
      if ((s[1] < 0xa0) || (s[1] > 0xbf)) return 0;
    } else if (c == 0xed) {
      /* Second byte 0x80..0x9f: above that the code point would be
       * a UTF-16 surrogate. */
      if ((s[1] < 0x80) || (s[1] > 0x9f)) return 0;
    } else if ((s[1] & 0xc0) != 0x80) {
      return 0;
    }
    if ((s[2] & 0xc0) != 0x80) return 0;
    *cp = ((unsigned long)(c & 0x0f) << 12)
        | ((unsigned long)(s[1] & 0x3f) << 6)
        | (s[2] & 0x3f);
    return 3;
  }
  if (c < 0xf5) {
    if (c == 0xf0) {
      /* Second byte 0x90..0xbf: below that the form is overlong. */
      if ((s[1] < 0x90) || (s[1] > 0xbf)) return 0;
    } else if (c == 0xf4) {
      /* Second byte 0x80..0x8f: above that the code point would
       * exceed U+10FFFF. */
      if ((s[1] < 0x80) || (s[1] > 0x8f)) return 0;
    } else if ((s[1] & 0xc0) != 0x80) {
      return 0;
    }
    if ((s[2] & 0xc0) != 0x80) return 0;
    if ((s[3] & 0xc0) != 0x80) return 0;
    *cp = ((unsigned long)(c & 0x07) << 18)
        | ((unsigned long)(s[1] & 0x3f) << 12)
        | ((unsigned long)(s[2] & 0x3f) << 6)
        | (s[3] & 0x3f);
    return 4;
  }
  return 0;
}
/*}}}*/

/* Number of bytes in the UTF-8 encoding of a code point. */
static int utf8_cplen(unsigned long cp)/*{{{*/
{
  if (cp < 0x80) return 1;
  if (cp < 0x800) return 2;
  if (cp < 0x10000) return 3;
  return 4;
}
/*}}}*/

/* Write the UTF-8 encoding of cp into s, occupying exactly len bytes.
 * The caller has checked that utf8_cplen(cp) == len. */
static void utf8_encode(unsigned char *s, unsigned long cp, int len)/*{{{*/
{
  switch (len) {
    case 1:
      s[0] = (unsigned char) cp;
      break;
    case 2:
      s[0] = (unsigned char) (0xc0 | (cp >> 6));
      s[1] = (unsigned char) (0x80 | (cp & 0x3f));
      break;
    case 3:
      s[0] = (unsigned char) (0xe0 | (cp >> 12));
      s[1] = (unsigned char) (0x80 | ((cp >> 6) & 0x3f));
      s[2] = (unsigned char) (0x80 | (cp & 0x3f));
      break;
    default: /* 4 */
      s[0] = (unsigned char) (0xf0 | (cp >> 18));
      s[1] = (unsigned char) (0x80 | ((cp >> 12) & 0x3f));
      s[2] = (unsigned char) (0x80 | ((cp >> 6) & 0x3f));
      s[3] = (unsigned char) (0x80 | (cp & 0x3f));
      break;
  }
}
/*}}}*/

/* Return the simple case folding of a code point, or the code point
 * itself if it has no simple folding. */
static unsigned long utf8_fold_codepoint(unsigned long cp)/*{{{*/
{
  int lo, hi;

  /* Binary search over the ranges (sorted by first code point). */
  lo = 0;
  hi = (int)(sizeof(utf8_fold_ranges) / sizeof(utf8_fold_ranges[0])) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const struct utf8_fold_range *r = &utf8_fold_ranges[mid];
    if (cp < r->first) {
      hi = mid - 1;
    } else if (cp > r->last) {
      lo = mid + 1;
    } else if ((cp - r->first) % r->step == 0) {
      return cp + r->delta;
    } else {
      break; /* Between two covered points of a stepped range. */
    }
  }

  /* Binary search over the pairs (sorted by source code point). */
  lo = 0;
  hi = (int)(sizeof(utf8_fold_pairs) / sizeof(utf8_fold_pairs[0])) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const struct utf8_fold_pair *pr = &utf8_fold_pairs[mid];
    if (cp < pr->from) {
      hi = mid - 1;
    } else if (cp > pr->from) {
      lo = mid + 1;
    } else {
      return pr->to;
    }
  }

  return cp;
}
/*}}}*/

void utf8_fold_string(char *str)/*{{{*/
{
  unsigned char *p = (unsigned char *) str;

  while (*p) {
    unsigned long cp, folded;
    int len = utf8_decode(p, &cp);
    if (len == 0) {
      p++; /* Invalid byte: pass it through unchanged. */
      continue;
    }
    folded = utf8_fold_codepoint(cp);
    if ((folded != cp) && (utf8_cplen(folded) == len)) {
      utf8_encode(p, folded, len);
    }
    p += len;
  }
}
/*}}}*/
