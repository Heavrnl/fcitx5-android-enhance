/*
 * Comments in this program are written in Japanese,
 * because this program is a Japanese input method.
 * (many Japanese gramatical terms will appear.)
 *
 * Kana-Kanji conversion engine Anthy.
 * 仮名漢字変換エンジンAnthy(アンシー)
 *
 * Funded by IPA未踏ソフトウェア創造事業 2001 9/22
 * Funded by IPA未踏ソフトウェア創造事業 2005
 * Copyright (C) 2000-2007 TABATA Yusuke, UGAWA Tomoharu
 * Copyright (C) 2004-2006 YOSHIDA Yuichi
 * Copyright (C) 2000-2007 KMC(Kyoto University Micro Computer Club)
 * Copyright (C) 2001-2002 TAKAI Kosuke, Nobuoka Takahiro
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
 * Anthyの変換機能はライブラリとして構成されており、この
 * ファイルにはライブラリの提供する関数(API)が記述されています。
 *
 * ライブラリの提供する関数は下記のようなものがあります
 * (1)ライブラリ全体の初期化、終了、設定
 * (2)変換コンテキストの作成、解放
 * (3)変換コンテキストに対する文字列の設定、文節長の変更、候補の取得等
 *
 * インターフェイスに関しては doc/LIBを参照してください
 * Anthyのコードを理解しようとする場合は
 * doc/GLOSSARY で用語を把握することを勧めます
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <anthy/dic.h>
#include <anthy/splitter.h>
#include <anthy/conf.h>
#include <anthy/ordering.h>
#include <anthy/logger.h>
#include <anthy/record.h>
#include <anthy/anthy.h>
#include <anthy/record.h>
#include <anthy/xchar.h> /* for KK_VU */
#include "main.h"
#include "config.h"


/** Anthyの初期化が完了したかどうかのフラグ */
static int is_init_ok;
/** コンテキスト生成時のエンコーディング */
static int default_encoding;
/***/
static char *history_file;

/** (API) 全体の初期化 */
int
anthy_init(void)
{
  char *hfn;
  if (is_init_ok) {
    /* 2度初期化しないように */
    return 0;
  }

  /* 各サブシステムを順に初期化する */
  if (anthy_init_dic()) {
    anthy_log(0, "Failed to initialize dictionary.\n");
    return -1;
  }

  if (anthy_init_splitter()) {
    anthy_log(0, "Failed to init splitter.\n");
    return -1;
  }
  anthy_init_contexts();
  anthy_init_personality();
  anthy_infosort_init();
  anthy_relation_init();

  /**/
  default_encoding = ANTHY_EUC_JP_ENCODING;
  is_init_ok = 1;
  history_file = NULL;
  hfn = getenv("ANTHY_HISTORY_FILE");
  if (hfn) {
    history_file = strdup(hfn);
  }

  /**/
  return 0;
}

/** (API) 全データの解放 */
void
anthy_quit(void)
{
  if (!is_init_ok) {
    return ;
  }
  anthy_quit_contexts();
  anthy_quit_personality();
  anthy_quit_splitter();
  /* 多くのデータ構造はここでallocatorによって解放される */
  anthy_quit_dic();

  is_init_ok = 0;
  /**/
  if (history_file) {
    free(history_file);
  }
  history_file = NULL;
}

/** (API) 設定項目の上書き */
void
anthy_conf_override(const char *var, const char *val)
{
  anthy_do_conf_override(var, val);
}

/** (API) personalityの設定 */
int
anthy_set_personality(const char *id)
{
  return anthy_do_set_personality(id);
}

/** (API) 変換contextの作成 */
struct anthy_context *
anthy_create_context(void)
{
  if (!is_init_ok) {
    return 0;
  }
  return anthy_do_create_context(default_encoding);
}

/** (API) 変換contextのリセット */
void
anthy_reset_context(struct anthy_context *ac)
{
  anthy_do_reset_context(ac);
}

/** (API) 変換contextの解放 */
void
anthy_release_context(struct anthy_context *ac)
{
  anthy_do_release_context(ac);
}

/**
 * 再変換が必要かどうかの判定
 */
static int
need_reconvert(struct anthy_context *ac, xstr *xs)
{
  int i;

  if (ac->reconversion_mode == ANTHY_RECONVERT_ALWAYS) {
    return 1;
  }
  if (ac->reconversion_mode == ANTHY_RECONVERT_DISABLE) {
    return 0;
  }

  for (i = 0; i < xs->len; ++i) {
    xchar xc = xs->str[i];
    int type = anthy_get_xchar_type(xc);

    /* これらの文字種の場合は逆変換する
     * 「ヴ」はフロントエンドが平仮名モードの文字列として送ってくるので、
     * 逆変換の対象とはしない
     */
    if (!(type & (XCT_HIRA | XCT_SYMBOL | XCT_NUM |
		  XCT_WIDENUM | XCT_OPEN | XCT_CLOSE |
		  XCT_ASCII)) &&
	xc != KK_VU) {
      return 1;
    }
  }
  return 0;
}


