/*
 * 文節に対する候補をソートする。
 * 将来的には近接する文節も見て、単語の結合による評価をする。
 * ダブった候補の削除もする。
 *
 * Funded by IPA未踏ソフトウェア創造事業 2001 9/22
 * Copyright (C) 2000-2006 TABATA Yusuke
 * Copyright (C) 2001 UGAWA Tomoharu
 *
 * $Id: candsort.c,v 1.27 2002/11/17 14:45:47 yusuke Exp $
 *
 */
/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include <anthy/segment.h>
#include <anthy/splitter.h>
#include <anthy/ordering.h>
#include "sorter.h"

/* お茶入れ学習による候補 */
#define OCHAIRE_BASE OCHAIRE_SCORE
/* metawordが十分無理矢理くさいときの、ひらがなカタカナのスコア */
#define NOCONV_WITH_BIAS 900000
/* 普通の候補 */
#define NORMAL_BASE 100
/* 単漢字 */
#define SINGLEWORD_BASE 10
/* 複合語 */
#define COMPOUND_BASE (OCHAIRE_SCORE / 2)
/* 複合語の一部分を一文節にしたもの */
#define COMPOUND_PART_BASE 2
/* 付属語のみ */
#define DEPWORD_BASE (OCHAIRE_SCORE / 2)
/* ひらがなカタカナのデフォルトのスコア */
#define NOCONV_BASE 1

/* 無理っぽい候補割り当てか判断する */
static int
uncertain_segment_p(struct seg_ent *se)
{
  struct meta_word *mw;
  if (se->nr_metaword == 0) {
    return 0;
  }

  mw = se->mw_array[0];

  /* 長さの6割 */
  if (se->len * 3 >= mw->len * 5) {
    return 1;
  }
  return 0;
}

static void
release_redundant_candidate(struct seg_ent *se)
{
  int i, j;
  /* 配列はソートされているのでscoreが0の候補が後ろに並んでいる */
  for (i = 0; i < se->nr_cands && se->cands[i]->score; i++);
  /* iから後ろの候補を解放 */
  if (i < se->nr_cands) {
    for (j = i; j < se->nr_cands; j++) {
      anthy_release_cand_ent(se->cands[j]);
    }
    se->nr_cands = i;
  }
}

/* qsort用の候補比較関数 */
static int
candidate_compare_func(const void *p1, const void *p2)
{
  const struct cand_ent *const *c1 = p1, *const *c2 = p2;
  return (*c2)->score - (*c1)->score;
}

static void
sort_segment(struct seg_ent *se)
{
  qsort(se->cands, se->nr_cands,
	sizeof(struct cand_ent *),
	candidate_compare_func);
}

static void
trim_kana_candidate(struct seg_ent *se)
{
  int i;
  if (NULL == se->cands) {  /* 辞書もしくは学習データが壊れていた時の対策 */
    return;
  }
  if (se->cands[0]->flag & CEF_KATAKANA) {
    return ;
  }
  for (i = 1; i < se->nr_cands; i++) {
    if (se->cands[i]->flag & CEF_KATAKANA) {
      /* 最低点まで下げる */
      se->cands[i]->score = NOCONV_BASE;
    }
  }
}

static void
check_dupl_candidate(struct seg_ent *se)
{
  int i,j;
  for (i = 0; i < se->nr_cands - 1; i++) {
    for (j = i + 1; j < se->nr_cands; j++) {
      if (!anthy_xstrcmp(&se->cands[i]->str, &se->cands[j]->str)) {
	/* ルールに良くマッチしたものの方を選ぶとかすべき */
	se->cands[j]->score = 0;
	se->cands[i]->flag |= se->cands[j]->flag;
      }
    }
  }
}

/* 品詞割り当てによって生成された候補を評価する */
static void
eval_candidate_by_metaword(struct cand_ent *ce)
{
  int i;
  int score = 1;

  /* まず、単語の頻度によるscoreを加算 */
  for (i = 0; i < ce->nr_words; i++) {
    struct cand_elm *elm = &ce->elm[i];
    int pos, div = 1;
    int freq;

    if (elm->nth < 0) {
      /* 候補割り当ての対象外なのでスキップ */
      continue;
    }
    pos = anthy_wtype_get_pos(elm->wt);
    if (pos == POS_PRE || pos == POS_SUC) {
      div = 4;
    }

    freq = anthy_get_nth_dic_ent_freq(elm->se, elm->nth);
    score += freq / div;
  }

  if (ce->mw) {
    score *= ce->mw->struct_score;
    score /= RATIO_BASE;
  }
  ce->score = score;
}

