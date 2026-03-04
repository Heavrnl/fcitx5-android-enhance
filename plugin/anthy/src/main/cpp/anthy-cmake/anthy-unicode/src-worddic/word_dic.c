/*
 * Anthyの辞書ライブラリの中心
 *
 * anthy_get_seq_ent_from_xstr()で辞書をひく
 *
 * Copyright (C) 2000-2007 TABATA Yusuke
 * Copyright (C) 2005-2006 YOSHIDA Yuichi
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
#include <stdlib.h>
#include <string.h>

#include <anthy/anthy.h>
#include <anthy/dic.h>
#include <anthy/conf.h>
#include <anthy/record.h>
#include <anthy/alloc.h>
#include <anthy/logger.h>
#include <anthy/xchar.h>
#include <anthy/feature_set.h>
#include <anthy/textdict.h>

#include <anthy/diclib.h>

#include "dic_ent.h"
#include "dic_personality.h"
#include "dic_main.h"

/**/
static int dic_init_count;

/* 辞書 */
/* 全personalityで共有されるファイル辞書 */
static struct word_dic *main_dic_file;

/* 各パーソナリティごとの辞書 */
struct mem_dic *anthy_current_personal_dic_cache;/* キャッシュ */
/**/
struct record_stat *anthy_current_record;

struct seq_ent *
anthy_validate_seq_ent(struct seq_ent *seq, xstr *xs, int is_reverse)
{
  if (!seq) {
    return NULL;
  }
  if (seq->nr_dic_ents == 0 && seq->nr_compound_ents == 0) {
    /* 無効なエントリを作成したのでcacheから削除 */
    anthy_mem_dic_release_seq_ent(anthy_current_personal_dic_cache,
				  xs, is_reverse);
    return NULL;
  }

  return seq;
}

struct seq_ent *
anthy_cache_get_seq_ent(xstr *xs, int is_reverse)
{
  struct seq_ent *seq;

  /* キャッシュ中に既にあればそれを返す */
  seq = anthy_mem_dic_find_seq_ent_by_xstr(anthy_current_personal_dic_cache,
					   xs, is_reverse);
  if (seq) {
    return seq;
  }

  /* キャッシュ中に無いので確保 */
  return anthy_mem_dic_alloc_seq_ent_by_xstr(anthy_current_personal_dic_cache,
					     xs, is_reverse);
}

int
anthy_dic_check_word_relation(int from, int to)
{
  return anthy_word_dic_check_word_relation(main_dic_file, from, to);
}

static seq_ent_t
do_get_seq_ent_from_xstr(xstr *xs, int is_reverse)
{
  struct seq_ent *seq;
  /* キャッシュから取り出す */
  seq = anthy_cache_get_seq_ent(xs, is_reverse);
  seq = anthy_validate_seq_ent(seq, xs, is_reverse);
  if (!seq) {
    /* 数字などの辞書に無い文字列を検索する */
    return anthy_get_ext_seq_ent_from_xstr(xs, is_reverse);
  }
  return seq;
}

static xstr *
convert_vu(xstr *xs)
{
  int i, v = 0;
  int j;

    /* 「ヴ」の出現を数える */
  for (i = 0; i < xs->len; i++) {
    if (xs->str[i] == KK_VU) {
      v++;
    }
  }
  if (v > 0) {
    xstr *nx = malloc(sizeof(xstr));
    if (!nx) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return NULL;
    }
    nx->len = xs->len + v;
    nx->str = malloc(sizeof(xchar)*nx->len);
    if (!nx->str) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      free(nx);
      return NULL;
    }
    j = 0;
    /* 「ヴ」を「う゛」に変換しつつコピーする */
    for (i = 0; i < xs->len; i++) {
      if (xs->str[i] == KK_VU) {
	nx->str[j] = HK_U;
	j++;
	nx->str[j] = HK_DDOT;
	j++;
      } else {
	nx->str[j] = xs->str[i];
	j++;
      }
    }
    return nx;
  }
  return NULL;
}

