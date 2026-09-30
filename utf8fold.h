#ifndef UTF8FOLD_H
#define UTF8FOLD_H

/* Fold a NUL-terminated UTF-8 string to its caseless form, in place.
   Applies Unicode simple case folding where the folded encoding is the
   same byte length; everything else (including invalid UTF-8 bytes) is
   left unchanged. */
void fold_utf8_inplace(char *str);

#endif
