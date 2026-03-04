/*
 * "123" "ABC" のような辞書にのってない
 * 文字列に対する問合せの場合は全ての候補をここで生成する
 * 上記の他に郵便番号へのアクセスも行なう
 *
 * Copyright (C) 2001-2005 TABATA Yusuke
 * Copyright (C) 2004-2005 YOSHIDA Yuichi
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <anthy/anthy.h> /* for ANTHY_*_ENCODING */
#include <anthy/conf.h>
#include <anthy/logger.h>
#include <anthy/xstr.h>
#include <anthy/xchar.h>
#include "dic_main.h"
#include "dic_ent.h"

/* ext entry */
static struct seq_ent unkseq_ent;/*未知文字列たとえば英文字列とか*/
static struct seq_ent num_ent;/*数字など*/
static struct seq_ent sep_ent;/*セパレータなど。*/
/* ext entryのwtype*/
static wtype_t wt_num;

static xchar narrow_wide_tab[]= {WIDE_0, WIDE_1, WIDE_2,
				 WIDE_3, WIDE_4, WIDE_5,
				 WIDE_6, WIDE_7, WIDE_8, WIDE_9};
static int kj_num_tab[]={KJ_0, KJ_1, KJ_2, KJ_3, KJ_4,
			 KJ_5, KJ_6, KJ_7, KJ_8, KJ_9};

struct zipcode_line {
  int nr;
  xstr **strs;
};

/* 地名を追加する */
static void
pushback_place_name(struct zipcode_line *zl, char *pn)
{
  if (pn[0] == '#') {
    return ;
  }
  zl->strs = realloc(zl->strs, sizeof(xstr *) * (zl->nr + 1));
  zl->strs[zl->nr] = anthy_cstr_to_xstr(pn, ANTHY_EUC_JP_ENCODING);
  zl->nr++;
}

/* 郵便番号辞書をパースしてスペース区切りを検出する */
static void
parse_zipcode_line(struct zipcode_line *zl, char *ln)
{
  char buf[1000];
  int i = 0;
  while (*ln) {
    buf[i] = *ln;
    if (*ln == '\\') {
      buf[i] = ln[1];
      i ++;
      if (ln[1]) {
	ln ++;
      }
    } else if (*ln == ' ') {
      buf[i] = 0;
      i = 0;
      pushback_place_name(zl, buf);
    } else {
      i ++;
    }
    /**/
    ln ++;
  }
  buf[i] = 0;
  pushback_place_name(zl, buf);
}

/* 郵便番号辞書から探す */
static void
search_zipcode_dict(struct zipcode_line *zl, xstr* xs)
{
  FILE *fp;
  char buf[1000];
  int len;
  xstr *temp;
  char *index;

  zl->nr = 0;
  zl->strs = NULL;
  fp = fopen(anthy_conf_get_str("ZIPDICT_EUC"), "r");
  if (!fp) {
    return ;
  }

  /* 半角、全角を吸収する */
  temp = anthy_xstr_wide_num_to_num(xs);
  index = anthy_xstr_to_cstr(temp, 0);
  len = strlen(index);

  /* 全部grepする */
  while (fgets(buf, 1000, fp)) {
    /* 3文字の郵便番号が7文字の郵便番号の頭にマッチしないように */
    if (!strncmp(buf, index, len) && buf[len] == ' ') {
      /* 改行を消す */
      buf[strlen(buf) - 1] = 0;
      parse_zipcode_line(zl, &buf[len + 1]);
    }
  }
  anthy_free_xstr(temp);    /* メモリリークの修正 */
  free(index);
  fclose(fp);
}

/* 郵便番号辞書の情報を解放する */
static void
free_zipcode_line(struct zipcode_line *zl)
{
  int i;
  for (i = 0; i < zl->nr; i++) {
    anthy_free_xstr(zl->strs[i]);
  }
  free(zl->strs);
}

