/*
 * 文節の境界を検出する。
 *
 * metawordの選択にはビタビアルゴリズムを使う
 *
 * anthy_eval_border() で指定された領域を文節に分割する
 *
 * Funded by IPA未踏ソフトウェア創造事業 2001 10/29
 * Copyright (C) 2000-2003 TABATA Yusuke, UGAWA Tomoharu
 */
#include <stdio.h>
#include <stdlib.h>

#include <anthy/alloc.h>
#include <anthy/splitter.h>
#include "wordborder.h"

static int
border_check(struct meta_word* mw,
	     int from,
	     int border)
{
  if (mw->from < border) {
    /* 先頭の文節の中から始まるmwは文節区切りにぴったりあっていないとダメ */
    if (mw->from == from && mw->from + mw->len == border) {
      return 1;
    } else {
      return 0;
    }
  } else {
    /* 後ろの文節は無条件に使用可能 */
    return 1;
  }
}

/*
 * 再帰的にmetawordが使用可能かチェックする
 */
static void
metaword_constraint_check(struct splitter_context *sc,
			  struct meta_word *mw,
			  int from,
			  int border)
{
  if (!mw) return;
  if (mw->can_use != unchecked) return;

  switch(anthy_metaword_type_tab[mw->type].check){
  case MW_CHECK_SINGLE:
    mw->can_use = border_check(mw, from, border) ? ok : ng;
    break;
  case MW_CHECK_BORDER:
    {
      struct meta_word* mw1 = mw->mw1;
      struct meta_word* mw2 = mw->mw2;

      if (mw1&&mw2&&mw1->from + mw1->len == border) {
	/* ちょうど境目にマークが入ってる */
	mw->can_use = ng;
	break;
      }
      if (mw1)
	metaword_constraint_check(sc, mw1, from, border);
      if (mw2)
	metaword_constraint_check(sc, mw2, mw2->from, border);

      if ((!mw1 || mw1->can_use == ok) && (!mw2 || mw2->can_use == ok)) {
	mw->can_use = ok;
      } else {
	mw->can_use = ng;
      }
    }
    break;
  case MW_CHECK_WRAP:
    metaword_constraint_check(sc, mw->mw1, from, border);
    mw->can_use = mw->mw1->can_use;
    break;
  case MW_CHECK_NUMBER:
    {
      struct meta_word* itr = mw;
      mw->can_use = ok;

      /* 個々の文節の一つでも文節区切りをまたがっていれば、この複合語は使えない */
      for (; itr && itr->type == MW_NUMBER; itr = itr->mw2) {
	struct meta_word* mw1 = itr->mw1;
	if (!border_check(mw1, from, border)) {
	  mw->can_use = ng;
	  break;
	}
      }
    }
    break;
  case MW_CHECK_COMPOUND:
    {
      struct meta_word* itr = mw;
      mw->can_use = ok;

      /* 個々の文節の一つでも文節区切りをまたがっていれば、この複合語は使えない */
      for (; itr && (itr->type == MW_COMPOUND_HEAD || itr->type == MW_COMPOUND); itr = itr->mw2) {
	struct meta_word* mw1 = itr->mw1;
	if (!border_check(mw1, from, border)) {
	  mw->can_use = ng;
	  break;
	}
      }
    }
    break;
  case MW_CHECK_OCHAIRE:
    {
      struct meta_word* mw1;
      if (border_check(mw, from, border)) {
	for (mw1 = mw; mw1; mw1 = mw1->mw1) {
	  mw1->can_use = ok;
	}
      } else {
	for (mw1 = mw; mw1; mw1 = mw1->mw1) {
	  mw1->can_use = ng;
	}
      }
    }
    break;
  case MW_CHECK_NONE:
    break;
  default:
    printf("try to check unknown type of metaword (%d).\n", mw->type);
  }
}

/*
 * 全てのmetawordについて使用できるかどうかをチェックする
 */
static void
metaword_constraint_check_all(struct splitter_context *sc,
			      int from, int to,
			      int border)
{
  int i;
  struct word_split_info_cache *info;
  info = sc->word_split_info;

  /* まずuncheckedにする */
  for (i = from; i < to; i ++) {
    struct meta_word *mw;
    for (mw = info->cnode[i].mw;
	 mw; mw = mw->next) {
      mw->can_use = unchecked;
    }
  }

  /* 次に合成されたmetawordについてチェック */
  for (i = from; i < to; i ++) {
    struct meta_word *mw;
    for (mw = info->cnode[i].mw; mw; mw = mw->next) {
      metaword_constraint_check(sc, mw, from, border);
    }
  }
}

/*
 * ここから文節境界をマークする
 */
void
anthy_eval_border(struct splitter_context *sc, int from, int from2, int to)
{
  struct meta_word *mw;
  int nr;

  /* 文節候補のうち使えるもののみ選択 */
  metaword_constraint_check_all(sc, from, to, from2);

  /* fromとfrom2の間をカバーするmeta_wordがあるかどうかを探す。
   * あれば、fromから解析を行い、なければfrom2から解析をする。
   */
  nr = 0;
  for (mw = sc->word_split_info->cnode[from].mw; mw; mw = mw->next) {
    if (mw->can_use == ok) {
      nr ++;
      break;
    }
  }
  if (nr == 0) {
    from = from2;
  }

  /* 文節の境界を設定する */
  anthy_mark_borders(sc, from, to);
}
