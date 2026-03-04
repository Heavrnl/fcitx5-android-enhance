/*
 * 個人辞書を扱うためのコード
 *
 * ユーザが明示的に登録した単語だけでなく、
 * 未知語を自動的に学習して管理するAPIも持つ。
 *
 * Copyright (C) 2000-2007 TABATA Yusuke
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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <anthy/anthy.h>
#include <anthy/alloc.h>
#include <anthy/dic.h>
#include <anthy/diclib.h>
#include <anthy/record.h>
#include <anthy/dicutil.h>
#include <anthy/conf.h>
#include <anthy/logger.h>
#include <anthy/texttrie.h>
#include <anthy/textdict.h>
#include <anthy/word_dic.h>
#include "dic_main.h"
#include "dic_ent.h"
#include <src-diclib/diclib_inner.h>

/* 個人辞書 */
struct text_trie *anthy_private_tt_dic;
struct text_trie *old_anthy_private_tt_dic;
struct textdict *anthy_private_text_dic;
struct textdict *old_anthy_private_text_dic;
static struct textdict *anthy_imported_text_dic;
static struct textdict *old_anthy_imported_text_dic;
static char *anthy_imported_dic_dir;
static char *old_anthy_imported_dic_dir;
static char *anthy_private_dir;
static char *old_anthy_private_dir;
/* ロック用の変数 */
static char *lock_fn;
static int lock_depth;
static int lock_fd;

#define MAX_DICT_SIZE 100000000

/* anthy_get_user_dir:
 * @is_old: 0 or 1.
 *
 * Get the Anthy private directory.
 * The internal directory will be released by anthy_release_private_dic().
 */
const char *
anthy_get_user_dir(int is_old)
{
  const char *hd;
  const char *xdg;
  /**/
  if (!is_old && anthy_private_dir)
    return anthy_private_dir;
  if (is_old && old_anthy_private_dir)
    return old_anthy_private_dir;

  if (is_old) {
    hd = anthy_conf_get_str("HOME");
    if (!(old_anthy_private_dir = malloc(strlen(hd) + 10))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return NULL;
    }
    sprintf(old_anthy_private_dir, "%s/.anthy", hd);
    return old_anthy_private_dir;
  }
  xdg = anthy_conf_get_str("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    if (!(anthy_private_dir = malloc(strlen(xdg) + 10))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return NULL;
    }
    sprintf(anthy_private_dir, "%s/anthy", xdg);
  } else {
    hd = anthy_conf_get_str("HOME");
    if (!(anthy_private_dir = malloc(strlen(hd) + 15))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return NULL;
    }
    sprintf(anthy_private_dir, "%s/.config/anthy", hd);
  }
  return anthy_private_dir;
}

/* 個人辞書のディレクトリの有無を確認する */
void
anthy_check_user_dir(void)
{
  const char *dn = anthy_get_user_dir(0);
  /* Use anthy_file_test() and anthy_mkdir_with_parents() since
   * chmod() after stat() causes a a time-of-check, * time-of-use race
   * condition (TOCTOU).
   */
  if (!anthy_file_test (dn, ANTHY_FILE_TEST_EXISTS | ANTHY_FILE_TEST_IS_DIR)) {
    int r;
    errno = 0;
    r = anthy_mkdir_with_parents(dn, S_IRWXU);
    if (r == -1){
      anthy_log(0, "Failed to create profile directory: %s\n", strerror(errno));
      return;
    }
  }
}

static void
init_lock_fn(const char *home, const char *id)
{
  lock_fn = malloc(strlen(home) + strlen(id) + 33);
  sprintf(lock_fn, "%s/lock-file_%s", home, id);
}

static struct text_trie *
open_tt_dic(const char *home, const char *id)
{
  struct text_trie *tt;
  char *buf = malloc(strlen(home) + strlen(id) + 33);
  sprintf(buf, "%s/private_dict_%s.tt", home, id);
  tt = anthy_trie_open(buf, 0);
  free(buf);
  return tt;
}

static struct textdict *
open_textdic(const char *home, const char *name, const char *id)
{
  char *fn = malloc(strlen(home) + strlen(name) + strlen(id) + 2);
  struct textdict *td;
  sprintf(fn, "%s/%s%s", home, name, id);
  td = anthy_textdict_open(fn);
  free(fn);
  return td;
}

