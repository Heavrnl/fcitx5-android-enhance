#ifndef __prediction_h_included__
#define __prediction_h_included__

#include "xstr.h"

struct prediction_t {
  int timestamp;
  /* 読み */
  xstr *src_str;
  /* 結果の文字列 */
  xstr *str;
};

/* 予測された文字列を格納する */
int anthy_traverse_record_for_prediction(xstr*, struct prediction_t*);


#endif
