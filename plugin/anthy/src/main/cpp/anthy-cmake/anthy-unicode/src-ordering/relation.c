/*
 * 文節の関係を処理する
 * Copyright (C) 2006 Higashiyama Masahiko (thanks google summer of code program)
 * Copyright (C) 2002-2007 TABATA Yusuke
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 *
 * anthy_reorder_candidates_by_relation()
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

#include <arpa/inet.h>
#include <stdlib.h>

#include <anthy/segclass.h>
#include <anthy/segment.h>
#include <anthy/ordering.h>
#include <anthy/dic.h>
#include <anthy/diclib.h>
#include <anthy/feature_set.h>
#include <anthy/corpus.h>
#include "sorter.h"

#define MAX_COLLISION 4
#define SEARCH_LIMIT 100
#define MAX_NEIGHBOR 10


/* 全文検索用のコーパス */
static struct corpus_ {
  /* header */
  void *corpus_bucket;
  void *corpus_array;
  /**/
  int *bucket;
  int *array;
  /**/
  int bucket_size;
  int array_size;
} corpus_info;

/* 検索用のiterator */
struct iterator {
  /* 検索のキーと現在の場所 */
  int key;
  int idx;
  /* 検索回数の上限 */
  int limit;
};

struct neighbor {
  int nr;
  int id[MAX_NEIGHBOR];
};

/** 文節@segの中に@from_word_idの単語と共起関係にある
 *  候補があるかどうかを探し、あればスコアを上げる。
 */
static void
reorder_candidate(int from_word_id, struct seg_ent *seg)
{
  int i, pos;
  struct cand_ent *ce;
  if (NULL == seg->cands) { /* 辞書もしくは学習データが壊れていた時の対策 */
    return;
  }
  ce = seg->cands[0];
  if (ce->core_elm_index == -1) {
    return ;
  }
  /* 0番目の候補の品詞 */
  pos = anthy_wtype_get_pos(ce->elm[ce->core_elm_index].wt);

  for (i = 0; i < seg->nr_cands; i++) {
    int word_id;
    ce = seg->cands[i];
    if (ce->core_elm_index == -1) {
      continue;
    }
    word_id = ce->elm[ce->core_elm_index].id;
    if (anthy_dic_check_word_relation(from_word_id, word_id) &&
	anthy_wtype_get_pos(ce->elm[ce->core_elm_index].wt) == pos) {
      /* 用例にマッチしたので、候補のスコアを更新 */
      ce->flag |= CEF_USEDICT;
      ce->score *= 10;
    }
  }
}

static int
get_indep_word_id(struct seg_ent *seg, int nth)
{
  struct cand_ent *ce;
  if (NULL == seg->cands) { /* 辞書もしくは学習データが壊れていた時の対策 */
    return -1;
  }
  if (seg->cands[nth]->core_elm_index == -1) {
    /* 一番目の候補がseq_entから作られた候補ではない */
    return -1;
  }
  ce = seg->cands[nth];
  /* 自立語のidを取り出す */
  return ce->elm[ce->core_elm_index].id;
}

/* 用例辞書を使って並び替えをする */
static void
reorder_by_use_dict(struct segment_list *sl, int nth)
{
  int i;
  struct seg_ent *cur_seg;
  int word_id;

  cur_seg = anthy_get_nth_segment(sl, nth);
  word_id = get_indep_word_id(cur_seg, 0);
  if (word_id == -1) {
    /**/
    return ;
  }
  /* 近所の文節を順に見ていく */
  for (i = nth - 2; i < nth + 2 && i < sl->nr_segments; i++) {
    struct seg_ent *target_seg;
    if (i < 0 || i == nth) {
      continue ;
    }
    /* i番目の文節と前後のj番目の文節に対して */
    target_seg = anthy_get_nth_segment(sl, i);
    reorder_candidate(word_id, target_seg);
  }
}

static int
find_border_of_this_word(int idx)
{
  int val;
  if (idx < 0) {
    return 0;
  }
  val = ntohl(corpus_info.array[idx * 2]);
  while (!(val & ELM_WORD_BORDER) &&
	 idx > -1) {
    idx --;
  }
  return idx;
}

