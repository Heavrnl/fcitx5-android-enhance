/* segment(文節) の定義 */
#ifndef _segment_h_included_
#define _segment_h_included_

#include <anthy/segclass.h>
#include <anthy/wtype.h>
#include <anthy/xstr.h>
#include <anthy/dic.h>

/** 候補の構成要素 */
struct cand_elm {
  int nth; /* -1のときは辞書からの割り当てをやっていない */
  wtype_t wt;
  seq_ent_t se;
  int ratio;/* 頻度を評価する際に使用する比率 */
  xstr str;/* 変換対象の文字列 */
  int id;/* 変換結果の文字列に対するhash値 */
};

/** 一つの候補に相当する。
 * anthy_release_cand_ent()で解放する
 */
struct cand_ent {
  /** 候補のスコア */
  int score;
  /** 変換後の文字列 */
  xstr str;
  /** 要素の数 */
  int nr_words;
  /** 候補を構成する要素の配列 */
  struct cand_elm *elm;
  /** 自立語部のインデックス */
  int core_elm_index;
  /** 付属語のhash値 */
  int dep_word_hash;
  /** 候補のフラグ CEF_? */
  unsigned int flag;
  struct meta_word *mw;
};

/* 候補(cand_ent)のフラグ */
#define CEF_NONE           0
#define CEF_OCHAIRE        0x00000001
#define CEF_SINGLEWORD     0x00000002
#define CEF_HIRAGANA       0x00000004
#define CEF_KATAKANA       0x00000008
#define CEF_GUESS          0x00000010
#define CEF_USEDICT        0x00000020
#define CEF_COMPOUND       0x00000040
#define CEF_COMPOUND_PART  0x00000080
#define CEF_BEST           0x00000100
#define CEF_CONTEXT        0x00000200

/** Context内に存在する文節の列
 * release_seg_entで解放する
 */
struct seg_ent {
  /* strの実体はcontext中にある */
  xstr str;
  /* commitされた候補の番号、負の数の場合はまだコミットされていない */
  int committed;
  
  /* 候補の配列 */
  int nr_cands;/* 候補の数 */
  struct cand_ent **cands;/* 配列 */

  int from, len;/* len == str.len */

  /* 文節の構成 */
  int nr_metaword;
  struct meta_word **mw_array;

  /* 一番成績の良かったクラス */
  enum seg_class best_seg_class;
  /* 一番成績の良かったmeta_word
   * mw_array中にも、含まれることが期待できるが、保証はしない */
  struct meta_word *best_mw;

  struct seg_ent *prev, *next;
};

/** 文節のリスト */
struct segment_list {
  int nr_segments;
  struct seg_ent list_head;
};

/* 候補を解放する(無駄に生成してしまったもの等) */
void anthy_release_cand_ent(struct cand_ent *s);

/**/
struct seg_ent *anthy_get_nth_segment(struct segment_list *c, int );
void anthy_print_candidate(struct cand_ent *ce);

/* compose.c */
/* 候補を作り出す */
struct splitter_context;
void anthy_do_make_candidates(struct splitter_context *sc,
			      struct seg_ent *e, int is_reverse);

#endif
