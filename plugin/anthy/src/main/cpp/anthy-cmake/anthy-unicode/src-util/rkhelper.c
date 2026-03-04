/*
 * ローマ字から平仮名（正確にはキーの列から文字）の表(rk_map)の
 * カスタマイズを管理する
 *
 * Copyright (C) 2001-2002 UGAWA Tomoharu
 * Copyright (C) 2002 Tabata Yusuke
 *
 * Funded by IPA未踏ソフトウェア創造事業 2001
 */

#include <string.h>
#include <stdlib.h>
#include "rkconv.h"
#include "rkhelper.h"

static const char* rk_default_symbol[128] = {
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  "　", "！", "”", "＃", "＄", "％", "＆", "’",
  "（", "）", "＊", "＋", "、", "ー", "。", "／",
  "０", "１", "２", "３", "４", "５", "６", "７",
  "８", "９", "：", "；", "＜", "＝", "＞", "？",

  "＠", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, "「", "＼", "」", "＾", "＿",
  "‘", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, "｛", "｜", "｝", "〜", NULL
};

struct rk_conf_ent {
  char *lhs;
  char *rhs;
  struct rk_conf_ent *next;
};

struct rk_option {
  int enable_default;
  char toggle; /* 英数との一時的な切替えに使うシンボル */
  /*
   * 配列はそれぞれリストの先頭になる
   * リストの先頭は一文字のエントリが入る
   */
  struct rk_conf_ent hiragana_symbol[128]; /* ひらがなとの対応 */
  struct rk_conf_ent katakana_symbol[128]; /* カタカナとの対応 */
  struct rk_conf_ent hankaku_kana_symbol[128]; /* カタカナとの対応 */
};

#include "rkmap.h"

struct rk_option *
anthy_input_create_rk_option()
{
  struct rk_option *opt;
  int i;

  opt = malloc(sizeof(struct rk_option));
  opt->enable_default = 1;
  opt->toggle = '/';
  for (i = 0; i < 128; i++) {
    opt->hiragana_symbol[i].rhs = NULL;
    opt->hiragana_symbol[i].lhs = NULL;
    opt->hiragana_symbol[i].next = NULL;
    opt->katakana_symbol[i].rhs = NULL;
    opt->katakana_symbol[i].lhs = NULL;
    opt->katakana_symbol[i].next = NULL;
    opt->hankaku_kana_symbol[i].rhs = NULL;
    opt->hankaku_kana_symbol[i].lhs = NULL;
    opt->hankaku_kana_symbol[i].next = NULL;
  }
  return opt;
}

int
anthy_input_free_rk_option(struct rk_option *opt)
{
  int err;

  err = anthy_input_do_clear_rk_option(opt, 1);
  free(opt);

  return err;
}

static struct rk_conf_ent *
find_rk_conf_ent(struct rk_option *opt, int map,
		 const char *key, int force)
{
  int c = key[0];
  struct rk_conf_ent *tab = NULL;
  struct rk_conf_ent *sym = NULL;

  if (c == 0) {
    return NULL;
  }

  if (map == RKMAP_HIRAGANA) {
    tab = opt->hiragana_symbol;
  }
  if (map == RKMAP_KATAKANA) {
    tab = opt->katakana_symbol;
  }
  if (map == RKMAP_HANKAKU_KANA) {
    tab = opt->hankaku_kana_symbol;
  }
  if (!tab) {
    return NULL;
  }
  if (strlen(key) == 1) {
    sym = &tab[c];
  } else {
    /* 2文字以上 */
    for (sym = tab[c].next; sym; sym = sym->next) {
      if (!strcmp(sym->lhs, key)) {
	break;
      }
    }
  }
  if (!sym && force) {
    /* メモリ確保してつなぐ */
    sym = malloc(sizeof(struct rk_conf_ent));
    sym->rhs = NULL;
    sym->lhs = NULL;
    sym->next = tab[c].next;
    tab[c].next = sym;
  }
  if (sym && !sym->lhs) {
    sym->lhs = strdup(key);
  }
  return sym;
}

/*
 * opt 変更対象のoption
 * map RKMAP_*
 * from 変換もとの文字
 * to 変換先の文字列
 * follow follow集合
 */
