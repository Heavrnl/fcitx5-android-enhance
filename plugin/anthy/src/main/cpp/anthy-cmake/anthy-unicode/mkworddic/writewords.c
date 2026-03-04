/*
 * 読みから単語の情報を取得するデータ構造をファイル中に
 * 出力するためのコード
 *
 * データ構造を変更しやすくするためにmkdic.cから分離(2005/7/8)
 *
 * output_word_dict()が呼び出される
 *
 * Copyright (C) 2000-2006 TABATA Yusuke
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <anthy/anthy.h>
#include <anthy/word_dic.h>
#include "mkdic.h"

extern FILE *page_out, *page_index_out;
extern FILE *yomi_entry_index_out, *yomi_entry_out;

static int
write_word(struct word_entry *we, int encoding)
{
  int count;
  if (encoding == ANTHY_UTF8_ENCODING) {
    count = fprintf(yomi_entry_out, "%s", we->word_utf8);
  } else {
    char *s = anthy_conv_utf8_to_euc(we->word_utf8);
    count = fprintf(yomi_entry_out, "%s", s);
    free(s);
  }
  return count;
}

static int
write_freq(FILE *fp, struct word_entry *we)
{
  int count = 0;
  int freq = we->freq / 100;
  if (freq != 1) {
    count += fprintf(fp, "*%d", freq);
  }
  return count;
}

static int
compare_word_entry(struct word_entry *prev_we,
		   struct word_entry *we)
{
  if (strcmp(prev_we->wt_name, we->wt_name) ||
      (prev_we->freq / 100) != (we->freq / 100) ||
      prev_we->feature != we->feature) {
    return 1;
  }
  return 0;
}

/** 一つの読みに対する単語の内容を出力する
 * 返り値は出力したバイト数
 */
static int
output_word_entry_for_a_yomi(struct yomi_entry *ye, int encoding)
{
  int i;
  int count = 0;

  if (!ye) {
    return 0;
  }
  if (encoding == ANTHY_UTF8_ENCODING) {
    count ++;
    fputc('u', yomi_entry_out);
  }
  /* 各単語を出力する */
  for (i = 0; i < ye->nr_entries; i++) {
    struct word_entry *we = &ye->entries[i];
    struct word_entry *prev_we = NULL;
    if (i != 0) {
      prev_we = &ye->entries[i-1];
    }
    /**/
    if (!we->raw_freq) {
      continue;
    }
    if (i > 0) {
      /* 二つ目以降は空白から始まる */
      count += fprintf(yomi_entry_out, " ");
    }
    /* 品詞と頻度を出力する */
    if (i == 0 ||
	compare_word_entry(prev_we, we)) {
      count += fprintf(yomi_entry_out, "%s", we->wt_name);
      if (we->feature != 0) {
	count += fprintf(yomi_entry_out, ",");
      }
      count += write_freq(yomi_entry_out, we);
      count += fprintf(yomi_entry_out, " ");
    }
    /* 単語を出力する場所がこの単語のid */
    we->offset = count + ye->offset;
    /* 単語を出力する */
    count += write_word(we, encoding);
  }

  fputc(0, yomi_entry_out);
  return count + 1;
}

/* 2つの文字列の共通部分の長さを求める */
static int
common_len(xstr *s1, xstr *s2)
{
  int m,i;
  if ((!s1) || (!s2)) {
    return 0;
  }
  if (s1->len < s2->len) {
    m = s1->len;
  }else{
    m = s2->len;
  }
  for (i = 0; i < m; i++) {
    if (s1->str[i] != s2->str[i]) {
      return i;
    }
  }
  return m;
}

/*
 * 2つの文字列の差分を出力する
 * AAA ABBB という2つの文字列を見た場合には
 * ABBBはAAAのうしろ2文字を消してBBBを付けたものとして
 * \0x2BBBと出力される。
 */
static int
output_diff(xstr *p, xstr *c, int encoding)
{
  int i, m, len = 1;
  m = common_len(p, c);
  if (p && p->len > m) {
    fprintf(page_out, "%c", p->len - m + 1);
  } else {
    fprintf(page_out, "%c", 1);
  }
  for (i = m; i < c-> len; i++) {
    char buf[8];
    len += anthy_sputxchar(buf, c->str[i], encoding);
    fputs(buf, page_out);
  }
  return len;
}

static void
begin_new_page(int i)
{
  fputc(0, page_out);
  write_nl(page_index_out, i);
}

static void
output_entry_index(int i)
{
  write_nl(yomi_entry_index_out, i);
}

/* 読みの文字列からファイル中の位置(offset)を求めるためのテーブルを作る
 * page_out, page_index_out, yomi_entry_index_outに出力
 */
static void
generate_yomi_to_offset_map(struct yomi_entry_list *yl)
{
  int i;
  struct yomi_entry *ye = NULL;
  xstr *prev = NULL;
  int page_index = 0;
  /* 読みから位置(offset)を計算するデータ構造を構成する */

  /* まず、最初の読みに対するエントリのインデックスを書き出す */
  write_nl(page_index_out, page_index);
  /**/
  for (i = 0; i < yl->nr_valid_entries; i++) {
    ye = yl->ye_array[i];
    /* 新しいページの開始 */
    if ((i % WORDS_PER_PAGE) == 0 && (i != 0)) {
      page_index ++;
      prev = NULL;
      begin_new_page(page_index);
    }

    /* 読みに対応する情報を出力する */
    page_index += output_diff(prev, ye->index_xstr, yl->index_encoding);

    output_entry_index(ye->offset);
    /***/
    prev = ye->index_xstr;
  }
}

/** 単語辞書を出力する
 * また、このときに辞書中のオフセットも計算する */
void
output_word_dict(struct yomi_entry_list *yl)
{
  int entry_index = 0;
  int i;
  struct yomi_entry *ye = NULL;

  /* 各読みに対するループ */
  for (i = 0; i < yl->nr_valid_entries; i++) {
    /* 単語を出力して、ファイル中の位置(offset)を計算する */
    ye = yl->ye_array[i];
    ye->offset = entry_index;
    entry_index += output_word_entry_for_a_yomi(ye, yl->body_encoding);
  }
  /* 読みの文字列からファイル中の位置(offset)を求めるためのテーブルを作る */
  generate_yomi_to_offset_map(yl);

  /* 最後の読みを終了 */
  entry_index += output_word_entry_for_a_yomi(ye, yl->body_encoding);
  write_nl(yomi_entry_index_out, entry_index);
  write_nl(page_index_out, 0);

  /**/
  printf("Total %d indexes, %d words, (%d pages).\n",
	 yl->nr_valid_entries,
	 yl->nr_words,
	 yl->nr_valid_entries / WORDS_PER_PAGE + 1);
}
