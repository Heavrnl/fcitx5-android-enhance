/*
 * 文節の構造metawordをソートする
 *
 * 文節に対する複数の構造の候補をソートする
 *
 * Copyright (C) 2000-2007 TABATA Yusuke
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
#include <stdlib.h>
#include <math.h>

#include <anthy/segment.h>
#include <anthy/ordering.h>
#include <anthy/feature_set.h>
#include <anthy/splitter.h>
#include <anthy/diclib.h>
#include "sorter.h"

static void *cand_info_array;

static double
calc_probability(struct feature_list *fl)
{
  struct feature_freq *res, arg;
  res = anthy_find_feature_freq(cand_info_array,
				fl, &arg);
  if (res) {
    double pos = (double)res->f[15];
    double neg = (double)res->f[14];
    double prob = pos / (pos + neg);
    prob = prob * prob;
    /**/
    return prob;
  }
  return 0;
}

static void
mw_eval(struct seg_ent *prev_seg, struct seg_ent *seg,
	struct meta_word *mw)
{
  int pc;
  struct feature_list fl;
  double prob;
  (void)seg;
  anthy_feature_list_init(&fl);
  /**/
  anthy_feature_list_set_cur_class(&fl, mw->seg_class);
  anthy_feature_list_set_dep_word(&fl, mw->dep_word_hash);
  anthy_feature_list_set_dep_class(&fl, mw->dep_class);
  anthy_feature_list_set_mw_features(&fl, mw->mw_features);
  /* 前の文節の素性 */
  if (prev_seg) {
    pc = prev_seg->best_seg_class;
  } else {
    pc = SEG_HEAD;
  }
  anthy_feature_list_set_class_trans(&fl, pc, mw->seg_class);
  anthy_feature_list_sort(&fl);
  /* 計算する */
  prob = 0.1 + calc_probability(&fl);
  if (prob < 0) {
    prob = (double)1 / (double)1000;
  }
  anthy_feature_list_free(&fl);
  mw->struct_score = RATIO_BASE * RATIO_BASE;
  mw->struct_score *= prob;
  /*
  anthy_feature_list_print(&fl);
  printf(" prob=%f, struct_score=%d\n", prob, mw->struct_score);
  */

  /**/
  if (mw->mw_features & MW_FEATURE_SUFFIX) {
    mw->struct_score /= 2;
  }
  if (mw->mw_features & MW_FEATURE_WEAK_CONN) {
    mw->struct_score /= 10;
  }
}

static void
seg_eval(struct seg_ent *prev_seg,
	 struct seg_ent *seg)
{
  int i;
  for (i = 0; i < seg->nr_metaword; i++) {
    mw_eval(prev_seg, seg, seg->mw_array[i]);
  }
}

static void
sl_eval(struct segment_list *seg_list)
{
  int i;
  struct seg_ent *prev_seg = NULL;
  for (i = 0; i < seg_list->nr_segments; i++) {
    struct seg_ent *seg;
    seg = anthy_get_nth_segment(seg_list, i);
    seg_eval(prev_seg, seg);
    prev_seg = seg;
  }
}

static int
metaword_compare_func(const void *p1, const void *p2)
{
  const struct meta_word * const *s1 = p1;
  const struct meta_word * const *s2 = p2;
  return (*s2)->struct_score - (*s1)->struct_score;
}

void
anthy_sort_metaword(struct segment_list *seg_list)
{
  int i;
  /**/
  sl_eval(seg_list);
  /**/
  for (i = 0; i < seg_list->nr_segments; i++) {
    struct seg_ent *seg = anthy_get_nth_segment(seg_list, i);
    if (seg->mw_array) {    /* 不正なメモリアクセスを行うバグの修正 */
    qsort(seg->mw_array, seg->nr_metaword, sizeof(struct meta_word *),
	  metaword_compare_func);
    }
  }
}

void
anthy_infosort_init(void)
{
  cand_info_array = anthy_file_dic_get_section("cand_info");
}
