/* splitterモジュールのインターフェイス */
#ifndef _splitter_h_included_
#define _splitter_h_included_

#include <anthy/dic.h>
#include <anthy/xstr.h>
#include <anthy/wtype.h>
#include <anthy/segclass.h>

/* パラメータ */
#define RATIO_BASE 256
#define OCHAIRE_SCORE 5000000

/** splitterのコンテキスト．
 * 最初の境界設定からanthy_contextの解放まで有効
 */
struct splitter_context {
  /** splitter内部で使用する構造体 */
  struct word_split_info_cache *word_split_info;
  int char_count;
  int is_reverse;
  struct char_ent {
    xchar *c;
    int seg_border;
    int initial_seg_len;/* 最初の文節分割の際にここから始まった文節が
			   あればその長さ */
    enum seg_class best_seg_class;
    struct meta_word* best_mw; /* 一番優先して使いたいmetaword */
  }*ce;
};

/* 制約のチェックの状態 */
enum constraint_stat {
  unchecked, ok, ng
};

/* とりあえず、適当に増やしてみて問題が出たら分類する */
enum metaword_type {
  /* ダミー : seginfoを持たない */
  MW_DUMMY,
  /* wordlistを0個 or 一個含むもの */
  MW_SINGLE,
  /* 別のmetaword一個を含む: metaword + 句読点 など :seginfoはmw1から取る */
  MW_WRAP,
  /* 複合語先頭 */
  MW_COMPOUND_HEAD,
  /* 複合語用 */
  MW_COMPOUND,
  /* 複合語の一文節 */
  MW_COMPOUND_LEAF,
  /* 複合語の中の個々の文節を結合して一つの文節としてみたもの */
  MW_COMPOUND_PART,
  /* 動詞の連用形 + 形容詞 */
  MW_V_RENYOU_A,
  /* 動詞の連用形 + 名詞 */
  MW_V_RENYOU_NOUN,
  /* 数字 */
  MW_NUMBER,
  /**/
  MW_OCHAIRE,
  /**/
  MW_END
};

#define MW_FEATURE_NONE 0
#define MW_FEATURE_SV 1
#define MW_FEATURE_WEAK_CONN 2
#define MW_FEATURE_SUFFIX 4
#define MW_FEATURE_NUM 16
#define MW_FEATURE_CORE1 32
#define MW_FEATURE_DEP_ONLY 64
#define MW_FEATURE_HIGH_FREQ 128
#define MW_FEATURE_WEAK_SEQ 256

/*
 * meta_word: 境界の検索の対象となるもの
 * 単一のword_listを含むものの他にいくつかの種類がある．
 * 
 */
struct meta_word {
  int from, len;
  /* 文節境界の検出に使用するスコア */
  int score;
  /* 候補の生成の時に使用するスコア */
  int struct_score;
  /* 形式の情報 */
  int dep_word_hash;
  int mw_features;
  wtype_t core_wt;
  enum dep_class dep_class;
  /**/
  enum seg_class seg_class;
  enum constraint_stat can_use; /* セグメント境界に跨がっていない */
  enum metaword_type type;
  struct word_list *wl;
  struct meta_word *mw1, *mw2;
  xstr cand_hint;

  int nr_parts;

  /* listのリンク */
  struct meta_word *next;
};

int anthy_init_splitter(void);
void anthy_quit_splitter(void);

void anthy_init_split_context(xstr *xs, struct splitter_context *, int is_reverse);
/*
 * mark_border(context, l1, l2, r1);
 * l1とr1の間の文節を検出する、ただしl1とl2の間は境界にしない。
 */
void anthy_mark_border(struct splitter_context *, int from, int from2, int to);
void anthy_commit_border(struct splitter_context *, int nr,
		   struct meta_word **mw, int *len);
void anthy_release_split_context(struct splitter_context *c);

/* 作り出した文節の情報を取得する */
int anthy_get_nr_metaword(struct splitter_context *, int from, int len);
struct meta_word *anthy_get_nth_metaword(struct splitter_context *,
					 int from, int len, int nth);
/**/
int anthy_dep_word_hash(xstr *xs);


#endif