static int
gen_zipcode(xstr* xs, xstr *dest, int nth)
{
  struct zipcode_line zl;

  /* 郵便番号辞書から地名を読み取る */
  search_zipcode_dict(&zl, xs);

  /* 候補を取得する */
  if (zl.nr > nth) {
    dest->len = zl.strs[nth]->len;
    dest->str = anthy_xstr_dup_str(zl.strs[nth]);
    free_zipcode_line(&zl);
    return 0;
  } else {
    free_zipcode_line(&zl);
    return -1;
  }
}



/* 半角の数字から全角の数字を求める */
static xchar
narrow_num_to_wide_num(xchar xc)
{
  if (xc > '9' || xc < '0') {
    return WIDE_0;
  }
  return narrow_wide_tab[(int)(xc - '0')];
}

/* 全角の数字から半角の数字を求める */
static xchar
wide_num_to_narrow_num(xchar xc)
{
  int i;
  for (i = 0; i < 10; i++) {
    if (xc == narrow_wide_tab[i]) {
      return i + '0';
    }
  }
  return '0';
}
/*
 * 一桁の整数を漢数字に変換する
 */
static xchar
get_kj_num(int n)
{
  if (n > 9 || n < 1) {
    return KJ_10;
  }
  return kj_num_tab[n];
}

/*
 * 4桁分の整数を漢字文字列としてを生成する
 */
static void
compose_num_component(xstr *xs, long long num)
{
  int n[4],i;
  int a[4] = { 0 , KJ_10, KJ_100, KJ_1000};
  xstr *tmp;
   assert(xs);
  for (i = 0; i < 4; i++) {
    n[i] = num-(num/10)*10;
    num /= 10;
  }
  /* 10,100,1000の位 */
  for (i = 3; i > 0; i--) {
    if (n[i] > 0) {
      if (n[i] > 1) {
        tmp = anthy_xstrappend(xs, get_kj_num(n[i]));
	assert(tmp == xs);
      }
      tmp = anthy_xstrappend(xs, a[i]);
      assert(tmp == xs);
    }
  }
  /* 1の位 */
  if (n[0]) {
    tmp = anthy_xstrappend(xs, get_kj_num(n[0]));
    assert(tmp == xs);
  }
}

/** 漢数字の文字列を作る */
static int
gen_kanji_num(long long num, xstr *dest)
{
  int i;
  int n[10];
  if (num < 1 || num >= 10000000000000000LL) {
    return -1;
  }
  /* 4桁ずつ配列nにつめる */
  for (i = 0; i < 10; i ++) {
    n[i] = num-(num/10000)*10000;
    num = num/10000;
  }
  /**/
  dest->len = 0;
  dest->str = 0;
  /* 京の位をつくる */
  if (n[3]) {
    compose_num_component(dest, n[3]);
    anthy_xstrappend(dest, KJ_1000000000000);
  }
  /* 億の位をつくる */
  if (n[2]) {
    compose_num_component(dest, n[2]);
    anthy_xstrappend(dest, KJ_100000000);
  }
  /* 万の位をつくる */
  if (n[1]) {
    compose_num_component(dest, n[1]);
    anthy_xstrappend(dest, KJ_10000);
  }
  /**/
  compose_num_component(dest, n[0]);
  return 0;
}

static int
get_nr_zipcode(xstr* xs)
{
  struct zipcode_line zl;
  int nr = 0;
  if (xs->len != 3 && xs->len != 7) {
    return 0;
  }
  /* 郵便番号辞書から地名を読み取る */
  search_zipcode_dict(&zl, xs);

  nr = zl.nr;
  free_zipcode_line(&zl);
  return nr;
}

static int
get_nr_num_ents(long long num)
{
  if (num > 0 && num < 10000000000000000LL) {
    if (num > 999) {
      /* アラビア数字(そのまま)、アラビア数字(全角半角切替え)、
	 漢数字、3桁区切り(全角、半角) */
      return 5;
    } else {
      /* アラビア数字(そのまま)、全角半角切替え、漢数字 */
      return 3;
    }
  } else {
    /* アラビア数字(そのまま)、全角半角切替え */
    return 2;
  }
}


/*
 * いくつの合成のエントリーがあるか
 */
