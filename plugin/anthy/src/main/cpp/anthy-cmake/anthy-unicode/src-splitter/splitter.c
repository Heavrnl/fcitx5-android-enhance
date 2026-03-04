/*
 * 文を文節にsplitするsplitter
 *
 * 文節の境界を検出する
 *  anthy_init_split_context() 分割用のコンテキストを作って
 *  anthy_mark_border() 分割をして
 *  anthy_release_split_context() コンテキストを解放する
 *
 *  anthy_commit_border() コミットされた内容に対して学習をする
 *
 * Funded by IPA未踏ソフトウェア創造事業 2001 9/22
 *
 * Copyright (C) 2004 YOSHIDA Yuichi
 * Copyright (C) 2000-2004 TABATA Yusuke
 * Copyright (C) 2000-2001 UGAWA Tomoharu
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 *
 * $Id: splitter.c,v 1.48 2002/11/18 11:39:18 yusuke Exp $
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
#include <string.h>

#include <anthy/alloc.h>
#include <anthy/record.h>
#include <anthy/splitter.h>
#include <anthy/logger.h>
#include "wordborder.h"

#define MAX_EXPAND_PAIR_ENTRY_COUNT 1000

static int splitter_debug_flags;

/**/
wtype_t anthy_wtype_noun;
wtype_t anthy_wtype_name_noun;
wtype_t anthy_wtype_num_noun;
wtype_t anthy_wtype_prefix;
wtype_t anthy_wtype_num_prefix;
wtype_t anthy_wtype_num_postfix;
wtype_t anthy_wtype_name_postfix;
wtype_t anthy_wtype_sv_postfix;
wtype_t anthy_wtype_a_tail_of_v_renyou;
wtype_t anthy_wtype_v_renyou;
wtype_t anthy_wtype_noun_tail;/* いれ「たて」とか */
wtype_t anthy_wtype_n1;
wtype_t anthy_wtype_n10;


/** make_word_cacheで作成した文節情報を解放する
 */
static void
release_info_cache(struct splitter_context *sc)
{
  struct word_split_info_cache *info = sc->word_split_info;

  anthy_free_allocator(info->MwAllocator);
  anthy_free_allocator(info->WlAllocator);
  free(info->cnode);
  free(info->seq_len);
  free(info->rev_seq_len);
  free(info);
}

static void
metaword_dtor(void *p)
{
  struct meta_word *mw = (struct meta_word*)p;
  if (mw->cand_hint.str) {
    free(mw->cand_hint.str);
  }
}


static void
alloc_char_ent(xstr *xs, struct splitter_context *sc)
{
  int i;

  sc->char_count = xs->len;
  sc->ce = (struct char_ent*)
    malloc(sizeof(struct char_ent)*(xs->len + 1));
  if (!sc->ce) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  for (i = 0; i <= xs->len; i++) {
    sc->ce[i].c = &xs->str[i];
    sc->ce[i].seg_border = 0;
    sc->ce[i].initial_seg_len = 0;
    sc->ce[i].best_seg_class = SEG_HEAD;
    sc->ce[i].best_mw = NULL;
  }

  /* 左右両端は文節の境界である */
  sc->ce[0].seg_border = 1;
  sc->ce[xs->len].seg_border = 1;
}

/*  ここで確保した内容はrelease_info_cacheで解放される
 */
static void
alloc_info_cache(struct splitter_context *sc)
{
  int i;
  struct word_split_info_cache *info;

  /* キャッシュのデータを確保 */
  sc->word_split_info = malloc(sizeof(struct word_split_info_cache));
  info = sc->word_split_info;
  info->MwAllocator = anthy_create_allocator(sizeof(struct meta_word), metaword_dtor);
  info->WlAllocator = anthy_create_allocator(sizeof(struct word_list), 0);
  info->cnode =
    malloc(sizeof(struct char_node) * (sc->char_count + 1));

  info->seq_len = malloc(sizeof(int) * (sc->char_count + 1));
  info->rev_seq_len = malloc(sizeof(int) * (sc->char_count + 1));

  /* 各文字インデックスに対して初期化を行う */
  for (i = 0; i <= sc->char_count; i++) {
    info->seq_len[i] = 0;
    info->rev_seq_len[i] = 0;
    info->cnode[i].wl = NULL;
    info->cnode[i].mw = NULL;
    info->cnode[i].max_len = 0;
  }
}

/** 外から呼び出されるwordsplitterのトップレベルの関数 */
void
anthy_mark_border(struct splitter_context *sc,
		  int from, int from2, int to)
{
  int i;
  struct word_split_info_cache *info;

  /* sanity check */
  if ((to - from) <= 0) {
    return ;
  }

  /* 境界マーク用とlatticeの検索で用いられるクラス用の領域を確保 */
  info = sc->word_split_info;
  info->seg_border = alloca(sizeof(int)*(sc->char_count + 1));
  info->best_seg_class = alloca(sizeof(enum seg_class)*(sc->char_count + 1));
  info->best_mw = alloca(sizeof(struct meta_word*)*(sc->char_count + 1));
  for (i = 0; i < sc->char_count + 1; ++i) {
    info->seg_border[i] = sc->ce[i].seg_border;
    info->best_seg_class[i] = sc->ce[i].best_seg_class;
    info->best_mw[i] = sc->ce[i].best_mw;
  }

  /* 境界を決定する */
  anthy_eval_border(sc, from, from2, to);

  for (i = from; i < to; ++i) {
    sc->ce[i].seg_border = info->seg_border[i];
    sc->ce[i].best_seg_class = info->best_seg_class[i];
    sc->ce[i].best_mw = info->best_mw[i];
  }
}