int
anthy_input_do_edit_rk_option(struct rk_option* opt, int map,
			      const char* from, const char* to, const char *follow)
{
  struct rk_conf_ent *tab;
  (void)follow;

  tab = find_rk_conf_ent(opt, map, from, 1);
  if (!tab) {
    return -1;
  }

  if (tab->rhs) {
    free(tab->rhs);
  }
  if (to == NULL) {
    tab->rhs = NULL;
  } else {
    tab->rhs = strdup(to);
  }
  return 0;
}

static void
free_rk_conf_ent(struct rk_conf_ent *e)
{
  if (e->lhs) {
    free(e->lhs);
    e->lhs = NULL;
  }
  if (e->rhs) {
    free(e->rhs);
    e->rhs = NULL;
  }
  e->next = NULL;
}

int
anthy_input_do_clear_rk_option(struct rk_option* opt,
			       int use_default)
{
  int i;

  opt->enable_default = use_default;
  for (i = 0; i < 128; i++) {
    /* 各文字に対して */
    struct rk_conf_ent *tab, *tmp;
    /* ひらがなのリストを解放 */
    for (tab = opt->hiragana_symbol[i].next; tab;) {
      tmp = tab;
      tab = tab->next;
      free_rk_conf_ent(tmp);
      free(tmp);
    }
    /* カタカナのリストを解放 */
    for (tab = opt->katakana_symbol[i].next; tab;) {
      tmp = tab;
      tab = tab->next;
      free_rk_conf_ent(tmp);
      free(tmp);
    }
    /* 先頭の一文字のエントリも忘れずに解放 */
    free_rk_conf_ent(&opt->katakana_symbol[i]);
    free_rk_conf_ent(&opt->hiragana_symbol[i]);
  }
  return 0;
}

int
anthy_input_do_edit_toggle_option(struct rk_option* opt,
				  char toggle)
{
  opt->toggle = toggle;
  return 0;
}

static void
rkrule_set(struct rk_rule* r,
	   const char* lhs, const char* rhs, const char* follow)
{
  r->lhs = lhs;
  r->rhs = rhs;
  r->follow = follow;
}

struct rk_map*
make_rkmap_ascii(struct rk_option* opt)
{
  struct rk_rule var_part[130];
  struct rk_rule* complete_rules;
  struct rk_map* map;
  struct rk_rule* p;
  char work[2*128];
  char* w;
  int c;

  (void)opt;
  p = var_part;
  w = work;
  for (c = 0; c < 128; c++) {
    if (rk_default_symbol[c]) {
      w[0] = c;
      w[1] = '\0';
      rkrule_set(p++, w, w, NULL);
      w += 2;
    }
  }
  p->lhs = NULL;

  complete_rules = rk_merge_rules(rk_rule_alphabet, var_part);
  map = rk_map_create(complete_rules);
  rk_rules_free(complete_rules);

  return map;
}

struct rk_map*
make_rkmap_wascii(struct rk_option* opt)
{
  (void)opt;
  return rk_map_create(rk_rule_walphabet);
}

struct rk_map*
make_rkmap_shiftascii(struct rk_option* opt)
{
  struct rk_rule var_part[130];
  struct rk_rule* complete_rules;
  struct rk_map* map;
  struct rk_rule* p;
  char work[2*128 + 3];
  char* w;
  int c;
  int toggle_char = opt->toggle;

  p = var_part;
  w = work;
  for (c = 0; c < 128; c++) {
    if (rk_default_symbol[c]) {
      if (c == toggle_char) {
	/* トグルする文字の場合 */
	w[0] = c;
	w[1] = '\0';
	rkrule_set(p++, w, "\xff" "o", NULL);
	w[2] = c;
	w[3] = c;
	w[4] = '\0';
	rkrule_set(p++, w + 2, w, NULL);
	w += 5;
      } else {
	/* 普通の文字の場合 */
	w[0] = c;
	w[1] = '\0';
	rkrule_set(p++, w, w, NULL);
	w += 2;
      }
    }
  }
  p->lhs = NULL;

  complete_rules = rk_merge_rules(rk_rule_alphabet, var_part);
  map = rk_map_create(complete_rules);
  rk_rules_free(complete_rules);

  return map;
}

