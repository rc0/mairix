#ifndef UTF8FOLD_H
#define UTF8FOLD_H

/* Fold a NUL-terminated UTF-8 string in place. Invalid byte sequences are
 * preserved. Only one-code-point folds with the same UTF-8 byte length are
 * applied, so the caller's allocation and string length never change. */
void utf8_casefold_inplace(char *text);

#endif
