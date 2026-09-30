/* Case folding of UTF-8 encoded text.

   Applies Unicode simple case folding (CaseFolding.txt, "C" and "S"
   mappings) to a NUL-terminated string, in place.  Only mappings to a
   single code point whose UTF-8 encoding is the same length as the source
   are applied, so the string never grows and no reallocation is needed.
   Bytes that are not valid UTF-8, and characters whose fold would change
   the encoded length (e.g. U+00DF LATIN SMALL LETTER SHARP S), pass
   through unchanged.

   The tables below are generated from the Unicode Character Database by
   tools/generate_utf8fold.py; do not edit them by hand.  Run
   "python3 tools/generate_utf8fold.py --check <CaseFolding.txt>" to verify
   them against the UCD. */

struct fold_range {
  unsigned int first, last;
  int delta;
  unsigned int step;
};

struct fold_pair {
  unsigned int from, to;
};

static const struct fold_range fold_ranges[] = {
  { 0x0041, 0x005a, 32, 1 },
  { 0x00c0, 0x00d6, 32, 1 },
  { 0x00d8, 0x00de, 32, 1 },
  { 0x0100, 0x012e, 1, 2 },
  { 0x0132, 0x0136, 1, 2 },
  { 0x0139, 0x0147, 1, 2 },
  { 0x014a, 0x0176, 1, 2 },
  { 0x0179, 0x017d, 1, 2 },
  { 0x0182, 0x0184, 1, 2 },
  { 0x0189, 0x018a, 205, 1 },
  { 0x01a0, 0x01a4, 1, 2 },
  { 0x01b1, 0x01b2, 217, 1 },
  { 0x01b3, 0x01b5, 1, 2 },
  { 0x01cb, 0x01db, 1, 2 },
  { 0x01de, 0x01ee, 1, 2 },
  { 0x01f2, 0x01f4, 1, 2 },
  { 0x01f8, 0x021e, 1, 2 },
  { 0x0222, 0x0232, 1, 2 },
  { 0x0246, 0x024e, 1, 2 },
  { 0x0370, 0x0372, 1, 2 },
  { 0x0388, 0x038a, 37, 1 },
  { 0x038e, 0x038f, 63, 1 },
  { 0x0391, 0x03a1, 32, 1 },
  { 0x03a3, 0x03ab, 32, 1 },
  { 0x03d8, 0x03ee, 1, 2 },
  { 0x03fd, 0x03ff, -130, 1 },
  { 0x0400, 0x040f, 80, 1 },
  { 0x0410, 0x042f, 32, 1 },
  { 0x0460, 0x0480, 1, 2 },
  { 0x048a, 0x04be, 1, 2 },
  { 0x04c1, 0x04cd, 1, 2 },
  { 0x04d0, 0x052e, 1, 2 },
  { 0x0531, 0x0556, 48, 1 },
  { 0x10a0, 0x10c5, 7264, 1 },
  { 0x13f8, 0x13fd, -8, 1 },
  { 0x1c90, 0x1cba, -3008, 1 },
  { 0x1cbd, 0x1cbf, -3008, 1 },
  { 0x1e00, 0x1e94, 1, 2 },
  { 0x1ea0, 0x1efe, 1, 2 },
  { 0x1f08, 0x1f0f, -8, 1 },
  { 0x1f18, 0x1f1d, -8, 1 },
  { 0x1f28, 0x1f2f, -8, 1 },
  { 0x1f38, 0x1f3f, -8, 1 },
  { 0x1f48, 0x1f4d, -8, 1 },
  { 0x1f59, 0x1f5f, -8, 2 },
  { 0x1f68, 0x1f6f, -8, 1 },
  { 0x1f88, 0x1f8f, -8, 1 },
  { 0x1f98, 0x1f9f, -8, 1 },
  { 0x1fa8, 0x1faf, -8, 1 },
  { 0x1fb8, 0x1fb9, -8, 1 },
  { 0x1fba, 0x1fbb, -74, 1 },
  { 0x1fc8, 0x1fcb, -86, 1 },
  { 0x1fd8, 0x1fd9, -8, 1 },
  { 0x1fda, 0x1fdb, -100, 1 },
  { 0x1fe8, 0x1fe9, -8, 1 },
  { 0x1fea, 0x1feb, -112, 1 },
  { 0x1ff8, 0x1ff9, -128, 1 },
  { 0x1ffa, 0x1ffb, -126, 1 },
  { 0x2160, 0x216f, 16, 1 },
  { 0x24b6, 0x24cf, 26, 1 },
  { 0x2c00, 0x2c2f, 48, 1 },
  { 0x2c67, 0x2c6b, 1, 2 },
  { 0x2c80, 0x2ce2, 1, 2 },
  { 0x2ceb, 0x2ced, 1, 2 },
  { 0xa640, 0xa66c, 1, 2 },
  { 0xa680, 0xa69a, 1, 2 },
  { 0xa722, 0xa72e, 1, 2 },
  { 0xa732, 0xa76e, 1, 2 },
  { 0xa779, 0xa77b, 1, 2 },
  { 0xa77e, 0xa786, 1, 2 },
  { 0xa790, 0xa792, 1, 2 },
  { 0xa796, 0xa7a8, 1, 2 },
  { 0xa7b4, 0xa7c2, 1, 2 },
  { 0xa7c7, 0xa7c9, 1, 2 },
  { 0xa7d6, 0xa7d8, 1, 2 },
  { 0xab70, 0xabbf, -38864, 1 },
  { 0xff21, 0xff3a, 32, 1 },
  { 0x10400, 0x10427, 40, 1 },
  { 0x104b0, 0x104d3, 40, 1 },
  { 0x10570, 0x1057a, 39, 1 },
  { 0x1057c, 0x1058a, 39, 1 },
  { 0x1058c, 0x10592, 39, 1 },
  { 0x10594, 0x10595, 39, 1 },
  { 0x10c80, 0x10cb2, 64, 1 },
  { 0x118a0, 0x118bf, 32, 1 },
  { 0x16e40, 0x16e5f, 32, 1 },
  { 0x1e900, 0x1e921, 34, 1 },
};

