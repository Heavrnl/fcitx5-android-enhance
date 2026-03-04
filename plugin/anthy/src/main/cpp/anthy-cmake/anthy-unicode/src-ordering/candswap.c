/*
 * 候補の交換のヒストリを管理する。
 *
 * anthy_swap_cand_ent() で学習する
 * anthy_proc_swap_candidate() で学習結果を用いる
 *
 *  「田端が」という候補をトップに出して「田畑が」で確定された場合は
 *  自立語部:「田端」->「田畑」
 *    の二つのエントリを追加する
 *
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 *
 */
#include <stdlib.h>
#include <stdio.h>

#include <anthy/logger.h>
#include <anthy/record.h>
#include <anthy/segment.h>
/* for OCHAIRE_SCORE */
#include <anthy/splitter.h>
#include "sorter.h"

#define MAX_INDEP_PAIR_ENTRY 100

/* 候補の自立語部を学習する */
static void
learn_swap_cand_indep(struct cand_ent *o, struct cand_ent *n)
{
  xstr os, ns;
  int res;
  int o_idx = o->core_elm_index;
  int n_idx = n->core_elm_index;

  /* 自立語部を含む文節しか学習しない */
  if (o_idx < 0 || n_idx < 0) {
    return ;
  }
  if (o->elm[o_idx].str.len != n->elm[n_idx].str.len) {
    return ;
  }
  if (o->elm[o_idx].nth == -1 || n->elm[n_idx].nth == -1) {
    return ;
  }
  res = anthy_get_nth_dic_ent_str(o->elm[o_idx].se, &o->elm[o_idx].str,
				  o->elm[o_idx].nth, &os);
  if (res) {
    return ;
  }
  res = anthy_get_nth_dic_ent_str(n->elm[n_idx].se, &n->elm[n_idx].str,
				  n->elm[n_idx].nth, &ns);
  if (res) {
    free(os.str);
    return ;
  }
  if (anthy_select_section("INDEPPAIR", 1) == 0) {
    if (anthy_select_row(&os, 1) == 0) {
      anthy_set_nth_xstr(0, &ns);
    }
  }
  free(os.str);
  free(ns.str);
}

/*
 * 候補o を出したらn がコミットされたので
 * o -> n をrecordにセットする
 */
void
anthy_swap_cand_ent(struct cand_ent *o, struct cand_ent *n)
{
  if (o == n) {
    /* 同じ候補 */
    return ;
  }
  if (n->flag & CEF_USEDICT) {
    /* 用例辞書から出てきた候補 */
    return ;
  }
  /* 自立語部 */
  learn_swap_cand_indep(o, n);
}


/*
 * 変換時に生成した候補を並べた状態で最優先の候補を決める
 * ループの除去なども行う
 */
static xstr *
prepare_swap_candidate(xstr *target)
{
  xstr *xs, *n;
  if (anthy_select_row(target, 0) == -1) {
    return NULL;
  }
  xs = anthy_get_nth_xstr(0);
  if (!xs) {
    return NULL;
  }
  /* 第一候補 -> xs となるのを発見 */
  anthy_mark_row_used();
  if (anthy_select_row(xs, 0) != 0){
    /* xs -> ⊥ */
    return xs;
  }
  /* xs -> n */
  n = anthy_get_nth_xstr(0);
  if (!n) {
    return NULL;
  }

  if (!anthy_xstrcmp(target, n)) {
    int i;
    char buff[256];
    /* 第一候補 -> xs -> n で n = 第一候補のループ */
    if (anthy_select_row(target, 0)) {
      for (i = 0; i < 3 && i < target->len; i++)
        sprintf (buff + i * 6, "%04X, ", target->str[i]);
      anthy_log(0, "No current selection or Could not find %s in %s:%d.\n",
                buff, __FILE__, __LINE__);
    }
    anthy_release_row();
    if (anthy_select_row(xs, 0)) {
      for (i = 0; i < 3 && i < xs->len; i++)
        sprintf (buff + i * 6, "%04X, ", xs->str[i]);
      anthy_log(0, "No current selection or Could not find %s in %s:%d.\n",
                buff, __FILE__, __LINE__);
    }
    anthy_release_row();
    /* 第一候補 -> xs を消して、交換の必要は無し */
    return NULL;
  }
  /* 第一候補 -> xs -> n で n != 第一候補なので
   * 第一候補 -> nを設定
   */
  if (anthy_select_row(target, 0) == 0){
    anthy_set_nth_xstr(0, n);
  }
  return n;
}

#include <src-worddic/dic_ent.h>

/*
 * 自立語のみ
 */
static void
proc_swap_candidate_indep(struct seg_ent *se)
{
  xstr *xs;
  xstr key;
  int i;
  int core_elm_idx;
  int res;
  struct cand_elm *core_elm;

  core_elm_idx = se->cands[0]->core_elm_index;
  if (core_elm_idx < 0) {
    return ;
  }

  /* 0番目の候補の文字列を取り出す */
  core_elm = &se->cands[0]->elm[core_elm_idx];
  if (core_elm->nth < 0) {
    return ;
  }
  res = anthy_get_nth_dic_ent_str(core_elm->se,
				  &core_elm->str,
				  core_elm->nth,
				  &key);
  if (res) {
    return ;
  }

  /**/
  if (anthy_select_section("INDEPPAIR", 1))
    anthy_log(0, "Failed to save INDEPPAIR in %s:%d.\n", __FILE__, __LINE__);
  xs = prepare_swap_candidate(&key);
  free(key.str);
  if (!xs) {
    return ;
  }

  /* 第一候補 -> xs なので xsの候補を探す */
  for (i = 1; i < se->nr_cands; i++) {
    if (se->cands[i]->nr_words == se->cands[0]->nr_words &&
	se->cands[i]->core_elm_index == core_elm_idx) {
      xstr cand;
      res = anthy_get_nth_dic_ent_str(se->cands[i]->elm[core_elm_idx].se,
				      &se->cands[i]->elm[core_elm_idx].str,
				      se->cands[i]->elm[core_elm_idx].nth,
				      &cand);
      if (res == 0 &&
	  !anthy_xstrcmp(&cand, xs)) {
	free(cand.str);
	/* みつけたのでその候補のスコアをアップ */
	se->cands[i]->score = se->cands[0]->score + 1;
	return ;
      }
      free(cand.str);
    }
  }
}

/*
 * 変換時に生成した候補を並べた状態で最優先の候補を決める
 */
void
anthy_proc_swap_candidate(struct seg_ent *seg)
{
  if (NULL == seg->cands) { /* 辞書もしくは学習データが壊れていた時の対策 */
    return;
  }

  if (seg->cands[0]->score >= OCHAIRE_SCORE) {
    /* cands[0] は特別な点数を持っている */
    return ;
  }
  if (seg->cands[0]->flag & CEF_USEDICT) {
    return ;
  }
  /**/
  proc_swap_candidate_indep(seg);
}

/* 候補交換の古いエントリを消す */
void
anthy_cand_swap_ageup(void)
{
  if (anthy_select_section("INDEPPAIR", 0) == 0) {
    anthy_truncate_section(MAX_INDEP_PAIR_ENTRY);
  }
}