int
anthy_get_nr_dic_ents_of_ext_ent(seq_ent_t se, xstr *xs)
{
  if (se == &unkseq_ent) {
    return 1;
  }
  if (anthy_get_xstr_type(xs) & (XCT_NUM|XCT_WIDENUM)) {
    long long num = anthy_xstrtoll(xs);
    return get_nr_num_ents(num) + get_nr_zipcode(xs);
  }
  return 0;
}

/* 文字列の全角半角を交換する */
static void
toggle_wide_narrow(xstr *dest, xstr *src)
{
  int f, i;
  dest->len = src->len;
  dest->str = anthy_xstr_dup_str(src);
  f = anthy_get_xstr_type(src) & XCT_WIDENUM;
  for (i = 0; i < dest->len; i++) {
    if (f) {
      dest->str[i] = wide_num_to_narrow_num(src->str[i]);
    } else {
      dest->str[i] = narrow_num_to_wide_num(src->str[i]);
    }
  }
}

/* 3桁に区切った数字を生成する */
static int
gen_separated_num(long long num, xstr *dest, int full)
{
  int width = 0, dot_count;
  long long tmp;
  int i, pos;

  if (num < 1000) {
    return -1;
  }

  /* 桁数を数える */
  for (tmp = num; tmp != 0; tmp /= 10) {
    width++;
  }
  /* 点の数 */
  dot_count = (width - 1) / 3;
  /* 格納するのに必要な文字列を用意する */
  dest->len = dot_count + width;
  if (!(dest->str = malloc(sizeof(xchar)*dest->len))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    dest->len = 0;
    return -1;
  }

  /* 右の桁から順に決めていく */
  for (i = 0, (pos = dest->len - 1) && (pos >= 0); i < width; i++, pos--) {
    int n = num % 10;
    /* カンマを追加 */
    if (i > 0 && (i % 3) == 0) {
      if (full) {
	dest->str[pos] = WIDE_COMMA;
      } else {
	dest->str[pos] = ',';
      }
      pos--;
    }
    if (pos < 0) {
      anthy_log(0, "pos %d < 0 in %s:%d\n", pos, __FILE__, __LINE__);
      break;
    }
    if (full) {
      /* 全角数字 */
      dest->str[pos] = narrow_wide_tab[n];
    } else {
      /* ASCII数字 */
      dest->str[pos] = 48 + n;
    }
    num /= 10;
  }
  return 0;
}

/*
 * nth個めの候補を取り出す
 */
int
anthy_get_nth_dic_ent_str_of_ext_ent(seq_ent_t se, xstr *xs,
				     int nth, xstr *dest)
{
  dest->str = NULL;     /* 不正なメモリアクセスやメモリの多重解放をするバグの修正 */
  dest->len = 0;
  if (nth == 0) {
    /* 無変換文字列 */
    dest->len = xs->len;
    dest->str = anthy_xstr_dup_str(xs);
    return 0;
  }
  if (se == &unkseq_ent) {
    switch(nth) {
    case 1:
      /* 全角、半角のトグル */
      return 0;
    }
  }
  if (anthy_get_xstr_type(xs) & (XCT_NUM|XCT_WIDENUM)) {
    long long num = anthy_xstrtoll(xs);
    const int base_ents = get_nr_num_ents(num); /* ３桁郵便番号への対応 */
    /* 漢数字、アラビア数字、全角半角切替え
     * GCC 11.0.1 reports this statement may fall through because of no break
     * in case statement with "-Wimplicit-fallthrough" option.
     */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    switch(nth) {
    case 1:
      /* 全角半角を入れ換えたもの */
      toggle_wide_narrow(dest, xs);
      return 0;
    case 2:
      /* 漢数字 */
      if (!gen_kanji_num(num, dest)) {
	return 0;
      }
      /* break無し */
    case 3:
      /* 3桁で区切った数字 */
      if (!gen_separated_num(num, dest, 0)) {
	return 0;
      }
      /* break無し */
    case 4:
      /* 3桁で区切った数字(全角) */
      if (!gen_separated_num(num, dest, 1)) {
	return 0;
      }
      /* break無し */
    default:
      /* 郵便番号 */
      if (base_ents <= nth) {   /* ３桁郵便番号への対応 */
	if (xs->len == 3 || xs->len == 7) {
	  if (!gen_zipcode(xs, dest, nth - base_ents)) {    /* ３桁郵便番号への対応 */
	    return 0;
	  }
	}
      }
      break;
    }
#pragma GCC diagnostic pop
    return -1;
  }
  return 0;
}