/* 候補を評価する */
static void
eval_candidate(struct cand_ent *ce, int uncertain)
{
  if ((ce->flag &
       (CEF_OCHAIRE | CEF_SINGLEWORD | CEF_HIRAGANA |
	CEF_KATAKANA | CEF_GUESS | CEF_COMPOUND | CEF_COMPOUND_PART |
	CEF_BEST)) == 0) {
    /* splitterからの情報(metaword)によって生成された候補 */
    eval_candidate_by_metaword(ce);
  } else if (ce->flag & CEF_OCHAIRE) {
    ce->score = OCHAIRE_BASE;
  } else if (ce->flag & CEF_SINGLEWORD) {
    ce->score = SINGLEWORD_BASE;
  } else if (ce->flag & CEF_COMPOUND) {
    ce->score = COMPOUND_BASE;
  } else if (ce->flag & CEF_COMPOUND_PART) {
    ce->score = COMPOUND_PART_BASE;
  } else if (ce->flag & CEF_BEST) {
    ce->score = OCHAIRE_BASE;
  } else if (ce->flag & (CEF_HIRAGANA | CEF_KATAKANA |
			 CEF_GUESS)) {
    if (uncertain) {
      /*
       * この文節は外来語などのようなので、生成した候補よりも
       * ひらがなカタカナの候補を出した方がよい
       */
      ce->score = NOCONV_WITH_BIAS;
      if (CEF_KATAKANA & ce->flag) {
	ce->score ++;
      }
      if (CEF_GUESS & ce->flag) {
	ce->score += 2;
      }
    } else {
      ce->score = NOCONV_BASE;
    }
  }
  ce->score += 1;
}

static void
eval_segment(struct seg_ent *se)
{
  int i;
  int uncertain = uncertain_segment_p(se);
  for (i = 0; i < se->nr_cands; i++) {
    eval_candidate(se->cands[i], uncertain);
  }
}

/* 学習履歴の内容で順位を調整する */
static void
apply_learning(struct segment_list *sl, int nth)
{
  int i;

  /*
   * 優先順位の低いものから順に適用する
   */

  /* 用例辞書による順序の変更 */
  anthy_reorder_candidates_by_relation(sl, nth);
  /* 候補の交換 */
  for (i = nth; i < sl->nr_segments; i++) {
    struct seg_ent *seg = anthy_get_nth_segment(sl, i);
    /* 候補の交換 */
    anthy_proc_swap_candidate(seg);
    /* 履歴による順序の変更 */
    anthy_reorder_candidates_by_history(anthy_get_nth_segment(sl, i));
  }
}

/** 外から呼ばれるエントリポイント
 * @nth以降の文節を対象とする
 */
void
anthy_sort_candidate(struct segment_list *sl, int nth)
{
  int i;
  for (i = nth; i < sl->nr_segments; i++) {
    struct seg_ent *seg = anthy_get_nth_segment(sl, i);
    /* まず評価する */
    eval_segment(seg);
    /* つぎにソートする */
    sort_segment(seg);
    /* ダブったエントリの点の低い方に0点を付ける */
    check_dupl_candidate(seg);
    /* もういちどソートする */
    sort_segment(seg);
    /* 評価0の候補を解放 */
    release_redundant_candidate(seg);
  }

  /* 学習の履歴を適用する */
  apply_learning(sl, nth);

  /* またソートする */
  for ( i = nth ; i < sl->nr_segments ; i++){
    sort_segment(anthy_get_nth_segment(sl, i));
  }
  /* カタカナの候補が先頭でなければ最後に回す */
  for (i = nth; i < sl->nr_segments; i++) {
    trim_kana_candidate(anthy_get_nth_segment(sl, i));
  }
  /* またソートする */
  for ( i = nth ; i < sl->nr_segments ; i++){
    sort_segment(anthy_get_nth_segment(sl, i));
  }
}