seq_ent_t
anthy_get_seq_ent_from_xstr(xstr *xs, int is_reverse)
{
  struct seq_ent *se;

  if (!xs) {
    return NULL;
  }
  if (!is_reverse) {
    xstr *nx = convert_vu(xs);
    /* 「ヴ」の混ざった順変換の場合、「う゛」に直して検索する
     *   上位のレイヤーではユーザの与えた文字列をそのまま保持することが
     *   期待されるので、変換はここで行なう。
     */
    if (nx) {
      se = do_get_seq_ent_from_xstr(nx, 0);
      anthy_free_xstr(nx);
      return se;
    }
  }
  /* 「ヴ」が出現しない、もしくは逆変換の場合 */
  return do_get_seq_ent_from_xstr(xs, is_reverse);
}

static void
gang_elm_dtor(void *p)
{
  struct gang_elm *ge = p;
  free(ge->key);
}

static int
find_gang_elm(allocator ator, struct gang_elm *head, xstr *xs)
{
  char *str = anthy_xstr_to_cstr(xs, ANTHY_UTF8_ENCODING);
  struct gang_elm *ge;
  for (ge = head->tmp.next; ge; ge = ge->tmp.next) {
    if (!strcmp(ge->key, str)) {
      free(str);
      return 0;
    }
  }
  ge = anthy_smalloc(ator);
  ge->xs = *xs;
  ge->key = str;
  ge->tmp.next = head->tmp.next;
  head->tmp.next = ge;
  return 1;
}

static int
gang_elm_compare_func(const void *p1, const void *p2)
{
  const struct gang_elm * const *s1 = p1;
  const struct gang_elm * const *s2 = p2;
  return strcmp((*s1)->key, (*s2)->key);
}

struct gang_scan_context {
  /**/
  int nr;
  struct gang_elm **array;
  /**/
  int nth;
};

static int
is_ext_ent(struct seq_ent *seq)
{
  if (!seq->md) {
    return 1;
  }
  return 0;
}

static void
scan_misc_dic(struct gang_elm **array, int nr, int is_reverse)
{
  int i;
  for (i = 0; i < nr; i++) {
    xstr *xs = &array[i]->xs;
    struct seq_ent *seq;
    seq = anthy_cache_get_seq_ent(xs, is_reverse);
    /* 個人辞書からの取得(texttrie(旧形式)と未知語辞書) */
    if (seq) {
      anthy_copy_words_from_private_dic(seq, xs, is_reverse);
      anthy_validate_seq_ent(seq, xs, is_reverse);
    }
  }
}

static void
load_word(xstr *xs, const char *n, int is_reverse)
{
  struct seq_ent *seq = anthy_get_seq_ent_from_xstr(xs, 0);
  xstr *word_xs;
  wtype_t wt;
  struct word_line wl;
  if (!seq || is_ext_ent(seq)) {
    seq = anthy_mem_dic_alloc_seq_ent_by_xstr(anthy_current_personal_dic_cache,
					      xs, is_reverse);
  }
  if (anthy_parse_word_line(n, &wl)) {
    return ;
  }
  word_xs = anthy_cstr_to_xstr(wl.word, ANTHY_UTF8_ENCODING);
  if (anthy_type_to_wtype(wl.wt, &wt)) {
    anthy_mem_dic_push_back_dic_ent(seq, 0, word_xs, wt,
				    NULL, wl.freq, 0);
  }

  anthy_free_xstr(word_xs);
}

static int
gang_scan(void *p, int offset, const char *key, const char *n)
{
  struct gang_scan_context *gsc = p;
  struct gang_elm *elm;
  int r;
  (void)offset;
  while (1) {
    if (gsc->nth >= gsc->nr) {
      return 0;
    }
    elm = gsc->array[gsc->nth];
    r = strcmp(elm->key, key);
    if (r == 0) {
      /* find it */
      load_word(&elm->xs, n, 0);
      /* go next in dictionary */
      return 0;
    } else if (r > 0) {
      /* go next in dictionary */
      return 0;
    } else {
      /* go next in lookup */
      gsc->nth ++;
    }
  }
  return 0;
}

static void
scan_dict(struct textdict *td, int nr, struct gang_elm **array)
{
  struct gang_scan_context gsc;
  gsc.nr = nr;
  gsc.array = array;
  gsc.nth = 0;
  anthy_textdict_scan(td, 0, &gsc, gang_scan);
}

struct scan_arg {
  struct gang_elm **array;
  int nr;
};