int
anthy_get_ext_seq_ent_indep(struct seq_ent *se)
{
  if (se == &num_ent || se == &unkseq_ent) {
    return 1;
  }
  return 0;
}

/* 活用形を得る */
int
anthy_get_ext_seq_ent_ct(struct seq_ent *se, int pos, int ct)
{
  if (anthy_get_ext_seq_ent_pos(se, pos) && ct == CT_NONE) {
    /* 品詞が合っていてかつ無活用の場合
       (ext_entは活用しない) */
    return 10;
  }
  return 0;
}

/* 品詞を取得する */
int
anthy_get_ext_seq_ent_pos(struct seq_ent *se, int pos)
{
  /* ext_entは名詞のみ */
  if (se == &num_ent && pos == POS_NOUN) {
    return 10;
  }
  if ((se == &unkseq_ent) && pos == POS_NOUN) {
    return 10;
  }
  return 0;
}

/*
 * 辞書にのっていないシーケンスを解析
 */
seq_ent_t
anthy_get_ext_seq_ent_from_xstr(xstr *x, int is_reverse)
{
  int t = anthy_get_xstr_type(x);

  /* 数字のみで構成されていれば num_ent */
  if (t & (XCT_NUM | XCT_WIDENUM)) {
    return &num_ent;
  }
  /* 英数ならunkseq */
  if (t & XCT_ASCII) {
    return &unkseq_ent;
  }
  if (t & XCT_KATA) {
    return &unkseq_ent;
  }
  if (!is_reverse) {
    /* 逆変換中は漢字候補は作らない */
    if (t & XCT_KANJI) {
      return &unkseq_ent;
    }
  }
  if (x->len == 1) {
    /* 辞書にのってなくて1文字ならセパレータ */
    return &sep_ent;
  }
  return 0;
}

int
anthy_get_nth_dic_ent_wtype_of_ext_ent(xstr *xs, int nth,
				       wtype_t *wt)
{
  int type;
  (void)nth;
  type = anthy_get_xstr_type(xs);
  if (type & (XCT_NUM | XCT_WIDENUM)) {
    *wt = wt_num;
    return 0;
  }
  if (type & XCT_KATA) {
    *wt = anthy_get_wtype(POS_NOUN, COS_NONE, SCOS_NONE, CC_NONE,
			  CT_NONE, WF_INDEP);
    return 0;
  }
  return -1;
}

int
anthy_get_nth_dic_ent_freq_of_ext_ent(struct seq_ent *se, int nth)
{
  (void)se;
  (void)nth;
  return 100;
}

int
anthy_get_ext_seq_ent_wtype(struct seq_ent *se, wtype_t w)
{
  if (se == &num_ent) {
    if (anthy_wtype_include(w, wt_num)) {
      /* 数字の場合 */
      return 10;
    }
    return 0;
  }
  if (anthy_wtype_get_pos(w) == POS_NOUN &&
      anthy_wtype_get_cos(w) == COS_NONE &&
      anthy_wtype_get_scos(w) == SCOS_NONE) {
    /* 名詞、副品詞なし、副々品詞無しにマッチ */
    return 10;
  }
  return 0;
}

void
anthy_init_ext_ent(void)
{
  /**/
  unkseq_ent.seq_type = 0;
  unkseq_ent.nr_dic_ents = 0;
  num_ent.seq_type = 0;
  num_ent.nr_dic_ents = 0;
  sep_ent.seq_type = 0;
  sep_ent.nr_dic_ents = 0;
  /**/
  wt_num = anthy_init_wtype_by_name("数詞");
}
