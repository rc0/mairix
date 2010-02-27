/*
  mairix - message index builder and finder for maildir folders.

 **********************************************************************
 * Copyright (C) Richard P. Curnow  2002-2004,2006
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>
#include "mairix.h"
#include "dates.h"
#include "datescan.h"

static enum DATESCAN_TYPE discover_type(char *first, char *last)/*{{{*/
{
  int current_state = 0;
  int token;
  char *p;
  p = first;
  while (p < last) {
    token = datescan_char2tok[(int)*(unsigned char*)p];
    current_state = datescan_next_state(current_state, token);
    if (current_state < 0) break;
    p++;
  }

  if (current_state < 0) {
    return DS_FAILURE;
  } else {
    return datescan_attr[current_state];
  }
}
/*}}}*/
static int match_month(char *p)/*{{{*/
{
  if (!strncasecmp(p, "jan", 3)) return 1;
  if (!strncasecmp(p, "feb", 3)) return 2;
  if (!strncasecmp(p, "mar", 3)) return 3;
  if (!strncasecmp(p, "apr", 3)) return 4;
  if (!strncasecmp(p, "may", 3)) return 5;
  if (!strncasecmp(p, "jun", 3)) return 6;
  if (!strncasecmp(p, "jul", 3)) return 7;
  if (!strncasecmp(p, "aug", 3)) return 8;
  if (!strncasecmp(p, "sep", 3)) return 9;
  if (!strncasecmp(p, "oct", 3)) return 10;
  if (!strncasecmp(p, "nov", 3)) return 11;
  if (!strncasecmp(p, "dec", 3)) return 12;
  return 0;
}
/*}}}*/
static int year_fix(int y)/*{{{*/
{
  if (y>100) {
    return y-1900;
  } else if (y < 70) {
    /* 2000-2069 */
    return y+100;
  } else {
    /* 1970-1999 */
    return y;
  }
}
/*}}}*/
static int last_day(int mon, int y) {/*{{{*/
  /* mon in [0,11], y=year-1900 */

  static unsigned char days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (mon != 1) {
    return days[mon];
  } else {
    /* Because 2000 was a leap year, we don't have to bother about the %100
     * rule, at least not in this range of dates. */
    if ((y % 4) == 0) {
      return 29;
    } else {
      return 28;
    }
  }
}
/*}}}*/
static void set_day(struct tm *x, int y)/*{{{*/
{
  if (y > x->tm_mday) {
    /* Shorthand for that day in previous month */
    if (x->tm_mon == 0) {
      x->tm_mon = 11;
      --x->tm_year;
    } else {
      --x->tm_mon;
    }
  }
  x->tm_mday = y; /* Always */
}
/*}}}*/
static int is_later_dm(struct tm *x, int m, int d)/*{{{*/
{
  int m1 = m-1;
  return ((x->tm_mon < m1) || ((x->tm_mon == m1) && (x->tm_mday < d)));
}
/*}}}*/
static int scan_date_expr(char *first, char *last, struct tm *start, struct tm *end)/*{{{*/
{
  enum DATESCAN_TYPE type;
  time_t now;

  time(&now);
  type = discover_type(first, last);

  if (type == DS_SCALED) {/*{{{*/
    int v;
    char *p;
    time_t then;

    p = first;
    v = 0;
    while (isdigit(*p)) {
      v = (v*10) + (*p - '0');
      p++;
    }
    switch(*p) {
      case 'd': v *= 86400; break;
      case 'w': v *= 7*86400; break;
      case 'm': v *= 30*86400; break;
      case 'y': v *= 365*86400; break;
      default:
        fprintf(stderr, "Unrecognized relative date scaling '%c'\n", *p);
        return -1;
    }
    then = now - v;
    if (start) {
      *start = *localtime(&then);
    }
    if (end) {
      *end = *localtime(&then);
    }/*}}}*/
  } else if (type == DS_FAILURE) {
    fputs("Cannot parse date expression [", stderr);
    fwrite(first, sizeof(char), last-first, stderr);
    fputs("]\n", stderr);
    return -1;
  } else {
    /* something else */
    int v1, v3;
    int m2;   /* decoded month */
    char *p;

    v1 = v3 = m2 = 0;
    p = first;
    while (p < last && isdigit(*p)) {
      v1 = (v1*10) + (*p - '0');
      p++;
    }
    if (p < last) {
      m2 = match_month(p);
      p += 3;
      if (m2 == 0) {
        return -1; /* failure */
      }

    }
    while (p < last && isdigit(*p)) {
      v3 = (v3*10) + (*p - '0');
      p++;
    }
    assert(p==last); /* should be true in all cases. */

    switch (type) {
      case DS_D:/*{{{*/
        if (start) set_day(start, v1);
        if (end) set_day(end, v1);
        break;
/*}}}*/
      case DS_Y:/*{{{*/
        if (start) {
          start->tm_mday = 1;
          start->tm_mon  = 0; /* january */
          start->tm_year = year_fix(v1);
        }
        if (end) {
          end->tm_mday = 31;
          end->tm_mon  = 11;
          end->tm_year = year_fix(v1);
        }
        break;
/*}}}*/
      case DS_YYMMDD:/*{{{*/
        if (start) {
          start->tm_mday = v1 % 100;
          start->tm_mon  = ((v1 / 100) % 100) - 1;
          start->tm_year = year_fix(v1/10000);
        }
        if (end) {
          end->tm_mday = v1 % 100;
          end->tm_mon  = ((v1 / 100) % 100) - 1;
          end->tm_year = year_fix(v1/10000);
        }
        break;
/*}}}*/
      case DS_M:/*{{{*/
        if (start) {
          if (m2-1 > start->tm_mon) --start->tm_year; /* shorthand for previous year */
          start->tm_mon = m2-1;
          start->tm_mday = 1;
        }
        if (end) {
          if (m2-1 > end->tm_mon) --end->tm_year; /* shorthand for previous year */
          end->tm_mon = m2-1;
          end->tm_mday = last_day(m2-1, end->tm_year);
        }
        break;
/*}}}*/
      case DS_DM:/*{{{*/
        if (start) {
          if (is_later_dm(start, m2, v1)) --start->tm_year; /* shorthand for previous year. */
          start->tm_mon = m2-1;
          start->tm_mday = v1;
        }
        if (end) {
          if (is_later_dm(end, m2, v1)) --end->tm_year; /* shorthand for previous year. */
          end->tm_mon = m2-1;
          end->tm_mday = v1;
        }
        break;
/*}}}*/
      case DS_MD:/*{{{*/
        if (start) {
          if (is_later_dm(start, m2, v3)) --start->tm_year; /* shorthand for previous year. */
          start->tm_mon = m2-1;
          start->tm_mday = v3;
        }
        if (end) {
          if (is_later_dm(end, m2, v3)) --end->tm_year; /* shorthand for previous year. */
          end->tm_mon = m2-1;
          end->tm_mday = v3;
        }
        break;
/*}}}*/
      case DS_DMY:/*{{{*/
        if (start) {
          start->tm_mon = m2-1;
          start->tm_mday = v1;
          start->tm_year = year_fix(v3);
        }
        if (end) {
          end->tm_mon = m2-1;
          end->tm_mday = v1;
          end->tm_year = year_fix(v3);
        }
        break;
/*}}}*/
      case DS_YMD:/*{{{*/
        if (start) {
          start->tm_mon = m2-1;
          start->tm_mday = v3;
          start->tm_year = year_fix(v1);
        }
        if (end) {
          end->tm_mon = m2-1;
          end->tm_mday = v3;
          end->tm_year = year_fix(v1);
        }
        break;
/*}}}*/
      case DS_MY:/*{{{*/
        if (start) {
          start->tm_year = year_fix(v3);
          start->tm_mon  = m2 - 1;
          start->tm_mday = 1;
        }
        if (end) {
          end->tm_year = year_fix(v3);
          end->tm_mon  = m2 - 1;
          end->tm_mday = last_day(end->tm_mon, end->tm_year);
        }
        break;
/*}}}*/
      case DS_YM:/*{{{*/
        if (start) {
          start->tm_year = year_fix(v1);
          start->tm_mon  = m2 - 1;
          start->tm_mday = 1;
        }
        if (end) {
          end->tm_year = year_fix(v1);
          end->tm_mon  = m2 - 1;
          end->tm_mday = last_day(end->tm_mon, end->tm_year);
        }
        break;/*}}}*/
      case DS_FAILURE:
        return -1;
        break;

      case DS_SCALED:
        assert(0);
        break;

    }
  }
  return 0;
}
/*}}}*/

