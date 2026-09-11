#ifndef UTF8FOLD_H
#define UTF8FOLD_H

/* Unicode simple case folding in-place for UTF-8 encoded text.
 * Preserves byte length for every folded character so that memory is never reallocated.
 */
void fold_utf8_inplace(char *str);

/* Returns 1 if the Unicode codepoint cp is a delimiter/punctuation/whitespace character
 * (such as quotes, em-dashes, ellipsis, bullets, or non-breaking spaces), 0 if word char.
 */
int utf8_is_delimiter(unsigned int cp);

#endif