static void
request_scan(struct textdict *td, void *arg)
{
  struct scan_arg *sarg = (struct scan_arg *)arg;
  scan_dict(td, sarg->nr, sarg->array);
}

static void
do_gang_load_dic(xstr *sentence, int is_reverse)
{
  allocator ator = anthy_create_allocator(sizeof(struct gang_elm),
					  gang_elm_dtor);
  int from, len;
  xstr xs;
  int i, nr;
  struct gang_elm head;
  struct gang_elm **array, *cur;
  struct scan_arg sarg;
  head.tmp.next = NULL;
  nr = 0;
  for (from = 0; from < sentence->len ; from ++) {
    for (len = 1; len < 32 && from + len <= sentence->len; len ++) {
      xs.str = &sentence->str[from];
      xs.len = len;
      nr += find_gang_elm(ator, &head, &xs);
    }
  }
  if (!(array = malloc(sizeof(struct gang_elm *) * nr))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return;
  }
  cur = head.tmp.next;
  for (i = 0; i < nr; i++) {
    array[i] = cur;
    if (!cur) {
      anthy_log(0, "gang_elm is null at %dth loop\n", i);
      break;
    }
    cur = cur->tmp.next;
  }
  qsort(array, nr, sizeof(struct gang_elm *), gang_elm_compare_func);
  /**/
  anthy_gang_fill_seq_ent(main_dic_file, array, nr, is_reverse);
  /**/
  scan_misc_dic(array, nr, is_reverse);
  /* 個人辞書から読む */
  sarg.nr = nr;
  sarg.array = array;
  anthy_ask_scan(request_scan, (void *)&sarg);
  /**/
  free(array);
  anthy_free_allocator(ator);
}

void
anthy_gang_load_dic(xstr *sentence, int is_reverse)
{
  xstr *nx;
  if (!is_reverse && (nx = convert_vu(sentence))) {
    do_gang_load_dic(nx, is_reverse);
    anthy_free_xstr(nx);
  } else {
    do_gang_load_dic(sentence, is_reverse);
  }
}

/*
 * seq_entの取得
 ************************
 * seq_entの各種情報の取得
 */
int
anthy_get_nr_dic_ents(seq_ent_t se, xstr *xs)
{
  struct seq_ent *s = se;
  if (!s) {
    return 0;
  }
  if (!xs) {
    return s->nr_dic_ents;
  }
  return s->nr_dic_ents + anthy_get_nr_dic_ents_of_ext_ent(se, xs);
}

int
anthy_get_nth_dic_ent_str(seq_ent_t se, xstr *orig,
			  int n, xstr *x)
{
  if (!se || (n < 0)) { /* INDEPPAIR学習による交換先が見つからなかった時に不正なメモリアクセスをするバグの修正（通称「いちおく」の件） */
    x->str = NULL;      /* 不正なメモリアクセスやメモリの多重解放をするバグの修正 */
    x->len = 0;
    return -1;
  }
  if (n >= se->nr_dic_ents) {
    return anthy_get_nth_dic_ent_str_of_ext_ent(se, orig,
						n - se->nr_dic_ents, x);
  }
  x->len = se->dic_ents[n]->str.len;
  x->str = anthy_xstr_dup_str(&se->dic_ents[n]->str);
  return 0;
}

int
anthy_get_nth_dic_ent_is_compound(seq_ent_t se, int nth)
{
  if (!se) {
    return 0;
  }
  if (nth >= se->nr_dic_ents) {
    return 0;
  }
  return se->dic_ents[nth]->is_compound;
}

int
anthy_get_nth_dic_ent_freq(seq_ent_t se, int nth)
{
  struct seq_ent *s = se;
  if (!s) {
    return 0;
  }
  if (!s->dic_ents) {
    return anthy_get_nth_dic_ent_freq_of_ext_ent(se, nth);
  }
  if (s->nr_dic_ents <= nth) {
    return anthy_get_nth_dic_ent_freq_of_ext_ent(se, nth - se->nr_dic_ents);
  }
  return s->dic_ents[nth]->freq;
}