void
anthy_priv_dic_lock(void)
{
  struct flock lck;
  lock_depth ++;
  if (lock_depth > 1) {
    return ;
  }
  if (!lock_fn) {
    /* 初期化をミスってる */
    lock_fd = -1;
    return ;
  }

  /* ファイルロックの方法は多数あるが、この方法はcygwinでも動くので採用した */
  lock_fd = open(lock_fn, O_CREAT|O_RDWR, S_IREAD|S_IWRITE);
  if (lock_fd == -1) {
    return ;
  }

  lck.l_type = F_WRLCK;
  lck.l_whence = (short) 0;
  lck.l_start = (off_t) 0;
  lck.l_len = (off_t) 1;
  if (fcntl(lock_fd, F_SETLKW, &lck) == -1) {
    close(lock_fd);
    lock_fd = -1;
  }
}

void
anthy_priv_dic_unlock(void)
{
  lock_depth --;
  if (lock_depth > 0) {
    return ;
  }

  if (lock_fd != -1) {
    close(lock_fd);
    lock_fd = -1;
  }
}

void
anthy_priv_dic_update(void)
{
  if (anthy_private_tt_dic)
    anthy_trie_update_mapping(anthy_private_tt_dic);
  if (old_anthy_private_tt_dic)
    anthy_trie_update_mapping(old_anthy_private_tt_dic);
}

/* seq_entに追加する */
static void
add_to_seq_ent(const char *line, int encoding, struct seq_ent *seq)
{
  struct word_line wl;
  wtype_t wt;
  xstr *xs;
  /* */
  if (anthy_parse_word_line(line, &wl)) {
    return ;
  }
  xs = anthy_cstr_to_xstr(wl.word, encoding);
  anthy_type_to_wtype(wl.wt, &wt);
  anthy_mem_dic_push_back_dic_ent(seq, 0, xs, wt,
				  NULL, wl.freq, 0);
  anthy_free_xstr(xs);
}

/* texttrieに登録されているかをチェックし、
 * 登録されていればseq_entに追加する
 */
static void
copy_words_from_tt(struct seq_ent *seq, xstr *xs,
                   int encoding, const char *prefix,
                   int is_old)
{
  struct text_trie *tt_dic;
  char *key, *v;
  int key_len;
  char *key_buf;
  int prefix_len = strlen(prefix);
  /**/
  if (is_old)
    tt_dic = old_anthy_private_tt_dic;
  else
    tt_dic = anthy_private_tt_dic;
  if (!tt_dic)
    return;
  key = anthy_xstr_to_cstr(xs, encoding);
  key_len = strlen(key);
  if (!(key_buf = malloc(key_len + 12))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    free(key);
    return;
  }
  /* 辞書中には各単語が「見出し XXXX」(XXXXはランダムな文字列)を
   * キーとして保存されているので列挙する
   */
  sprintf(key_buf, "%s%s ", prefix, key);
  do {
    if (strncmp(&key_buf[2], key, key_len) ||
	strncmp(&key_buf[0], prefix, prefix_len) ||
	key_buf[key_len+2] != ' ') {
      /* 「見出し 」で始まっていないので対象外 */
      break;
    }
    /* 単語を読み出して登録
     * GCC 11.0.1 reports double-'free' of 'v'
     * in case statement with "-Wanalyzer-double-free" option
     * but 'v' is always allocated newly.
     */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-double-free"
    v = anthy_trie_find(tt_dic, key_buf);
    if (v) {
      add_to_seq_ent(v, encoding, seq);
    }
    free(v);
#pragma GCC diagnostic pop
    /**/
  } while (anthy_trie_find_next_key(tt_dic,
				    key_buf, key_len + 8));
  free(key);
  free(key_buf);
}

