

#ifndef _FROMCHECK_H
#define _FROMCHECK_H

enum fromcheck_result {
  FROMCHECK_PASS,
  FROMCHECK_FAIL
};

extern int fromcheck_next_state(int, int);
extern enum fromcheck_result fromcheck_exitval[];

/* Tokens, keep in the same sequence as the list in the fromcheck.nfa file */
#define FS_CR 0
#define FS_DIGIT 1
#define FS_AT 2
#define FS_COLON 3
#define FS_WHITE 4
#define FS_LOWER 5
#define FS_UPPER 6
#define FS_PLUSMINUS 7
#define FS_OTHEREMAIL 8

#endif
