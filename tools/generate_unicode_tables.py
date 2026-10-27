#!/usr/bin/env python3
"""Generate the Unicode tables used by utf8.c.

Regenerating the tables needs nothing but the Python 3 standard library:
the mappings are derived from unicodedata (the interpreter's bundled copy
of the Unicode Character Database), so no network access is required.

Simple case folding (CaseFolding.txt, common + simple mappings):

  * str.casefold() applies the C+F mappings.  When it yields a single
    character that is the C or S mapping and we take it.
  * When casefold() expands to several characters, a single-character
    simple mapping may still exist (status 'S' entries: U+1E9E and the
    Greek capitals with prosgegrammeni).  str.lower() exposes those
    mappings, so they are added as a second pass.
  * Entries whose fold expands to more than one character (U+00DF SHARP
    S to "ss", U+0130, ligatures, ...) have no simple mapping and are
    left unchanged, exactly as Unicode simple case folding prescribes.

Word characters for tokenisation:

  * A decoded multibyte character is a word character unless it belongs
    to a punctuation (P*), separator (Z*), symbol (S*) or control/format
    (Cc, Cf) general category.  Letters, marks and numbers are word
    characters, as are unassigned and private-use code points: the
    historical behaviour of mairix was to keep every byte >= 0x80 in
    tokens, which keeps non-UTF-8 8-bit text searchable, so the code
    only carves out sequences it positively knows cannot be part of a
    word.

Usage: python3 tools/generate_unicode_tables.py
Prints the contents of the two tables to stdout for pasting into utf8.c.
"""

import sys
import unicodedata

UCD = unicodedata.unidata_version


def build_fold_map():
    fold = {}
    for cp in range(0x110000):
        ch = chr(cp)
        cf = ch.casefold()
        if len(cf) == 1 and cf != ch:
            fold[cp] = ord(cf)
        elif len(cf) > 1:
            lo = ch.lower()
            if len(lo) == 1 and ord(lo) != cp:
                # 'S' mapping that differs from the 'F' mapping.
                fold[cp] = ord(lo)
    return fold


def compress_folds(fold):
    """Merge mappings into (first, last, delta, step) runs.

    A run covers every code point first+i*step, i = 0..n, that all map by
    the same delta.  Leftover singletons are returned as (from, to) pairs.
    Runs and pairs are each sorted; all runs precede all pairs.
    """
    srcs = sorted(fold)
    ranges = []
    pairs = []
    i = 0
    n = len(srcs)
    while i < n:
        s = srcs[i]
        d = fold[s] - s
        j = i
        while (j + 1 < n and srcs[j + 1] == srcs[j] + 1
               and fold[srcs[j + 1]] - srcs[j + 1] == d):
            j += 1
        if j - i >= 1:
            ranges.append((s, srcs[j], d, 1))
            i = j + 1
            continue
        j = i
        while (j + 1 < n and srcs[j + 1] == srcs[j] + 2
               and fold[srcs[j + 1]] - srcs[j + 1] == d):
            j += 1
        if j - i >= 1:
            ranges.append((s, srcs[j], d, 2))
            i = j + 1
            continue
        pairs.append((s, fold[s]))
        i += 1
    return ranges, pairs


def build_delim_ranges():
    """Contiguous ranges of code points >= 0x80 that are not word chars."""
    def is_delim(cp):
        cat = unicodedata.category(chr(cp))
        return cat[0] in 'PZS' or cat in ('Cc', 'Cf')

    ranges = []
    start = None
    for cp in range(0x80, 0x110000):
        if is_delim(cp):
            if start is None:
                start = cp
        elif start is not None:
            ranges.append((start, cp - 1))
            start = None
    if start is not None:
        ranges.append((start, 0x10FFFF))
    return ranges


def emit():
    fold = build_fold_map()
    ranges, pairs = compress_folds(fold)
    delims = build_delim_ranges()

    print("/* UCD %s - %d case fold mappings + %d non-word ranges */" %
          (UCD, len(fold), len(delims)))
    print("static const struct fold_range fold_ranges[] = {")
    for first, last, delta, step in ranges:
        print("  { 0x%05x, 0x%05x, %d, %d }," % (first, last, delta, step))
    print("};")
    print()
    print("static const struct fold_pair fold_pairs[] = {")
    for i in range(0, len(pairs), 4):
        chunk = ", ".join("{ 0x%05x, 0x%05x }" % p for p in pairs[i:i+4])
        print("  %s," % chunk)
    print("};")
    print()
    print("static const struct cp_range delim_ranges[] = {")
    for i in range(0, len(delims), 3):
        chunk = ", ".join("{ 0x%05x, 0x%05x }" % r for r in delims[i:i+3])
        print("  %s," % chunk)
    print("};")


if __name__ == '__main__':
    emit()