void
anthy_copy_words_from_private_dic(struct seq_ent *seq,
				  xstr *xs, int is_reverse)
{
  if (is_reverse) {
    return ;
  }
  /* 個人辞書から取ってくる */
  copy_words_from_tt(seq, xs, ANTHY_EUC_JP_ENCODING, "  ", 0);
  copy_words_from_tt(seq, xs, ANTHY_UTF8_ENCODING, " p", 0);
  copy_words_from_tt(seq, xs, ANTHY_EUC_JP_ENCODING, "  ", 1);
  copy_words_from_tt(seq, xs, ANTHY_UTF8_ENCODING, " p", 1);
  /**/
  if (!anthy_select_section("UNKNOWN_WORD", 0) &&
      !anthy_select_row(xs, 0)) {
    wtype_t wt;
    xstr *word_xs;
    anthy_type_to_wtype("#T35", &wt);
    word_xs = anthy_get_nth_xstr(0);
    anthy_mem_dic_push_back_dic_ent(seq, 0, word_xs, wt, NULL, 10, 0);
  }
}

int
anthy_parse_word_line(const char *line, struct word_line *res)
{
  int i;
  const char *buf = line;
  /* default values */
  res->wt[0] = 0;
  res->freq = 1;
  res->word = NULL;
  /* 品詞と頻度をparse */
  for (i = 0; i < 9 && *buf && *buf != '*' && *buf != ' '; buf++, i++) {
    res->wt[i] = *buf;
  }
  res->wt[i] = 0;
  if (*buf == '*') {
    buf ++;
    sscanf(buf, "%d", &res->freq);
    buf = strchr(buf, ' ');
  } else {
    res->freq = 1;
  }
  if (!buf || !(*buf)) {
    res->word = "";
    return -1;
  }
  buf++;
  /* 単語 */
  res->word = buf;
  return 0;
}

static void
anthy_ask_scan_option(void (*request_scan)(struct textdict *, void *),
	       void *arg, int is_old)
{
  struct textdict *private_text_dic;
  struct textdict *imported_text_dic;
  const char *imported_dic_dir;
  DIR *dir;
  struct dirent *de;
  int size = 0;
  /**/
  if (is_old) {
    private_text_dic = old_anthy_private_text_dic;
    imported_text_dic = old_anthy_imported_text_dic;
    imported_dic_dir = old_anthy_imported_dic_dir;
  } else {
    private_text_dic = anthy_private_text_dic;
    imported_text_dic = anthy_imported_text_dic;
    imported_dic_dir = anthy_imported_dic_dir;
  }
  request_scan(private_text_dic, arg);
  request_scan(imported_text_dic, arg);
  dir = opendir(imported_dic_dir);
  if (!dir) {
    return ;
  }
  while ((de = readdir(dir))) {
    struct stat st_buf;
    struct textdict *td;
    char *fn = malloc(strlen(imported_dic_dir) +
		      strlen(de->d_name) + 3);
    if (!fn) {
      break;
    }
    sprintf(fn, "%s/%s", imported_dic_dir, de->d_name);
    if (stat(fn, &st_buf)) {
      free(fn);
      continue;
    }
    if (!S_ISREG(st_buf.st_mode)) {
      free(fn);
      continue;
    }
    size += st_buf.st_size;
    if (size > MAX_DICT_SIZE) {
      free(fn);
      break;
    }
    td = anthy_textdict_open(fn);
    request_scan(td, arg);
    anthy_textdict_close(td);
    free(fn);
  }
  closedir(dir);
}

void
anthy_ask_scan(void (*request_scan)(struct textdict *, void *),
	       void *arg)
{
  anthy_ask_scan_option(request_scan, arg, 0);
  anthy_ask_scan_option(request_scan, arg, 1);
}

static void
add_unknown_word(xstr *yomi, xstr *word)
{
  /* recordに追加 */
  if (anthy_select_section("UNKNOWN_WORD", 1)) {
    return ;
  }
  if (!anthy_select_row(yomi, 0)) {
    anthy_mark_row_used();
  }
  if (anthy_select_row(yomi, 1)) {
    return ;
  }
  anthy_set_nth_xstr(0, word);
}

void
anthy_add_unknown_word(xstr *yomi, xstr *word)
{
  if (!(anthy_get_xstr_type(word) & XCT_KATA) &&
      !(anthy_get_xstr_type(word) & XCT_HIRA)) {
    return ;
  }
  if (yomi->len < 4 || yomi->len > 30) {
    return ;
  }
  /**/
  add_unknown_word(yomi, word);
}