/* 文節が拡大されたので，それを学習する */
static void
proc_expanded_segment(struct splitter_context *sc,
		      int from, int len)
{
  int initial_len = sc->ce[from].initial_seg_len;
  int i, nr;
  xstr from_xs, to_xs, *xs;

  from_xs.str = sc->ce[from].c;
  from_xs.len = initial_len;
  to_xs.str = sc->ce[from].c;
  to_xs.len = len;
  if (anthy_select_section("EXPANDPAIR", 1) == -1) {
    return ;
  }
  if (anthy_select_row(&from_xs, 1) == -1) {
    return ;
  }
  nr = anthy_get_nr_values();
  for (i = 0; i < nr; i ++) {
    xs = anthy_get_nth_xstr(i);
    if (!xs || !anthy_xstrcmp(xs, &to_xs)) {
      /* 既にある */
      return ;
    }
  }
  anthy_set_nth_xstr(nr, &to_xs);
  anthy_truncate_section(MAX_EXPAND_PAIR_ENTRY_COUNT);
}

/* 文節のマージと語尾を学習する */
void
anthy_commit_border(struct splitter_context *sc, int nr_segments,
		    struct meta_word **mw, int *seg_len)
{
  int i, from = 0;

  /* 伸ばした文節 */
  for (i = 0; i < nr_segments; i++) {
    /* それぞれの文節に対して */

    int len = seg_len[i];
    int initial_len = sc->ce[from].initial_seg_len;
    int real_len = 0;
    int l2;

    if (!initial_len || from + initial_len == sc->char_count) {
      /* そこは境界ではない */
      goto tail;
    }
    l2 = sc->ce[from + initial_len].initial_seg_len;
    if (initial_len + l2 > len) {
      /* 隣の文節を含むほど拡大されたわけではない */
      goto tail;
    }
    if (mw[i]) {
      real_len = mw[i]->len;
    }
    if (real_len <= initial_len) {
      goto tail;
    }
    /* 右の文節を含む長さに拡張された文節がコミットされた */
    proc_expanded_segment(sc, from, real_len);
  tail:
    from += len;
  }
}

int
anthy_splitter_debug_flags(void)
{
  return splitter_debug_flags;
}

void
anthy_init_split_context(xstr *xs, struct splitter_context *sc, int is_reverse)
{
  alloc_char_ent(xs, sc);
  alloc_info_cache(sc);
  sc->is_reverse = is_reverse;
  /* 全ての部分文字列をチェックして、文節の候補を列挙する
     word_listを構成してからmetawordを構成する */
  anthy_lock_dic();
  anthy_make_word_list_all(sc);
  anthy_unlock_dic();
  anthy_make_metaword_all(sc);

}

void
anthy_release_split_context(struct splitter_context *sc)
{
  if (sc->word_split_info) {
    release_info_cache(sc);
    sc->word_split_info = 0;
  }
  if (sc->ce) {
    free(sc->ce);
    sc->ce = 0;
  }
}

/** splitter全体の初期化を行う */
int
anthy_init_splitter(void)
{
  /* デバッグプリントの設定 */
  char *en = getenv("ANTHY_ENABLE_DEBUG_PRINT");
  char *dis = getenv("ANTHY_DISABLE_DEBUG_PRINT");
  splitter_debug_flags = SPLITTER_DEBUG_NONE;
  if (!dis && en && strlen(en)) {
    char *fs = getenv("ANTHY_SPLITTER_PRINT");
    if (fs) {
      if (strchr(fs, 'w')) {
	splitter_debug_flags |= SPLITTER_DEBUG_WL;
      }
      if (strchr(fs, 'm')) {
	splitter_debug_flags |= SPLITTER_DEBUG_MW;
      }
      if (strchr(fs, 'l')) {
	splitter_debug_flags |= SPLITTER_DEBUG_LN;
      }
      if (strchr(fs, 'i')) {
	splitter_debug_flags |= SPLITTER_DEBUG_ID;
      }
      if (strchr(fs, 'c')) {
	splitter_debug_flags |= SPLITTER_DEBUG_CAND;
      }
    }
  }
  /* 付属語グラフの初期化 */
  if (anthy_init_depword_tab()) {
    anthy_log(0, "Failed to init dependent word table.\n");
    anthy_quit_depword_tab();
    return -1;
  }
  /**/
  anthy_wtype_noun = anthy_init_wtype_by_name("名詞35");
  anthy_wtype_name_noun = anthy_init_wtype_by_name("人名");
  anthy_wtype_num_noun = anthy_init_wtype_by_name("数詞");
  anthy_wtype_a_tail_of_v_renyou = anthy_init_wtype_by_name("形容詞化接尾語");
  anthy_wtype_v_renyou = anthy_init_wtype_by_name("動詞連用形");
  anthy_wtype_noun_tail = anthy_init_wtype_by_name("名詞化接尾語");
  anthy_wtype_prefix = anthy_init_wtype_by_name("名詞接頭辞");
  anthy_wtype_num_prefix = anthy_init_wtype_by_name("数接頭辞");
  anthy_wtype_num_postfix = anthy_init_wtype_by_name("数接尾辞");
  anthy_wtype_name_postfix = anthy_init_wtype_by_name("人名接尾辞");
  anthy_wtype_sv_postfix = anthy_init_wtype_by_name("サ変接尾辞");
  anthy_wtype_n1 = anthy_init_wtype_by_name("数詞1");
  anthy_wtype_n10 = anthy_init_wtype_by_name("数詞10");
  return 0;
}

void
anthy_quit_splitter(void)
{
  anthy_quit_depword_tab();
}
