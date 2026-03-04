/* 変換結果のデータベース */
#ifndef _convdb_h_included_
#define _convdb_h_included_

#include <anthy/anthy.h>

/* 不明, OK, 誤変換, don't careの4つのカテゴリーに分類する */
#define CHK_UNKNOWN 0
#define CHK_OK 1
#define CHK_MISS 2
#define CHK_DONTCARE 3

/**/
#define CONV_OK 0
#define CONV_SIZE_MISS 1
#define CONV_CAND_MISS 2
#define CONV_INVALID 4

/* 変換前と変換後の文字列、結果に対する判定を格納する */
struct conv_res {
  /* 検索のキー */
  char *src_str;
  char *res_str;
  /* 候補を割り当てたもの */
  char *cand_str;
  /**/
  int *cand_check;
  /**/
  int check;
  int used;
  /**/
  struct conv_res *next;
};

/* 変換結果のカウント */
struct res_stat {
  int unknown;
  int ok;
  int miss;
  int dontcare;
};

/* 変換結果のデータベース */
struct res_db {
  /**/
  struct conv_res res_list;
  struct conv_res *tail;
  /**/
  int total;
  struct res_stat res, split;
};

struct res_db *create_db(void);
void read_db(struct res_db *db, const char *fn);
struct conv_res *find_conv_res(struct res_db *db, anthy_context_t ac,
			       const char *src, int conv);
void print_size_miss_segment_info(anthy_context_t ac, int nth);
void print_cand_miss_segment_info(anthy_context_t ac, int nth);
void print_context_info(anthy_context_t ac, struct conv_res *cr);

#endif
