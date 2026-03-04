#ifndef _mkdic_h_included_
#define _mkdic_h_included_

#include <stdio.h>
#include <anthy/xstr.h>

/** 単語 */
struct word_entry {
  /** 品詞名 */
  const char *wt_name;
  /** 頻度 */
  int raw_freq;
  int source_order;
  int freq;
  /* 素性 */
  int feature;
  /** 単語 */
  char *word_utf8;
  /** 辞書ファイル中のオフセット */
  int offset;
  /** 属すyomi_entry*/
  struct yomi_entry* ye;
};

/** ある読み */
struct yomi_entry {
  /* 読みの文字列 */
  xstr *index_xstr;
  /* 読みの文字列(辞書ファイル内のインデックス) */
  char *index_str;
  /* 辞書ファイル中のページ中のオフセット */
  int offset;
  /* 各エントリ */
  int nr_entries;
  struct word_entry *entries;
  /**/
  struct yomi_entry *next;
  struct yomi_entry *hash_next;
};

#define YOMI_HASH (16384 * 16)

/* 辞書全体 */
struct yomi_entry_list {
  /* 見出しのリスト */
  struct yomi_entry *head;
  /* 辞書ファイル中の見出しの数 */
  int nr_entries;
  /* 見出しの中で単語を持つものの数 */
  int nr_valid_entries;
  /* 単語の数 */
  int nr_words;
  /**/
  struct yomi_entry *hash[YOMI_HASH];
  struct yomi_entry **ye_array;
  /**/
  int index_encoding;
  int body_encoding;
};

#define ADJUST_FREQ_UP 1
#define ADJUST_FREQ_DOWN 2
#define ADJUST_FREQ_KILL 3

/* 頻度補正用コマンド */
struct adjust_command {
  int type;
  xstr *yomi;
  const char *wt;
  char *word;
  struct adjust_command *next;
};

/**/
struct yomi_entry *find_yomi_entry(struct yomi_entry_list *yl,
				   xstr *index, int create);

/* 辞書書き出し用の補助 */
void write_nl(FILE *fp, int i);

/**/
const char *get_wt_name(const char *name);

/* mkudic.c
 * 用例辞書を作る */
struct uc_dict *create_uc_dict(void);
void read_uc_file(struct uc_dict *ud, const char *fn);
void free_uc_dict(struct uc_dict *dict);
void make_ucdict(FILE *out, struct uc_dict *uc);
/**/

/* writewords.c */
void output_word_dict(struct yomi_entry_list *yl);

/* calcfreq.c */
void calc_freq(struct yomi_entry_list *yl);

#endif
