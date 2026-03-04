/*
 * 確率を評価しビタビアルゴリズム(viterbi algorithm)によって
 * 文節の区切りを決定してマークする。
 *
 *
 * 外部から呼び出される関数
 *  anthy_mark_borders()
 *
 * Copyright (C) 2006-2007 TABATA Yusuke
 * Copyright (C) 2004-2006 YOSHIDA Yuichi
 * Copyright (C) 2006 HANAOKA Toshiyuki
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
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
/*
 * コンテキスト中に存在するmeta_wordをつないでグラフを構成します。
 * (このグラフのことをラティス(lattice/束)もしくはトレリス(trellis)と呼びます)
 * meta_wordどうしの接続がグラフのノードとなり、構造体lattice_nodeの
 * リンクとして構成されます。
 *
 * ここでの処理は次の二つの要素で構成されます
 * (1) グラフを構成しつつ、各ノードへの到達確率を求める
 * (2) グラフを後ろ(右)からたどって最適なパスを求める
 *
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anthy/alloc.h>
#include <anthy/diclib.h>
#include <anthy/feature_set.h>
#include <anthy/logger.h>
#include <anthy/segclass.h>
#include <anthy/splitter.h>
#include <anthy/xstr.h>
#include "wordborder.h"

static float anthy_normal_length = 20.0; /* 文節の期待される長さ */
static void *trans_info_array;

#define NODE_MAX_SIZE 50

/* グラフのノード(遷移状態) */
struct lattice_node {
  int border; /* 文字列中のどこから始まる状態か */
  enum seg_class seg_class; /* この状態の品詞 */


  double real_probability;  /* ここに至るまでの確率(文節数補正無し) */
  double adjusted_probability;  /* ここに至るまでの確率(文節数補正有り) */


  struct lattice_node* before_node; /* 一つ前の遷移状態 */
  struct meta_word* mw; /* この遷移状態に対応するmeta_word */

  struct lattice_node* next; /* リスト構造のためのポインタ */
};

struct node_list_head {
  struct lattice_node *head;
  int nr_nodes;
};

struct lattice_info {
  /* 遷移状態のリストの配列 */
  struct node_list_head *lattice_node_list;
  struct splitter_context *sc;
  /* ノードのアロケータ */
  allocator node_allocator;
  int lattice_node_len;
};

/*
 */
static void
print_lattice_node(struct lattice_info *info, struct lattice_node *node)
{
  if (!node) {
    printf("**lattice_node (null)*\n");
    return ;
  }
  printf("**lattice_node probability=%.128f\n", node->real_probability);
  if (node->mw) {
    anthy_print_metaword(info->sc, node->mw);
  }
}

static double
get_poisson(double lambda, int r)
{
  int i;
  double result;

  /* 要するにポワソン分布 */
  result = pow(lambda, r) * exp(-lambda);
  for (i = 2; i <= r; ++i) {
    result /= i;
  }

  return result;
}

/* 文節の形式からスコアを調整する */
static double
get_form_bias(struct meta_word *mw)
{
  double bias;
  int r;
  /* wrapされている場合は内部のを使う */
  while (mw->type == MW_WRAP) {
    mw = mw->mw1;
  }
  /* 文節長による調整 */
  r = mw->len;
  if (r > 6) {
    r = 6;
  }
  if (r < 2) {
    r = 2;
  }
  if (mw->seg_class == SEG_RENTAI_SHUSHOKU &&
      r < 3) {
    /* 指示語 */
    r = 3;
  }
  bias = get_poisson(anthy_normal_length, r);
  return bias;
}

static void
build_feature_list(struct lattice_node *node,
		   struct feature_list *features)
{
  int pc, cc;
  if (node) {
    cc = node->seg_class;
  } else {
    cc = SEG_TAIL;
  }
  anthy_feature_list_set_cur_class(features, cc);
  if (node && node->before_node) {
    pc = node->before_node->seg_class;
  } else {
    pc = SEG_HEAD;
  }
  anthy_feature_list_set_class_trans(features, pc, cc);