int
anthy_get_nth_dic_ent_wtype(seq_ent_t se, xstr *xs,
			    int n, wtype_t *w)
{
  struct seq_ent *s = se;
  if (!s) {
    *w = anthy_wt_none;
    return -1;
  }
  if (s->nr_dic_ents <= n) {
    int r;
    r = anthy_get_nth_dic_ent_wtype_of_ext_ent(xs, n - s->nr_dic_ents, w);
    if (r == -1) {
      *w = anthy_wt_none;
    }
    return r;
  }
  *w =  s->dic_ents[n]->type;
  return 0;
}

int
anthy_get_seq_ent_pos(seq_ent_t se, int pos)
{
  int i, v=0;
  struct seq_ent *s = se;
  if (!s) {
    return 0;
  }
  if (s->nr_dic_ents == 0) {
    return anthy_get_ext_seq_ent_pos(se, pos);
  }
  for (i = 0; i < s->nr_dic_ents; i++) {
    if (anthy_wtype_get_pos(s->dic_ents[i]->type) == pos) {
      v += s->dic_ents[i]->freq;
      if (v == 0) {
	v = 1;
      }
    }
  }
  return v;
}

int
anthy_get_seq_ent_ct(seq_ent_t se, int pos, int ct)
{
  int i, v=0;
  struct seq_ent *s = se;
  if (!s) {
    return 0;
  }
  if (s->nr_dic_ents == 0) {
    return anthy_get_ext_seq_ent_ct(s, pos, ct);
  }
  for (i = 0; i < s->nr_dic_ents; i++) {
    if (anthy_wtype_get_pos(s->dic_ents[i]->type)== pos &&
	anthy_wtype_get_ct(s->dic_ents[i]->type)==ct) {
      v += s->dic_ents[i]->freq;
      if (v == 0) {
	v = 1;
      }
    }
  }
  return v;
}

/*
 * wtの品詞を持つ単語の中で最大の頻度を持つものを返す
 */
int
anthy_get_seq_ent_wtype_freq(seq_ent_t seq, wtype_t wt)
{
  int i, f;

  if (!seq) {
    return 0;
  }
  /**/
  if (seq->nr_dic_ents == 0) {
    return anthy_get_ext_seq_ent_wtype(seq, wt);
  }

  f = 0;
  /* 単語 */
  for (i = 0; i < seq->nr_dic_ents; i++) {
    if (seq->dic_ents[i]->order == 0 &&
	anthy_wtype_include(wt, seq->dic_ents[i]->type)) {
      if (f < seq->dic_ents[i]->freq) {
	f = seq->dic_ents[i]->freq;
      }
    }
  }
  return f;
}

/*
 * wtの品詞を持つ複合語の中で最大の頻度を持つものを返す
 */
int
anthy_get_seq_ent_wtype_compound_freq(seq_ent_t se, wtype_t wt)
{
  int i,f;
  struct seq_ent *s = se;
  if (!s) {
    return 0;
  }
  /**/
  f = 0;
  for (i = 0; i < s->nr_dic_ents; i++) {
    if (!anthy_get_nth_dic_ent_is_compound(se, i)) {
      continue;
    }
    if (anthy_wtype_include(wt, s->dic_ents[i]->type)) {
      if (f < s->dic_ents[i]->freq) {
	f = s->dic_ents[i]->freq;
      }
    }
  }
  return f;
}

int
anthy_get_seq_ent_indep(seq_ent_t se)
{
  int i;
  struct seq_ent *s = se;
  if (!s) {
    return 0;
  }
  if (s->nr_dic_ents == 0) {
    return anthy_get_ext_seq_ent_indep(s);
  }
  for (i = 0; i < s->nr_dic_ents; i++) {
    if (anthy_wtype_get_indep(s->dic_ents[i]->type)) {
      return 1;
    }
  }
  return 0;
}

int
anthy_has_compound_ents(seq_ent_t se)
{
  if (!se) {
    return 0;
  }
  return se->nr_compound_ents;
}

/* compundでない候補を持っているか */
int
anthy_has_non_compound_ents(seq_ent_t se)
{
  if (!se) {
    return 0;
  }
  if (se->nr_dic_ents == 0) {
    return 1;
  }
  return se->nr_dic_ents - se->nr_compound_ents;
}

compound_ent_t
anthy_get_nth_compound_ent(seq_ent_t se, int nth)
{
  if (!se) {
    return NULL;
  }
  if (nth >= 0 && nth < se->nr_dic_ents) {
    return se->dic_ents[nth];
  }
  return NULL;
}