/** (API) 変換文字列の設定 */
int
anthy_set_string(struct anthy_context *ac, const char *s)
{
  xstr *xs;
  int retval = 0;

  if (!ac) {
    return -1;
  }

  /*初期化*/
  anthy_do_reset_context(ac);

  /* 辞書セッションの開始 */
  if (!ac->dic_session) {
    ac->dic_session = anthy_dic_create_session();
    if (!ac->dic_session) {
      return -1;
    }
  }

  anthy_dic_activate_session(ac->dic_session);
  /* 変換を開始する前に個人辞書をreloadする */
  anthy_reload_record();

  xs = anthy_cstr_to_xstr(s, ac->encoding);
  /**/
  if (!need_reconvert(ac, xs)) {
    /* 普通に変換する */
    retval |= anthy_do_context_set_str(ac, xs, 0);
  } else {
    /* 漢字やカタカナが混じっていたら再変換してみる */
    struct anthy_conv_stat stat;
    struct seg_ent *seg;
    int i;
    xstr* hira_xs;
    /* 与えられた文字列に変換をかける */
    retval |= anthy_do_context_set_str(ac, xs, 1);

    /* 各文節の第一候補を取得して平仮名列を得る */
    anthy_get_stat(ac, &stat);
    hira_xs = NULL;
    for (i = 0; i < stat.nr_segment; ++i) {
      seg = anthy_get_nth_segment(&ac->seg_list, i);
      hira_xs = anthy_xstrcat(hira_xs, &seg->cands[0]->str);
    }
    /* 改めて変換を行なう */
    anthy_release_segment_list(ac);
    retval |= anthy_do_context_set_str(ac, hira_xs, 0);
    anthy_free_xstr(hira_xs);
  }

  anthy_free_xstr(xs);
  return retval;
}

/** (API) 文節長の変更 */
void
anthy_resize_segment(struct anthy_context *ac, int nth, int resize)
{
  anthy_dic_activate_session(ac->dic_session);
  anthy_do_resize_segment(ac, nth, resize);
}

/** (API) 変換の状態の取得 */
int
anthy_get_stat(struct anthy_context *ac, struct anthy_conv_stat *s)
{
  s->nr_segment = ac->seg_list.nr_segments;
  return 0;
}

/** (API) 文節の状態の取得 */
int
anthy_get_segment_stat(struct anthy_context *ac, int n,
		       struct anthy_segment_stat *s)
{
  struct seg_ent *seg;
  seg = anthy_get_nth_segment(&ac->seg_list, n);
  if (seg) {
    s->nr_candidate = seg->nr_cands;
    s->seg_len = seg->str.len;
    return 0;
  }
  return -1;
}

static int
get_special_candidate_index(int nth, struct seg_ent *seg)
{
  int i;
  int mask = XCT_NONE;
  if (nth >= 0) {
    return nth;
  }
  if (nth == NTH_UNCONVERTED_CANDIDATE ||
      nth == NTH_HALFKANA_CANDIDATE) {
    return nth;
  }
  if (nth == NTH_KATAKANA_CANDIDATE) {
    mask = XCT_KATA;
  } else if (nth == NTH_HIRAGANA_CANDIDATE) {
    mask = XCT_HIRA;
  }
  for (i = 0; i < seg->nr_cands; i++) {
    if (anthy_get_xstr_type(&seg->cands[i]->str) & mask) {
      return i;
    }
  }
  return NTH_UNCONVERTED_CANDIDATE;
}

/** (API) 文節の取得 */
int
anthy_get_segment(struct anthy_context *ac, int nth_seg,
		  int nth_cand, char *buf, int buflen)
{
  struct seg_ent *seg;
  char *p;
  int len;

  /* 文節を取り出す */
  if (nth_seg < 0 || nth_seg >= ac->seg_list.nr_segments) {
    return -1;
  }
  seg = anthy_get_nth_segment(&ac->seg_list, nth_seg);

  /* 文節から候補を取り出す */
  p = NULL;
  if (nth_cand < 0) {
    nth_cand = get_special_candidate_index(nth_cand, seg);
  }
  if (nth_cand == NTH_HALFKANA_CANDIDATE) {
    xstr *xs = anthy_xstr_hira_to_half_kata(&seg->str);
    p = anthy_xstr_to_cstr(xs, ac->encoding);
    anthy_free_xstr(xs);
  } else if (nth_cand == NTH_UNCONVERTED_CANDIDATE) {
    /* 変換前の文字列を取得する */
    p = anthy_xstr_to_cstr(&seg->str, ac->encoding);
  } else if (nth_cand >= 0 && nth_cand < seg->nr_cands) {
    p = anthy_xstr_to_cstr(&seg->cands[nth_cand]->str, ac->encoding);
  }
  if (!p) {
    return -1;
  }

  /* バッファに書き込む */
  len = strlen(p);
  if (!buf) {
    free(p);
    return len;
  }
  if (len + 1 > buflen) {
    /* バッファが足りません */
    free(p);
    return -1;
  }
  strcpy(buf, p);
  free(p);
  return len;
}

