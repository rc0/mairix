/* Strict, locale-independent Unicode case folding for UTF-8 strings. */

#include "utf8fold.h"

#include <stddef.h>
#include <stdint.h>

struct fold_range {
  uint32_t first;
  uint32_t last;
  uint32_t step;
  int32_t delta;
};

struct fold_pair {
  uint32_t source;
  uint32_t target;
};

#include "utf8fold_table.inc"

static int continuation(unsigned char byte)
{
  return (byte & 0xc0u) == 0x80u;
}

/* Return the sequence length, or zero for an invalid/truncated sequence. */
static size_t decode_utf8(const unsigned char *text, uint32_t *codepoint)
{
  unsigned char first = text[0];
  unsigned char second;
  unsigned char third;
  unsigned char fourth;

  if (first < 0x80u) {
    *codepoint = first;
    return 1;
  }

  second = text[1];
  if (first >= 0xc2u && first <= 0xdfu) {
    if (!continuation(second)) return 0;
    *codepoint = ((uint32_t)(first & 0x1fu) << 6) |
                 (uint32_t)(second & 0x3fu);
    return 2;
  }

  if (second == 0) return 0;
  if (first >= 0xe0u && first <= 0xefu) {
    if (!continuation(second)) return 0;
    if (first == 0xe0u && second < 0xa0u) return 0;
    if (first == 0xedu && second > 0x9fu) return 0;
    third = text[2];
    if (!continuation(third)) return 0;
    *codepoint = ((uint32_t)(first & 0x0fu) << 12) |
                 ((uint32_t)(second & 0x3fu) << 6) |
                 (uint32_t)(third & 0x3fu);
    return 3;
  }

  if (first >= 0xf0u && first <= 0xf4u) {
    if (!continuation(second)) return 0;
    if (first == 0xf0u && second < 0x90u) return 0;
    if (first == 0xf4u && second > 0x8fu) return 0;
    third = text[2];
    if (!continuation(third)) return 0;
    fourth = text[3];
    if (!continuation(fourth)) return 0;
    *codepoint = ((uint32_t)(first & 0x07u) << 18) |
                 ((uint32_t)(second & 0x3fu) << 12) |
                 ((uint32_t)(third & 0x3fu) << 6) |
                 (uint32_t)(fourth & 0x3fu);
    return 4;
  }

  return 0;
}

static uint32_t fold_codepoint(uint32_t codepoint)
{
  size_t low = 0;
  size_t high = FOLD_RANGE_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    const struct fold_range *range = &fold_ranges[middle];
    if (codepoint < range->first) {
      high = middle;
    } else if (codepoint > range->last) {
      low = middle + 1;
    } else {
      if ((codepoint - range->first) % range->step == 0)
        return (uint32_t)((int32_t)codepoint + range->delta);
      return codepoint;
    }
  }

  low = 0;
  high = FOLD_PAIR_COUNT;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (codepoint < fold_pairs[middle].source) {
      high = middle;
    } else if (codepoint > fold_pairs[middle].source) {
      low = middle + 1;
    } else {
      return fold_pairs[middle].target;
    }
  }
  return codepoint;
}

static void encode_same_width(unsigned char *text, size_t width, uint32_t codepoint)
{
  if (width == 1) {
    text[0] = (unsigned char)codepoint;
  } else if (width == 2) {
    text[0] = (unsigned char)(0xc0u | (codepoint >> 6));
    text[1] = (unsigned char)(0x80u | (codepoint & 0x3fu));
  } else if (width == 3) {
    text[0] = (unsigned char)(0xe0u | (codepoint >> 12));
    text[1] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
    text[2] = (unsigned char)(0x80u | (codepoint & 0x3fu));
  } else {
    text[0] = (unsigned char)(0xf0u | (codepoint >> 18));
    text[1] = (unsigned char)(0x80u | ((codepoint >> 12) & 0x3fu));
    text[2] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
    text[3] = (unsigned char)(0x80u | (codepoint & 0x3fu));
  }
}

void utf8_casefold_inplace(char *text)
{
  unsigned char *cursor = (unsigned char *)text;
  if (!cursor) return;
  while (*cursor) {
    uint32_t codepoint;
    uint32_t folded;
    size_t width = decode_utf8(cursor, &codepoint);
    if (width == 0) {
      ++cursor;
      continue;
    }
    folded = fold_codepoint(codepoint);
    if (folded != codepoint) encode_same_width(cursor, width, folded);
    cursor += width;
  }
}