static int
find_left_word_border(int idx)
{
  int val;
  if (idx == -1) {
    return -1;
  }
  val = ntohl(corpus_info.array[idx * 2]);
  if (val & ELM_BOS) {
    return -1;
  }
  idx --;
  return find_border_of_this_word(idx);
}

static int
find_right_word_border(int idx)
{
  if (idx == -1) {
    return -1;
  }
  while (idx < corpus_info.array_size - 2) {
    int val;
    idx ++;
    val = ntohl(corpus_info.array[idx * 2]);
    if (val & ELM_BOS) {
      return -1;
    }
    if (val & ELM_WORD_BORDER) {
      return idx;
    }
  }
  return -1;
}

static void
push_id(struct neighbor *ctx,
	int id)
{
  if (ctx->nr < MAX_NEIGHBOR - 1) {
    ctx->id[ctx->nr] = id;
    ctx->nr++;
  }
}

static void
collect_word_context(struct neighbor *ctx, int idx)
{
  int id = ntohl(corpus_info.array[idx * 2]) & CORPUS_KEY_MASK;
  /*printf("  id=%d\n", id);*/
  push_id(ctx, id);
}

/* 例文中で周辺の情報を取得する */
static void
collect_corpus_context(struct neighbor *ctx,
		       struct iterator *it)
{
  int i;
  int this_idx, idx;

  this_idx = find_border_of_this_word(it->idx);

  /*printf(" key=%d\n", it->key);*/
  /* 左へスキャン */
  idx = this_idx;
  for (i = 0; i < 2; i++) {
    idx = find_left_word_border(idx);
    if (idx == -1) {
      break;
    }
    collect_word_context(ctx, idx);
  }
  /* 右へスキャン */
  idx = this_idx;
  for (i = 0; i < 2; i++) {
    idx = find_right_word_border(idx);
    if (idx == -1) {
      break;
    }
    collect_word_context(ctx, idx);
  }
}

/* 変換対象の文字列の周辺の情報を取得する */
static void
collect_user_context(struct neighbor *ctx,
		     struct segment_list *sl, int nth)
{
  int i;
  ctx->nr = 0;
  for (i = nth - 2; i <= nth + 2 && i < sl->nr_segments; i++) {
    int id;
    if ((i < 0) || (i == nth)) {
      continue;
    }
    id = get_indep_word_id(anthy_get_nth_segment(sl, i), 0);
    if (id > -1) {
      id &= CORPUS_KEY_MASK;
      /*printf("user_ctx=%d\n", id);*/
      push_id(ctx, id);
    }
  }
}

/* 隣接文節の情報を比較する */
static int
do_compare_context(struct neighbor *n1,
		   struct neighbor *n2)
{
  int i, j;
  int m = 0;
  for (i = 0; i < n1->nr; i++) {
    for (j = 0; j < n2->nr; j++) {
      if (n1->id[i] == n2->id[j]) {
	m++;
      }
    }
  }
  return m;
}

/* 隣接文節の情報を取得して比較する */
static int
compare_context(struct neighbor *user,
		struct iterator *it)
{
  struct neighbor sample;
  int nr;
  /**/
  sample.nr = 0;
  /* 例文中の周辺情報を集める */
  collect_corpus_context(&sample, it);
  if (sample.nr == 0) {
    return 0;
  }
  /* 比較する */
  nr = do_compare_context(user, &sample);
  if (nr >= sample.nr / 2) {
    return nr;
  }
  return 0;
}

/* keyの最初の出現場所を見つける
 * 見つからなかったら-1を返す
 */
static int
find_first_pos(int key)
{
  int i;
  for (i = 0; i < MAX_COLLISION; i++) {
    int bkt = (key + i) % corpus_info.bucket_size;
    if ((int)ntohl(corpus_info.bucket[bkt * 2]) == key) {
      return ntohl(corpus_info.bucket[bkt * 2 + 1]);
    }
  }
  return -1;
}

