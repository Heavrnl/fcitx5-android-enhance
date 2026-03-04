/* 文節境界の検出に使うデータ */
#ifndef _wordborder_h_included_
#define _wordborder_h_included_


#include <anthy/dic.h>
#include <anthy/alloc.h>
#include <anthy/segclass.h>
#include <anthy/depgraph.h>

struct splitter_context;

/*
 * meta_wordの使用可能チェックのやり方
 */
enum mw_check {
  /* なにもせず */
  MW_CHECK_NONE,
  /* mw->wlが無いか、wlが使える場合 */
  MW_CHECK_SINGLE,
  MW_CHECK_BORDER,
  MW_CHECK_WRAP,
  MW_CHECK_OCHAIRE,
  MW_CHECK_NUMBER,
  MW_CHECK_COMPOUND
};

/*
 * 文字列中のある場所を表し，
 * そこから始まるmeta_word, word_listのセットを持つ
 */
struct char_node {
  int max_len;
  struct meta_word *mw;
  struct word_list *wl;
};

/*
 * コンテキスト中の自立語などの情報、最初に変換キーを押したときに
 * 構築される
 */
struct word_split_info_cache {
  struct char_node *cnode;

  /* キャッシュ構成時に使う情報 */
  /* 接尾辞を探すのに使う */
  int *seq_len;/* そこから始まる最長の単語の長さ */
  /* 接頭辞を探すのに使う */
  int *rev_seq_len;/* そこで終わる最長の単語の長さ */
  /* 文節境界contextからのコピー */
  int *seg_border;
  /* 検索で一番成績の良かったクラス */
  enum seg_class* best_seg_class;
  /*  */
  struct meta_word **best_mw;
  /* アロケータ */
  allocator MwAllocator, WlAllocator;
};

/*
 * meta_wordの状態
 */
enum mw_status {
  MW_STATUS_NONE,
  /* mw->mw1に中身が入っている */
  MW_STATUS_WRAPPED,
  /* mw-mw1とmw->mw2から連結 */
  MW_STATUS_COMBINED,
  /* 複合語用 */
  MW_STATUS_COMPOUND,
  /* 複合語の個々の文節を結合して一つの文節として見たもの */
  MW_STATUS_COMPOUND_PART,
  /* OCHAIRE学習から取り出す */
  MW_STATUS_OCHAIRE
};



/* metawordの種類による処理の違い (metaword.c) */
extern struct metaword_type_tab_ {
  enum metaword_type type;
  const char *name;
  enum mw_status status;
  enum mw_check check;
} anthy_metaword_type_tab[];

/*
 * 0: 接頭辞
 * 1: 自立語部
 * 2: 接尾辞
 */
#define NR_PARTS 4
#define PART_PREFIX 0
#define PART_CORE 1
#define PART_POSTFIX 2
#define PART_DEPWORD 3

struct part_info {
  /* このpartの長さ */
  int from, len;
  /* 品詞 */
  wtype_t wt;
  seq_ent_t seq;
  /* 頻度 */
  int freq;
  /* 付属語クラス */
  enum dep_class dc;
};

/*
 * word_list: 文節を形成するもの
 * 接頭語、自立語、接尾語、付属語を含む
 */
struct word_list {
  /**/
  int from, len; /* 文節全体 */
  int is_compound; /* 複合語かどうか */

  /**/
  int dep_word_hash;
  int mw_features;
  /**/
  enum seg_class seg_class;
  enum constraint_stat can_use; /* セグメント境界に跨がっていない */

  /* 漢字を得るためではなくて、雑多な処理に使いたい情報 */
  int head_pos; /* lattice検索用の品詞 */
  int tail_ct; /* meta_wordの結合用の活用形 */

  /**/
  int last_part;
  struct part_info part[NR_PARTS];

  /* このword_listを作った際の情報 */
  int node_id; /* 付属語グラフの検索開始のnodeのid*/

  /* 同じfromを持つword_listのリスト */
  struct word_list *next;
};


/* splitter.c */
#define SPLITTER_DEBUG_NONE 0
/* wordlistの表示 */
#define SPLITTER_DEBUG_WL 1
/* metawordの表示 */
#define SPLITTER_DEBUG_MW 2
/* latticeの nodeの表示 */
#define SPLITTER_DEBUG_LN 4
/* 自立語のマッチした品詞 */
#define SPLITTER_DEBUG_ID 8
/**/
#define SPLITTER_DEBUG_CAND 16

int anthy_splitter_debug_flags(void);


/* defined in wordseq.c */
/* 自立語以降の接続の処理 */
void anthy_scan_node(struct splitter_context *sc,
		     struct word_list *wl,
		     xstr *follow, int node);
int anthy_get_node_id_by_name(const char *name);
int anthy_init_depword_tab(void);
void anthy_quit_depword_tab(void);

/* depgraph.c */
int anthy_get_nr_dep_rule(void);
void anthy_get_nth_dep_rule(int, struct wordseq_rule *);

/* defined in wordlist.c */
void anthy_commit_word_list(struct splitter_context *, struct word_list *wl);
struct word_list *anthy_alloc_word_list(struct splitter_context *);
void anthy_print_word_list(struct splitter_context *, struct word_list *);
void anthy_make_word_list_all(struct splitter_context *);

/* defined in metaword.c */
void anthy_commit_meta_word(struct splitter_context *, struct meta_word *mw);
void anthy_make_metaword_all(struct splitter_context *);
void anthy_print_metaword(struct splitter_context *, struct meta_word *);

void anthy_mark_border_by_metaword(struct splitter_context* sc,
				   struct meta_word* mw);


/* defined in evalborder.c */
void anthy_eval_border(struct splitter_context *, int, int, int);

/* defined at lattice.c */
void anthy_mark_borders(struct splitter_context *sc, int from, int to);

/* defined at seg_class.c */
void anthy_set_seg_class(struct word_list* wl);

/* 品詞(anthy_init_splitterで初期化される) */
extern wtype_t anthy_wtype_noun;
extern wtype_t anthy_wtype_name_noun;
extern wtype_t anthy_wtype_num_noun;
extern wtype_t anthy_wtype_prefix;
extern wtype_t anthy_wtype_num_prefix;
extern wtype_t anthy_wtype_num_postfix;
extern wtype_t anthy_wtype_name_postfix;
extern wtype_t anthy_wtype_sv_postfix;
extern wtype_t anthy_wtype_a_tail_of_v_renyou;
extern wtype_t anthy_wtype_v_renyou;
extern wtype_t anthy_wtype_noun_tail;/* いれ「たて」とか */
extern wtype_t anthy_wtype_n1;
extern wtype_t anthy_wtype_n10;

#endif