int scan_date_string(char *in, time_t *start, int *has_start, time_t *end, int *has_end)/*{{{*/
{
  char *hyphen;
  time_t now;
  struct tm start_tm, end_tm;
  char *nullchar;
  int status;

  *has_start = *has_end = 0;

  nullchar = in;
  while (*nullchar) nullchar++;

  time(&now);
  start_tm = end_tm = *localtime(&now);
  start_tm.tm_hour = 0;
  start_tm.tm_min = 0;
  start_tm.tm_sec = 0;
  end_tm.tm_hour = 23;
  end_tm.tm_min = 59;
  end_tm.tm_sec = 59;

  hyphen = strchr(in, '-');
  if (!hyphen) {
    /* Start and end are the same. */
    *has_start = *has_end = 1;
    status = scan_date_expr(in, nullchar, &start_tm, &end_tm);
    if (status) return status;
    *start = mktime(&start_tm);
    *end = mktime(&end_tm);
    return 0;
  } else {
    if (hyphen+1 < nullchar) {
      *has_end = 1;
      status = scan_date_expr(hyphen+1, nullchar, NULL, &end_tm);
      if (status) return status;
      *end = mktime(&end_tm);
      start_tm = end_tm;
    }
    if (hyphen > in) {
      *has_start = 1;
      status = scan_date_expr(in, hyphen, &start_tm, NULL);
      if (status) return status;
      *start = mktime(&start_tm);
    }
  }
  return 0;
}
/*}}}*/

#ifdef TEST
static void check(char *in)/*{{{*/
{
  struct tm start, end;
  int result;
  result = scan_date_string(in, &start, &end);
  if (result) printf("Conversion for <%s> failed\n", in);
  else {
    char buf1[128], buf2[128];
    strftime(buf1, 128, "%d-%b-%Y", &start);
    strftime(buf2, 128, "%d-%b-%Y", &end);
    printf("Computed range for <%s> : %s - %s\n", in, buf1, buf2);
  }

}
/*}}}*/
int main (int argc, char **argv)/*{{{*/
{

  check("2w-1w");
  check("4m-1w");
  check("2002-2003");
  check("may2002-2003");
  check("2002may-2003");
  check("feb98-15may99");
  check("feb98-15may1999");
  check("2feb98-1y");
  check("02feb98-1y");
  check("970617-20010618");

  return 0;
}
/*}}}*/
#endif