/* keyの最初の出現場所でiteratorを初期化する
 * 見つからなかったら-1を返す
 */
static int
find_first_from_corpus(int key, struct iterator *it, int limit)
{
  key &= CORPUS_KEY_MASK;
  it->idx = find_first_pos(key);
  it->key = key;
  it->limit = limit;
  return it->idx;
}

/* keyの次の出現場所のiteratorを設定する
 */
static int
find_next_from_corpus(struct iterator *it)
{
  int idx = it->idx;
  it->limit--;
  if (it->limit < 1) {
    it->idx = -1;
    return -1;
  }
  it->idx = ntohl(corpus_info.array[it->idx * 2 + 1]);
  if (it->idx < 0 || it->idx >= corpus_info.array_size ||
      it->idx < idx) {
    it->idx = -1;
  }
  return it->idx;
}

static void
check_candidate_context(struct seg_ent *cur_seg,
			int i,
			struct neighbor *user)
{
  struct iterator it;
  int nr = 0;
  int word_id;
  word_id = get_indep_word_id(cur_seg, i);
  if (word_id == -1) {
    return ;
  }
  /* 各出現場所をスキャンする */
  find_first_from_corpus(word_id, &it, SEARCH_LIMIT);
  /*printf("word_id=%d %d\n", word_id, it.idx);*/
  while (it.idx > -1) {
    nr += compare_context(user, &it);
    /**/
    find_next_from_corpus(&it);
  }
  /**/
  if (nr > 0) {
    cur_seg->cands[i]->flag |= CEF_CONTEXT;
  }
}

/* 全文検索で候補を並び替える */
static void
reorder_by_corpus(struct segment_list *sl, int nth)
{
  struct seg_ent *cur_seg;
  struct neighbor user;
  int i;
  /* 文節の周辺情報を集める */
  collect_user_context(&user, sl, nth);
  if (user.nr == 0) {
    return ;
  }
  cur_seg = anthy_get_nth_segment(sl, nth);
  if (NULL == cur_seg->cands) { /* 辞書もしくは学習データが壊れていた時の対策 */
    return;
  }
  /* 各候補について */
  for (i = 0; i < cur_seg->nr_cands; i++) {
    check_candidate_context(cur_seg, i, &user);
  }
  /* トップの候補に用例があれば、他の候補は見ない */
  if (cur_seg->cands[0]->flag & CEF_CONTEXT) {
    cur_seg->cands[0]->flag &= ~CEF_CONTEXT;
    return ;
  }
  /* 用例によるスコア加算 */
  for (i = 1; i < cur_seg->nr_cands; i++) {
    if (cur_seg->cands[i]->flag & CEF_CONTEXT) {
      cur_seg->cands[i]->score *= 2;
    }
  }
}

/*
 * 用例を用いて候補を並び替える
 *  @nth番目以降の文節を対象とする
 */
void
anthy_reorder_candidates_by_relation(struct segment_list *sl, int nth)
{
  int i;
  for (i = nth; i < sl->nr_segments; i++) {
    reorder_by_use_dict(sl, i);
    reorder_by_corpus(sl, i);
  }
}

void
anthy_relation_init(void)
{
  corpus_info.corpus_array = anthy_file_dic_get_section("corpus_array");
  corpus_info.corpus_bucket = anthy_file_dic_get_section("corpus_bucket");
  if (!corpus_info.corpus_array || !corpus_info.corpus_bucket)
    return ;
  corpus_info.array_size = ntohl(((int *)corpus_info.corpus_array)[1]);
  corpus_info.bucket_size = ntohl(((int *)corpus_info.corpus_bucket)[1]);
  corpus_info.array = &(((int *)corpus_info.corpus_array)[16]);
  corpus_info.bucket = &(((int *)corpus_info.corpus_bucket)[16]);
  /*
  {
    int i;
    for (i = 0; i < corpus_info.array_size; i++) {
      int v = ntohl(corpus_info.array[i * 2]);
      printf("%d: %d %d\n", i, v, v & CORPUS_KEY_MASK);
    }
  }
  */
}