  if (node && node->mw) {
    struct meta_word *mw = node->mw;
    anthy_feature_list_set_dep_class(features, mw->dep_class);
    anthy_feature_list_set_dep_word(features,
				    mw->dep_word_hash);
    anthy_feature_list_set_mw_features(features, mw->mw_features);
    anthy_feature_list_set_noun_cos(features, mw->core_wt);

  }
  anthy_feature_list_sort(features);
}

static double
calc_probability(int cc, struct feature_list *fl)
{
  struct feature_freq *res, arg;
  double prob;

  /* 確率を計算する */
  res = anthy_find_feature_freq(trans_info_array,
				fl, &arg);
  prob = 0;
  if (res) {
    double pos = res->f[15];
    double neg = res->f[14];
    prob = 1 - (neg) / (double) (pos + neg);
  }
  if (prob <= 0) {
    /* 例文中に存在しないパターンなので0に近いスコア */
    prob = 1.0f / (double)(10000 * 100);
  }

  if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
    anthy_feature_list_print(fl);
    printf(" cc=%d(%s), P=%f\n", cc, anthy_seg_class_name(cc), prob);
  }
  return prob;
}

static double
get_transition_probability(struct lattice_node *node)
{
  struct feature_list features;
  double probability;

  /**/
  anthy_feature_list_init(&features);
  build_feature_list(node, &features);
  probability = calc_probability(node->seg_class, &features);
  anthy_feature_list_free(&features);

  if (!(node->mw))
      return probability;
  /* 文節の形に対する評価 */
  probability *= get_form_bias(node->mw);
  return probability;
}

static struct lattice_info*
alloc_lattice_info(struct splitter_context *sc, int size)
{
  int i;
  struct lattice_info* info = (struct lattice_info*)malloc(sizeof(struct lattice_info));
  info->sc = sc;
  info->lattice_node_list = (struct node_list_head*)
    malloc((size + 1) * sizeof(struct node_list_head));
  if (!(info->lattice_node_list)) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    info->lattice_node_len = 0;
  } else {
    info->lattice_node_len = size + 1;
    for (i = 0; i < size + 1; i++) {
      info->lattice_node_list[i].head = NULL;
      info->lattice_node_list[i].nr_nodes = 0;
    }
  }
  info->node_allocator = anthy_create_allocator(sizeof(struct lattice_node),
						NULL);
  return info;
}

static void
calc_node_parameters(struct lattice_node *node)
{
  assert(node);
  /* 対応するmetawordが無い場合は文頭と判断する */
  node->seg_class = node->mw ? node->mw->seg_class : SEG_HEAD;

  if (node->before_node) {
    /* 左に隣接するノードがある場合 */
    node->real_probability = node->before_node->real_probability *
      get_transition_probability(node);
    node->adjusted_probability = node->real_probability *
      (node->mw ? node->mw->score : 1000);
  } else {
    /* 左に隣接するノードが無い場合 */
    node->real_probability = 1.0;
    node->adjusted_probability = node->real_probability;
  }
}

static struct lattice_node*
alloc_lattice_node(struct lattice_info *info,
		   struct lattice_node* before_node,
		   struct meta_word* mw, int border)
{
  struct lattice_node* node;
  node = anthy_smalloc(info->node_allocator);
  node->before_node = before_node;
  node->border = border;
  node->next = NULL;
  node->mw = mw;

  calc_node_parameters(node);

  return node;
}

static void
release_lattice_node(struct lattice_info *info, struct lattice_node* node)
{
  anthy_sfree(info->node_allocator, node);
}

static void
release_lattice_info(struct lattice_info* info)
{
  anthy_free_allocator(info->node_allocator);
  free(info->lattice_node_list);
  free(info);
}

static int
cmp_node_by_type(struct lattice_node *lhs, struct lattice_node *rhs,
		 enum metaword_type type)
{
  if (lhs->mw->type == type && rhs->mw->type != type) {
    return 1;
  } else if (lhs->mw->type != type && rhs->mw->type == type) {
    return -1;
  } else {
    return 0;
  }
}