struct elm_compound {
  int len;
  xstr str;
};

/* 要素に対応する読みの長さを返す */
static int
get_element_len(xchar xc)
{
  if (xc > '0' && xc <= '9') {
    return xc - '0';
  }
  if (xc >= 'a' && xc <= 'z') {
    return xc - 'a' + 10;
  }
  return 0;
}

static struct elm_compound *
get_nth_elm_compound(compound_ent_t ce, struct elm_compound *elm, int nth)
{
  int off = 0;
  int i, j;
  for (i = 0; i <= nth; i++) {
    /* nth番目の要素の先頭へ移動する */
    while (!(ce->str.str[off] == '_' &&
	     get_element_len(ce->str.str[off+1]) > 0)) {
      off ++;
      if (off + 1 >= ce->str.len) {
	return NULL;
      }
    }
    /* 構造体へ情報を取り込む */
    elm->len = get_element_len(ce->str.str[off+1]);
    elm->str.str = &ce->str.str[off+2];
    elm->str.len = ce->str.len - off - 2;
    for (j = 0; j < elm->str.len; j++) {
      if (elm->str.str[j] == '_') {
	elm->str.len = j;
	break;
      }
    }
    off ++;
  }
  return elm;
}

int
anthy_compound_get_nr_segments(compound_ent_t ce)
{
  struct elm_compound elm;
  int i;
  if (!ce) {
    return 0;
  }
  for (i = 0; get_nth_elm_compound(ce, &elm, i); i++);
  return i;
}

int
anthy_compound_get_nth_segment_len(compound_ent_t ce, int nth)
{
  struct elm_compound elm;
  if (get_nth_elm_compound(ce, &elm, nth)) {
    return elm.len;
  }
  return 0;
}

int
anthy_compound_get_nth_segment_xstr(compound_ent_t ce, int nth, xstr *xs)
{
  struct elm_compound elm;
  if (get_nth_elm_compound(ce, &elm, nth)) {
    if (xs) {
      *xs = elm.str;
      return 0;
    }
  }
  return -1;
}

int
anthy_compound_get_wtype(compound_ent_t ce, wtype_t *w)
{
  *w = ce->type;
  return 0;
}

int
anthy_compound_get_freq(compound_ent_t ce)
{
  return ce->freq;
}

/* フロントエンドから呼ばれる */
void
anthy_lock_dic(void)
{
  anthy_priv_dic_lock();
  anthy_priv_dic_update();
}

/* フロントエンドから呼ばれる */
void
anthy_unlock_dic(void)
{
  anthy_priv_dic_unlock();
}


dic_session_t
anthy_dic_create_session(void)
{
  return anthy_create_mem_dic();
}

void
anthy_dic_activate_session(dic_session_t d)
{
  anthy_current_personal_dic_cache = d;
}

void
anthy_dic_release_session(dic_session_t d)
{
  anthy_release_mem_dic(d);
}

void
anthy_dic_set_personality(const char *id)
{
  anthy_current_record = anthy_create_record(id);
  anthy_current_personal_dic_cache = anthy_create_mem_dic();
  anthy_init_private_dic(id);
}


/** 辞書サブシステムを初期化
 */
int
anthy_init_dic(void)
{
  if (dic_init_count) {
    dic_init_count ++;
    return 0;
  }
  if (anthy_init_diclib() == -1) {
    return -1;
  }

  anthy_init_wtypes();
  anthy_init_mem_dic();
  anthy_init_record();
  anthy_init_ext_ent();
  anthy_init_features();

  anthy_init_word_dic();
  main_dic_file = anthy_create_word_dic();
  if (!main_dic_file) {
    anthy_log(0, "Failed to create file dic.\n");
    return -1;
  }
  dic_init_count ++;
  return 0;
}

/** 辞書サブシステムをすべて解放
 */
void
anthy_quit_dic(void)
{
  dic_init_count --;
  if (dic_init_count) {
    return;
  }
  if (anthy_current_record) {
    anthy_release_record(anthy_current_record);
  }
  anthy_release_private_dic();
  anthy_current_record = NULL;
  anthy_quit_mem_dic();
  anthy_quit_diclib();
}
