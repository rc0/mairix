#!/usr/bin/env python3
# Generate the case folding tables in utf8fold.c from the Unicode
# Character Database file CaseFolding.txt.
#
# Usage:
#   generate_utf8fold.py CASEFOLDING.TXT           emit C tables to stdout
#   generate_utf8fold.py --check CASEFOLDING.TXT   verify the tables embedded
#                                                  in ../utf8fold.c instead
#
# Only mappings that mairix can apply are kept: simple (single code point)
# common+simple folds ("C" and "S" status) whose UTF-8 encoding has the same
# byte length as the source code point.  Such folds can be applied in place,
# which keeps the token storage format unchanged.  Everything else (multi
# code point folds such as U+00DF -> "ss", folds that change the encoded
# length, Turkic "T" mappings) is deliberately left untouched, symmetrically
# on the index and the query side.

import re
import sys


def utf8_len(cp):
    if cp < 0x80:
        return 1
    if cp < 0x800:
        return 2
    if cp < 0x10000:
        return 3
    return 4


def load_mappings(path):
    """Return sorted list of (cp_from, cp_to) applicable fold mappings."""
    mappings = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            fields = [x.strip() for x in line.split(";")]
            cp_from = int(fields[0], 16)
            status = fields[1]
            if status not in ("C", "S"):
                continue
            target = [int(x, 16) for x in fields[2].split()]
            if len(target) != 1:
                continue
            cp_to = target[0]
            if utf8_len(cp_from) != utf8_len(cp_to):
                continue
            mappings.append((cp_from, cp_to))
    mappings.sort()
    assert len(mappings) == len(set(cp for cp, _ in mappings))
    return mappings


def compress(mappings):
    """Split mappings into ranges and singleton pairs.

    A range (first, last, delta, step) folds every code point
    first + k*step (k = 0..(last-first)/step) to itself + delta.
    step 1 covers uniform blocks (e.g. Latin-1 capitals), step 2 covers
    alternating upper/lowercase pairs (e.g. Latin Extended-A).
    """
    ranges = []  # (first, last, delta, step)
    singles = []  # (from, to)
    i = 0
    n = len(mappings)
    while i < n:
        best = None
        for step in (1, 2):
            j = i
            delta = mappings[i][1] - mappings[i][0]
            while (j + 1 < n
                   and mappings[j + 1][0] == mappings[j][0] + step
                   and mappings[j + 1][1] - mappings[j + 1][0] == delta):
                j += 1
            if j > i and (best is None or j > best[1]):
                best = (step, j, delta)
        if best is not None:
            step, j, delta = best
            ranges.append((mappings[i][0], mappings[j][0], delta, step))
            i = j + 1
        else:
            singles.append(mappings[i])
            i += 1
    return ranges, singles


def emit(mappings):
    ranges, singles = compress(mappings)
    out = []
    out.append("static const struct fold_range fold_ranges[] = {")
    for first, last, delta, step in ranges:
        out.append("  { 0x%04x, 0x%04x, %d, %d }," % (first, last, delta, step))
    out.append("};")
    out.append("")
    out.append("static const struct fold_pair fold_pairs[] = {")
    for cp_from, cp_to in singles:
        out.append("  { 0x%04x, 0x%04x }," % (cp_from, cp_to))
    out.append("};")
    return "\n".join(out)


def check(path):
    """Verify the tables embedded in utf8fold.c against CaseFolding.txt."""
    here = sys.path[0]
    with open(here + "/../utf8fold.c", encoding="utf-8") as f:
        src = f.read()
    ranges = [(int(a, 16), int(b, 16), int(d), int(s)) for a, b, d, s in
              re.findall(r"\{ 0x([0-9a-f]+), 0x([0-9a-f]+), (-?\d+), (\d+) \},", src)]
    singles = [(int(a, 16), int(b, 16)) for a, b in
               re.findall(r"\{ 0x([0-9a-f]+), 0x([0-9a-f]+) \},", src)]

    def table_fold(cp):
        for first, last, delta, step in ranges:
            if first <= cp <= last and (cp - first) % step == 0:
                return cp + delta
        for cp_from, cp_to in singles:
            if cp_from == cp:
                return cp_to
        return cp

    expected = load_mappings(path)
    errors = 0
    for cp_from, cp_to in expected:
        got = table_fold(cp_from)
        if got != cp_to:
            print("MISMATCH U+%04X: expected U+%04X, table gives U+%04X"
                  % (cp_from, cp_to, got))
            errors += 1
    covered = set(cp for cp, _ in expected)
    for first, last, delta, step in ranges:
        for cp in range(first, last + 1, step):
            if cp not in covered:
                print("SPURIOUS range entry U+%04X" % cp)
                errors += 1
    for cp, _ in singles:
        if cp not in covered:
            print("SPURIOUS pair entry U+%04X" % cp)
            errors += 1
    if errors:
        print("%d error(s)" % errors)
        return 1
    print("OK: tables cover all %d applicable mappings (%d ranges, %d pairs)"
          % (len(expected), len(ranges), len(singles)))
    return 0


def main(argv):
    if argv and argv[0] == "--check":
        return check(argv[1])
    mappings = load_mappings(argv[0])
    print(emit(mappings))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