static int
cmp_node_by_type_to_type(struct lattice_node *lhs, struct lattice_node *rhs,
			 enum metaword_type type1, enum metaword_type type2)
{
  if (lhs->mw->type == type1 && rhs->mw->type == type2) {
    return 1;
  } else if (lhs->mw->type == type2 && rhs->mw->type == type1) {
    return -1;
  } else {
    return 0;
  }
}

/*
 * ノードを比較する
 *
 ** 返り値
 * 1: lhsの方が確率が高い
 * 0: 同じ
 * -1: rhsの方が確率が高い
 */
static int
cmp_node(struct lattice_node *lhs, struct lattice_node *rhs)
{
  struct lattice_node *lhs_before = lhs;
  struct lattice_node *rhs_before = rhs;
  int ret;

  if (lhs && !rhs) return 1;
  if (!lhs && rhs) return -1;
  if (!lhs && !rhs) return 0;

  while (lhs_before && rhs_before) {
    if (lhs_before->mw && rhs_before->mw &&
	lhs_before->mw->from + lhs_before->mw->len == rhs_before->mw->from + rhs_before->mw->len) {
      /* 学習から作られたノードかどうかを見る */
      ret = cmp_node_by_type(lhs_before, rhs_before, MW_OCHAIRE);
      if (ret != 0) return ret;

      /* COMPOUND_PARTよりはCOMPOUND_HEADを優先 */
      ret = cmp_node_by_type_to_type(lhs_before, rhs_before, MW_COMPOUND_HEAD, MW_COMPOUND_PART);
      if (ret != 0) return ret;
    } else {
      break;
    }
    lhs_before = lhs_before->before_node;
    rhs_before = rhs_before->before_node;
  }

  /* 最後に遷移確率を見る */
  assert(lhs);
  assert(rhs);
  if (lhs->adjusted_probability > rhs->adjusted_probability) {
    return 1;
  } else if (lhs->adjusted_probability < rhs->adjusted_probability) {
    return -1;
  } else {
    return 0;
  }
}

/*
 * 構成中のラティスにノードを追加する
 */
static void
push_node(struct lattice_info* info, struct lattice_node* new_node,
	  int position)
{
  struct lattice_node* node;
  struct lattice_node* previous_node = NULL;

  if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
    print_lattice_node(info, new_node);
  }

  assert(position >= 0);
  if (position >= info->lattice_node_len) {
    anthy_log(0, "info->lattice_node_list[%d] is not allocated\n", position);
    return;
  }
  /* 先頭のnodeが無ければ無条件に追加 */
  node = info->lattice_node_list[position].head;
  if (!node) {
    info->lattice_node_list[position].head = new_node;
    info->lattice_node_list[position].nr_nodes++;
    return;
  }

  while (node->next) {
    /* 余計なノードを追加しないための枝刈り */
    if (new_node->seg_class == node->seg_class &&
	new_node->border == node->border) {
      /* segclassが同じで、始まる位置が同じなら */
      switch (cmp_node(new_node, node)) {
      case 0:
      case 1:
	/* 新しい方が確率が大きいか学習によるものなら、古いのと置き換え*/
	if (previous_node) {
	  previous_node->next = new_node;
	} else {
	  info->lattice_node_list[position].head = new_node;
	}
	new_node->next = node->next;
	release_lattice_node(info, node);
	break;
      case -1:
	/* そうでないなら削除 */
	release_lattice_node(info, new_node);
	break;
      }
      return;
    }
    previous_node = node;
    node = node->next;
  }

  /* 最後のノードの後ろに追加 */
  node->next = new_node;
  info->lattice_node_list[position].nr_nodes++;
}