/* すべての文節がコミットされたかcheckする */
static int
commit_all_segment_p(struct anthy_context *ac)
{
  int i;
  struct seg_ent *se;
  for (i = 0; i < ac->seg_list.nr_segments; i++) {
    se = anthy_get_nth_segment(&ac->seg_list, i);
    if (se->committed < 0) {
      return 0;
    }
  }
  return 1;
}

/** (API) 文節の確定 */
int
anthy_commit_segment(struct anthy_context *ac, int s, int c)
{
  struct seg_ent *seg;
  if (!ac->str.str) {
    return -1;
  }
  if (s < 0 || s >= ac->seg_list.nr_segments) {
    return -1;
  }
  if (commit_all_segment_p(ac)) {
    /* すでに全てのセグメントがコミットされている */
    return -1;
  }

  anthy_dic_activate_session(ac->dic_session);
  seg = anthy_get_nth_segment(&ac->seg_list, s);
  if (c < 0) {
    c = get_special_candidate_index(c, seg);
  }
  if (c == NTH_UNCONVERTED_CANDIDATE) {
    /*
     * 変換前の文字列がコミットされたので，それに対応する候補の番号を探す
     */
    int i;
    for (i = 0; i < seg->nr_cands; i++) {
      if (!anthy_xstrcmp(&seg->str, &seg->cands[i]->str)) {
	c = i;
      }
    }
  }
  if (c < 0 || c >= seg->nr_cands) {
    return -1;
  }
  seg->committed = c;

  if (commit_all_segment_p(ac)) {
    /* 今、すべてのセグメントがコミットされた */
    anthy_proc_commit(&ac->seg_list, &ac->split_info);
    /**/
    anthy_save_history(history_file, ac);
  }
  return 0;
}

/** (API) 予測してほしい文字列の設定 */
int
anthy_set_prediction_string(struct anthy_context *ac, const char* s)
{
  int retval;
  xstr *xs;

  anthy_dic_activate_session(ac->dic_session);
  /* 予測を開始する前に個人辞書をreloadする */
  anthy_reload_record();


  xs = anthy_cstr_to_xstr(s, ac->encoding);

  retval = anthy_do_set_prediction_str(ac, xs);

  anthy_free_xstr(xs);

  return retval;
}

/** (API) 予測変換の状態の取得 */
int
anthy_get_prediction_stat(struct anthy_context *ac, struct anthy_prediction_stat * ps)
{
  ps->nr_prediction = ac->prediction.nr_prediction;
  return 0;
}

/** (API) 予測変換の候補の取得 */
int
anthy_get_prediction(struct anthy_context *ac, int nth, char* buf, int buflen)
{
  struct prediction_cache* prediction = &ac->prediction;
  int nr_prediction = prediction->nr_prediction;
  char* p;
  int len;

  if (nth < 0 || nr_prediction <= nth) {
    return -1;
  }

  p = anthy_xstr_to_cstr(prediction->predictions[nth].str, ac->encoding);

  /* バッファに書き込む */
  len = strlen(p);
  if (!buf) {
    free(p);
    return len;
  }
  if (len + 1 > buflen) {
    free(p);
    return -1;
  } else {
    strcpy(buf, p);
    free(p);
    return len;
  }
}

/** (API) 予測の結果を確定する
 */
int
anthy_commit_prediction(struct anthy_context *ac, int nth)
{
  struct prediction_cache* pc = &ac->prediction;
  if (nth < 0 || nth >= pc->nr_prediction) {
    return -1;
  }
  anthy_do_commit_prediction(pc->predictions[nth].src_str,
			     pc->predictions[nth].str);
  return 0;
}

/** (API) 開発用 */
void
anthy_print_context(struct anthy_context *ac)
{
  anthy_do_print_context(ac, ac->encoding);
}

/** (API) Anthy ライブラリのバージョンを表す文字列を返す
 * 共有ライブラリでは外部変数のエクスポートは好ましくないので関数にしてある
 */
const char *
anthy_get_version_string (void)
{
#ifdef VERSION
  return VERSION;
#else  /* just in case */
  return "(unknown)";
#endif
}

/** (API) */
int
anthy_context_set_encoding(struct anthy_context *ac, int encoding)
{
  if (!ac) {
    return ANTHY_EUC_JP_ENCODING;
  }
  if (encoding == ANTHY_UTF8_ENCODING ||
      encoding == ANTHY_EUC_JP_ENCODING) {
    ac->encoding = encoding;
  }
  return ac->encoding;
}

/** (API) */
int
anthy_set_reconversion_mode(anthy_context_t ac, int mode)
{
  if (!ac) {
    return ANTHY_RECONVERT_AUTO;
  }
  if (mode == ANTHY_RECONVERT_AUTO ||
      mode == ANTHY_RECONVERT_DISABLE ||
      mode == ANTHY_RECONVERT_ALWAYS) {
    ac->reconversion_mode = mode;
  }
  return ac->reconversion_mode;
}
