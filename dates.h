#ifndef DATES_H
#define DATES_H

enum DATESCAN_TYPE {
  DS_FAILURE,
  DS_D,
  DS_Y,
  DS_YYMMDD,
  DS_SCALED,
  DS_M,
  DS_DM,
  DS_MD,
  DS_YM,
  DS_MY,
  DS_YMD,
  DS_DMY,
};

extern int datescan_next_state(int current_state, int next_token);
extern enum DATESCAN_TYPE datescan_exitval[];
  

#endif /* DATES_H */