/* 一番確率の低いノードを消去する*/
static void
remove_min_node(struct lattice_info *info, struct node_list_head *node_list)
{
  struct lattice_node* node = node_list->head;
  struct lattice_node* previous_node = NULL;
  struct lattice_node* min_node = node;
  struct lattice_node* previous_min_node = NULL;

  if (!min_node) {
      anthy_log(0, "No min_node\n");
      return;
  }
  /* 一番確率の低いノードを探す */
  while (node) {
    if (cmp_node(node, min_node) < 0) {
      previous_min_node = previous_node;
      min_node = node;
    }
    previous_node = node;
    node = node->next;
  }

  /* 一番確率の低いノードを削除する */
  if (previous_min_node) {
    previous_min_node->next = min_node->next;
  } else {
    node_list->head = min_node->next;
  }
  release_lattice_node(info, min_node);
  node_list->nr_nodes--;
}

/* いわゆるビタビアルゴリズムを使用して経路を選ぶ */
static void
choose_path(struct lattice_info* info, int to)
{
  /* 最後まで到達した遷移のなかで一番確率の大きいものを選ぶ */
  struct lattice_node* node;
  struct lattice_node* best_node = NULL;
  int last = to;
  if (last >= info->lattice_node_len) {
    anthy_log(0, "info->lattice_node_list[%d] is not allocated\n", last);
    return;
  }
  while (!info->lattice_node_list[last].head) {
    /* 最後の文字まで遷移していなかったら後戻り */
    --last;
  }
  for (node = info->lattice_node_list[last].head; node; node = node->next) {
    if (cmp_node(node, best_node) > 0) {
      best_node = node;
    }
  }
  if (!best_node) {
    return;
  }

  /* 遷移を逆にたどりつつ文節の切れ目を記録 */
  node = best_node;
  if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
    printf("choose_path()\n");
  }
  while (node->before_node) {
    info->sc->word_split_info->best_seg_class[node->border] =
      node->seg_class;
    anthy_mark_border_by_metaword(info->sc, node->mw);
    /**/
    if (anthy_splitter_debug_flags() & SPLITTER_DEBUG_LN) {
      print_lattice_node(info, node);
    }
    /**/
    node = node->before_node;
  }
}

static void
build_graph(struct lattice_info* info, int from, int to)
{
  int i;
  struct lattice_node* node;
  struct lattice_node* left_node;

  /* 始点となるノードを追加 */
  node = alloc_lattice_node(info, NULL, NULL, from);
  push_node(info, node, from);

  /* info->lattice_node_list[index]にはindexまでの遷移が入っているのであって、
   * indexからの遷移が入っているのではない
   */

  if (to >= info->lattice_node_len || from < 0) {
    anthy_log(0, "info->lattice_node_list[%d] is not allocated: %d\n",
    to, from);
    return;
  }
  /* 全ての遷移を左から試す */
  for (i = from; i < to; ++i) {
    for (left_node = info->lattice_node_list[i].head; left_node;
	 left_node = left_node->next) {
      struct meta_word *mw;
      /* i文字目に到達するlattice_nodeのループ */

      for (mw = info->sc->word_split_info->cnode[i].mw; mw; mw = mw->next) {
	int position;
	struct lattice_node* new_node;
	/* i文字目からのmeta_wordのループ */

	if (mw->can_use != ok) {
	  continue; /* 決められた文節の区切りをまたぐmetawordは使わない */
	}
	position = i + mw->len;
	new_node = alloc_lattice_node(info, left_node, mw, i);
	push_node(info, new_node, position);

	/* 解の候補が多すぎたら、確率の低い方から削る */
	if (info->lattice_node_list[position].nr_nodes >= NODE_MAX_SIZE) {
	  remove_min_node(info, &info->lattice_node_list[position]);
	}
      }
    }
  }

  /* 文末補正 */
  for (node = info->lattice_node_list[to].head; node; node = node->next) {
    struct feature_list features;
    anthy_feature_list_init(&features);
    build_feature_list(NULL, &features);
    node->adjusted_probability = node->adjusted_probability *
      calc_probability(SEG_TAIL, &features);
    anthy_feature_list_free(&features);
  }
}

void
anthy_mark_borders(struct splitter_context *sc, int from, int to)
{
  struct lattice_info* info = alloc_lattice_info(sc, to);
  trans_info_array = anthy_file_dic_get_section("trans_info");
  build_graph(info, from, to);
  choose_path(info, to);
  release_lattice_info(info);
}