static const struct fold_pair fold_pairs[] = {
  { 0x00b5, 0x03bc },
  { 0x0178, 0x00ff },
  { 0x0181, 0x0253 },
  { 0x0186, 0x0254 },
  { 0x0187, 0x0188 },
  { 0x018b, 0x018c },
  { 0x018e, 0x01dd },
  { 0x018f, 0x0259 },
  { 0x0190, 0x025b },
  { 0x0191, 0x0192 },
  { 0x0193, 0x0260 },
  { 0x0194, 0x0263 },
  { 0x0196, 0x0269 },
  { 0x0197, 0x0268 },
  { 0x0198, 0x0199 },
  { 0x019c, 0x026f },
  { 0x019d, 0x0272 },
  { 0x019f, 0x0275 },
  { 0x01a6, 0x0280 },
  { 0x01a7, 0x01a8 },
  { 0x01a9, 0x0283 },
  { 0x01ac, 0x01ad },
  { 0x01ae, 0x0288 },
  { 0x01af, 0x01b0 },
  { 0x01b7, 0x0292 },
  { 0x01b8, 0x01b9 },
  { 0x01bc, 0x01bd },
  { 0x01c4, 0x01c6 },
  { 0x01c5, 0x01c6 },
  { 0x01c7, 0x01c9 },
  { 0x01c8, 0x01c9 },
  { 0x01ca, 0x01cc },
  { 0x01f1, 0x01f3 },
  { 0x01f6, 0x0195 },
  { 0x01f7, 0x01bf },
  { 0x0220, 0x019e },
  { 0x023b, 0x023c },
  { 0x023d, 0x019a },
  { 0x0241, 0x0242 },
  { 0x0243, 0x0180 },
  { 0x0244, 0x0289 },
  { 0x0245, 0x028c },
  { 0x0345, 0x03b9 },
  { 0x0376, 0x0377 },
  { 0x037f, 0x03f3 },
  { 0x0386, 0x03ac },
  { 0x038c, 0x03cc },
  { 0x03c2, 0x03c3 },
  { 0x03cf, 0x03d7 },
  { 0x03d0, 0x03b2 },
  { 0x03d1, 0x03b8 },
  { 0x03d5, 0x03c6 },
  { 0x03d6, 0x03c0 },
  { 0x03f0, 0x03ba },
  { 0x03f1, 0x03c1 },
  { 0x03f4, 0x03b8 },
  { 0x03f5, 0x03b5 },
  { 0x03f7, 0x03f8 },
  { 0x03f9, 0x03f2 },
  { 0x03fa, 0x03fb },
  { 0x04c0, 0x04cf },
  { 0x10c7, 0x2d27 },
  { 0x10cd, 0x2d2d },
  { 0x1c88, 0xa64b },
  { 0x1e9b, 0x1e61 },
  { 0x1fbc, 0x1fb3 },
  { 0x1fcc, 0x1fc3 },
  { 0x1fec, 0x1fe5 },
  { 0x1ffc, 0x1ff3 },
  { 0x2132, 0x214e },
  { 0x2183, 0x2184 },
  { 0x2c60, 0x2c61 },
  { 0x2c63, 0x1d7d },
  { 0x2c72, 0x2c73 },
  { 0x2c75, 0x2c76 },
  { 0x2cf2, 0x2cf3 },
  { 0xa77d, 0x1d79 },
  { 0xa78b, 0xa78c },
  { 0xa7b3, 0xab53 },
  { 0xa7c4, 0xa794 },
  { 0xa7c6, 0x1d8e },
  { 0xa7d0, 0xa7d1 },
  { 0xa7f5, 0xa7f6 },
  { 0xfb05, 0xfb06 },
};