static int
count_rk_rule_ent(struct rk_option *opt, int map_no)
{
  int i , c;
  struct rk_conf_ent *head;
  struct rk_conf_ent *ent;

  if (map_no == RKMAP_HIRAGANA) {
    head = opt->hiragana_symbol;
  } else if (map_no == RKMAP_HANKAKU_KANA) {
    head = opt->katakana_symbol;
  } else {
    head = opt->hankaku_kana_symbol;
  }

  c = 128;
  for (i = 0; i < 128; i++) {
    for (ent = head[i].next; ent; ent = ent->next) {
      if (ent->lhs) {
	c++;
      }
    }
  }
  return c;
}

/*
 * デフォルトのルールとカスタマイズされたルールをマージして
 * rk_mapを作る。
 */
static struct rk_map*
make_rkmap_hirakata(const struct rk_rule* rule,
		    struct rk_option *opt, int map_no)
{
  struct rk_conf_ent *tab;
  struct rk_rule* rk_var_part;
  struct rk_rule* complete_rules;
  struct rk_rule* p;
  struct rk_map* map;
  int toggle = opt->toggle;
  char *work;
  char* w;
  int c;
  int nr_rule;
  char buf[2];

  nr_rule = count_rk_rule_ent(opt, map_no);

  rk_var_part = alloca(sizeof(struct rk_rule) *(nr_rule + 2));
  work = alloca(2*128 + 8);
  p = rk_var_part;
  w = work;

  /* 一文字のものをrk_var_partに書き込んでいく */
  /* トグルの場合 */
  buf[0] = toggle;
  buf[1] = 0;
  w[0] = toggle;
  w[1] = '\0';
  w[2] = '\xff';
  w[3] = '0' + RKMAP_SHIFT_ASCII;
  w[4] = '\0';
  rkrule_set(p++, w, w + 2, NULL);
  w[5] = toggle;
  w[6] = toggle;
  w[7] = '\0';
  tab = find_rk_conf_ent(opt, map_no, buf, 0);
  if (tab && tab->rhs) {
    rkrule_set(p++, w + 5, tab->rhs, NULL);
  } else {
    rkrule_set(p++, w + 5, rk_default_symbol[toggle], NULL);
  }
  w += 8;
  /* トグル以外 */
  for (c = 0; c < 128; c++) {
    if (c != toggle) {
      buf[0] = c;
      buf[1] = 0;
      /* 一文字のもの */
      w[0] = c;
      w[1] = '\0';
      tab = find_rk_conf_ent(opt, map_no, buf, 0);
      if (tab && tab->rhs) {
	/* カスタマイズ済のがある */
	rkrule_set(p++, w, tab->rhs, NULL);
      } else if (rk_default_symbol[c]) {
	/* 記号など */
	rkrule_set(p++, w, rk_default_symbol[c], NULL);
      }
      w += 2;
      /* 二文字以上のもの */
      if (tab) {
	for (tab = tab->next; tab; tab = tab->next) {
	  rkrule_set(p++, tab->lhs, tab->rhs, NULL);
	}
      }
    }
  }
  p->lhs = NULL;

  if (opt->enable_default) {
    complete_rules = rk_merge_rules(rule, rk_var_part);
    map = rk_map_create(complete_rules);
    rk_rules_free(complete_rules);
  } else {
    map = rk_map_create(rk_var_part);
  }

  return map;
}

struct rk_map*
make_rkmap_hiragana(struct rk_option* opt)
{
  return make_rkmap_hirakata(rk_rule_hiragana,
			     opt, RKMAP_HIRAGANA);
}

struct rk_map*
make_rkmap_katakana(struct rk_option* opt)
{
  return make_rkmap_hirakata(rk_rule_katakana,
			     opt, RKMAP_KATAKANA);
}

struct rk_map *
make_rkmap_hankaku_kana(struct rk_option *opt)
{
  return make_rkmap_hirakata(rk_rule_hankaku_kana,
			     opt, RKMAP_HANKAKU_KANA);
}