void
anthy_forget_unused_unknown_word(xstr *xs)
{
  char key_buf[128];
  char *v;

  if (!anthy_private_tt_dic) {
    return ;
  }

  v = anthy_xstr_to_cstr(xs, ANTHY_UTF8_ENCODING);
  sprintf(key_buf, " U%s 0", v);
  free(v);
  anthy_trie_delete(anthy_private_tt_dic, key_buf);

  /* recordに記録された物を消す */
  if (anthy_select_section("UNKNOWN_WORD", 0)) {
    return ;
  }
  if (!anthy_select_row(xs, 0)) {
    anthy_release_row();
  }
}

static void
anthy_init_private_dic_option(const char *id, int is_old)
{
  struct text_trie *tt_dic;
  struct textdict *private_text_dic;
  struct textdict *imported_text_dic;
  char *imported_dic_dir;
  const char *home = anthy_get_user_dir(is_old);
  /**/
  if (is_old) {
    tt_dic = old_anthy_private_tt_dic;
    private_text_dic = old_anthy_private_text_dic;
    imported_text_dic = old_anthy_imported_text_dic;
  } else {
    tt_dic = anthy_private_tt_dic;
    private_text_dic = anthy_private_text_dic;
    imported_text_dic = anthy_imported_text_dic;
  }
  if (tt_dic) {
    anthy_trie_close(tt_dic);
  }
  anthy_textdict_close(private_text_dic);
  anthy_textdict_close(imported_text_dic);
  /**/
  if (!is_old) {
    if (lock_fn) {
      free(lock_fn);
    }
    init_lock_fn(home, id);
  }
  tt_dic = open_tt_dic(home, id);
  /**/
  private_text_dic = open_textdic(home, "private_words_", id);
  imported_text_dic = open_textdic(home, "imported_words_", id);
  imported_dic_dir = malloc(strlen(home) + strlen(id) + 23);
  sprintf(imported_dic_dir, "%s/imported_words_%s.d/", home, id);
  if (is_old) {
    old_anthy_private_tt_dic = tt_dic;
    old_anthy_private_text_dic = private_text_dic;
    old_anthy_imported_text_dic = imported_text_dic;
    old_anthy_imported_dic_dir = imported_dic_dir;
  } else {
    anthy_private_tt_dic = tt_dic;
    anthy_private_text_dic = private_text_dic;
    anthy_imported_text_dic = imported_text_dic;
    anthy_imported_dic_dir = imported_dic_dir;
  }
}

void
anthy_init_private_dic(const char *id)
{
  anthy_init_private_dic_option(id, 1);
  anthy_init_private_dic_option(id, 0);
}

static void
anthy_release_private_dic_option(int is_old)
{
  struct text_trie *tt_dic;
  struct textdict *private_text_dic;
  struct textdict *imported_text_dic;
  char *imported_dic_dir;
  if (is_old) {
    tt_dic = old_anthy_private_tt_dic;
    private_text_dic = old_anthy_private_text_dic;
    imported_text_dic = old_anthy_imported_text_dic;
    imported_dic_dir = old_anthy_imported_dic_dir;
  } else {
    tt_dic = anthy_private_tt_dic;
    private_text_dic = anthy_private_text_dic;
    imported_text_dic = anthy_imported_text_dic;
    imported_dic_dir = anthy_imported_dic_dir;
  }
  if (tt_dic) {
    anthy_trie_close(tt_dic);
    tt_dic = NULL;
  }
  /**/
  anthy_textdict_close(private_text_dic);
  anthy_textdict_close(imported_text_dic);
  free(imported_dic_dir);
  /**/
  if (is_old) {
    old_anthy_private_text_dic = NULL;
    old_anthy_imported_text_dic = NULL;
    old_anthy_imported_dic_dir = NULL;
    free (old_anthy_private_dir);
    old_anthy_private_dir = NULL;
  } else {
    anthy_private_text_dic = NULL;
    anthy_imported_text_dic = NULL;
    anthy_imported_dic_dir = NULL;
    free (anthy_private_dir);
    anthy_private_dir = NULL;
    if (lock_depth > 0) {
      /* not sane situation */
      lock_depth = 0;
      if (lock_fn) {
        unlink(lock_fn);
      }
    }
    /**/
    free(lock_fn);
    lock_fn = NULL;
  }
}

void
anthy_release_private_dic(void)
{
  anthy_release_private_dic_option(0);
  anthy_release_private_dic_option(1);
}