#define N_FOLD_RANGES (sizeof(fold_ranges) / sizeof(fold_ranges[0]))
#define N_FOLD_PAIRS  (sizeof(fold_pairs) / sizeof(fold_pairs[0]))

static unsigned int fold_codepoint(unsigned int cp)/*{{{*/
{
  unsigned int lo, hi;

  lo = 0; hi = N_FOLD_RANGES;
  while (lo < hi) {
    unsigned int mid = lo + (hi - lo) / 2;
    const struct fold_range *r = &fold_ranges[mid];
    if (cp < r->first) {
      hi = mid;
    } else if (cp > r->last) {
      lo = mid + 1;
    } else {
      return ((cp - r->first) % r->step == 0) ? cp + r->delta : cp;
    }
  }

  lo = 0; hi = N_FOLD_PAIRS;
  while (lo < hi) {
    unsigned int mid = lo + (hi - lo) / 2;
    if (cp < fold_pairs[mid].from) {
      hi = mid;
    } else if (cp > fold_pairs[mid].from) {
      lo = mid + 1;
    } else {
      return fold_pairs[mid].to;
    }
  }

  return cp;
}
/*}}}*/

/* Decode the UTF-8 sequence starting at s, which is known to be NUL
   terminated.  On success, store the code point in *cp and return the
   sequence length in bytes.  Return 0 for anything that is not well
   formed UTF-8 (overlong encodings, surrogates, truncated sequences,
   stray continuation bytes, code points beyond U+10FFFF). */
static int decode_utf8(const unsigned char *s, unsigned int *cp)/*{{{*/
{
  unsigned int val;
  int len, i;

  if (s[0] < 0x80) {
    *cp = s[0];
    return 1;
  } else if ((s[0] & 0xe0) == 0xc0) {
    val = s[0] & 0x1f; len = 2;
  } else if ((s[0] & 0xf0) == 0xe0) {
    val = s[0] & 0x0f; len = 3;
  } else if ((s[0] & 0xf8) == 0xf0) {
    val = s[0] & 0x07; len = 4;
  } else {
    return 0;
  }

  for (i = 1; i < len; i++) {
    if ((s[i] & 0xc0) != 0x80) return 0;
    val = (val << 6) | (s[i] & 0x3f);
  }

  /* Reject overlong encodings, UTF-16 surrogates and values that are
     out of range. */
  if ((len == 2 && val < 0x80)
      || (len == 3 && val < 0x800)
      || (len == 4 && val < 0x10000)
      || (val >= 0xd800 && val <= 0xdfff)
      || val > 0x10ffff) {
    return 0;
  }

  *cp = val;
  return len;
}
/*}}}*/

/* Encode cp as UTF-8 at out.  Only called for code points whose encoding
   is known to be the same length as the sequence being replaced. */
static void encode_utf8(unsigned char *out, unsigned int cp)/*{{{*/
{
  if (cp < 0x80) {
    out[0] = cp;
  } else if (cp < 0x800) {
    out[0] = 0xc0 | (cp >> 6);
    out[1] = 0x80 | (cp & 0x3f);
  } else if (cp < 0x10000) {
    out[0] = 0xe0 | (cp >> 12);
    out[1] = 0x80 | ((cp >> 6) & 0x3f);
    out[2] = 0x80 | (cp & 0x3f);
  } else {
    out[0] = 0xf0 | (cp >> 18);
    out[1] = 0x80 | ((cp >> 12) & 0x3f);
    out[2] = 0x80 | ((cp >> 6) & 0x3f);
    out[3] = 0x80 | (cp & 0x3f);
  }
}
/*}}}*/

void fold_utf8_inplace(char *str)/*{{{*/
{
  unsigned char *p = (unsigned char *) str;
  while (*p) {
    unsigned int cp;
    int len = decode_utf8(p, &cp);
    if (len > 0) {
      unsigned int folded = fold_codepoint(cp);
      if (folded != cp) {
        /* Table only contains length-preserving mappings, so this
           overwrite never moves the rest of the string. */
        encode_utf8(p, folded);
      }
      p += len;
    } else {
      /* Not valid UTF-8: leave the byte alone. */
      p++;
    }
  }
}
/*}}}*/
