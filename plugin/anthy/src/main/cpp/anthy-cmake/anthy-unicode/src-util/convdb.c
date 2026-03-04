/*
 * 変換エンジンの内部情報を使うため、意図的に
 * layer violationを放置している。
 *
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <anthy/anthy.h>
#include <anthy/convdb.h>
#include <anthy/segment.h>
#include <anthy/feature_set.h>
/**/
#include "../src-main/main.h"
#include "../src-splitter/wordborder.h"
#include "../src-worddic/dic_ent.h"


/* 自立語部か付属語部か */
#define WORD_INDEP 0
#define WORD_DEP 1

/* 単語(自立語or付属語) */
struct word {
  /* WORD_* */
  int type;
  /* 付属語のhash(WORD_INDEP)もしくは変換後の文字列のhash(WORD_DEP) */
  int hash;
  /* 読みの文字列のhash */
  int yomi_hash;
  /* 変換前の文字列 */
  xstr *raw_xs;
  /* 変換後の文字列 */
  xstr *conv_xs;
  /* 変換後の品詞 */
  const char *wt;
};

static struct cand_ent *
selected_candidate(struct seg_ent *seg)
{
  if (seg->committed > -1) {
    return seg->cands[seg->committed];
  }
  return seg->cands[0];
}

static void
get_res(anthy_context_t ac, char *res_buf, int conv)
{
  struct anthy_conv_stat acs;
  int i;

  anthy_get_stat(ac, &acs);
  res_buf[0] = 0;
  if (!conv) {
    strcat(res_buf, "|");
  }
  for (i = 0; i < acs.nr_segment; i++) {
    char buf[1024];
    if (conv) {
      anthy_get_segment(ac, i, 0, buf, 1024);
      strcat(res_buf, buf);
    } else {
      anthy_get_segment(ac, i, NTH_UNCONVERTED_CANDIDATE, buf, 1024);
      strcat(res_buf, buf);
      strcat(res_buf, "|");
    }
  }
}

static struct conv_res *
do_find_conv_res(struct res_db *db, const char *src, const char *res)
{
  struct conv_res *cr;

  for (cr = db->res_list.next; cr; cr = cr->next) {
    if (((!cr->res_str && !res) ||
        (cr->res_str && res && !strcmp(cr->res_str, res))) &&
        (cr->src_str && src && !strcmp(cr->src_str, src))) {
      return cr;
    }
  }
  cr = (struct conv_res *)malloc(sizeof(struct conv_res));
  cr->src_str = strdup(src);
  if (res) {
    cr->res_str = strdup(res);
  } else {
    cr->res_str = NULL;
  }
  cr->cand_str = NULL;
  cr->check = CHK_UNKNOWN;
  cr->used = 0;
  cr->cand_check = NULL;
  /**/
  db->tail->next = cr;
  cr->next = NULL;
  db->tail = cr;
  return cr;
}

struct conv_res *
find_conv_res(struct res_db *db, anthy_context_t ac,
	      const char *src, int conv)
{
  char res_buf[1024];
  get_res(ac, res_buf, conv);

  return do_find_conv_res(db, src, res_buf);
}

static void
chomp_line(char *buf)
{
  int len = strlen(buf);
  if (buf[len-1] == '\n') {
    buf[len-1] = 0;
  }
}

struct res_db *
create_db(void)
{
  struct res_db *db;

  db = malloc(sizeof(struct res_db));
  db->res_list.next = NULL;
  db->tail = &db->res_list;
  db->total = 0;
  db->res.unknown = 0;
  db->res.ok = 0;
  db->res.miss = 0;
  db->res.dontcare = 0;
  db->split.unknown = 0;
  db->split.ok = 0;
  db->split.miss = 0;
  db->split.dontcare = 0;

  return db;
}

static void
strip_separator_vbar(char *buf, const char *str)
{
  const char *src = str;
  char *dst = buf;
  while (*src) {
    if (*src != '|' && *src != '~') {
      *dst = *src;
      dst ++;
    }
    src ++;
  }
  *dst = 0;
}

static void
parse_line(struct res_db *db, char *line)
{
  char buf1[1024], buf2[1024], buf3[1024], buf4[1024];
  char *src, *res;
  const char *check;
  struct conv_res *cr;
  int nr;
  chomp_line(line);
  if (line[0] == '#' || line[0] == 0) {
    return ;
  }
  nr = sscanf(line, "%s %s %s", buf1, buf2, buf3);
  if (nr == 1) {
    cr = do_find_conv_res(db, buf1, NULL);
    cr->check = CHK_UNKNOWN;
    return ;
  }
  if (nr < 2) {
    return ;
  }
  if (buf1[0] != '|') {
    /* buf1 buf2    buf3
     * 平文 区切り文
     * 平文 区切り文 変換後
     * 平文 区切り文 check
     */
    src = buf1;
    res = buf2;
    if (nr == 3) {
      check = buf3;
    } else {
      check = "?";
    }
  } else {
    /* buf1    buf2  (buf3)
     * 区切り文
     * 区切り文 変換後
     * 区切り文 check
     */
    strip_separator_vbar(buf4, buf1);
    src = buf4;
    res = buf1;
    check = buf2;
  }
  cr = do_find_conv_res(db, src, res);
  if (nr == 2 && check[0] != '|') {
    cr->check = CHK_OK;
    return ;
  }
  if (check[0] == 'O') {
    cr->check = CHK_OK;
  } else if (check[0] == 'X') {
    cr->check = CHK_MISS;
  } else if (check[0] == '*') {
    cr->check = CHK_DONTCARE;
  } else if (check[0] == '|') {
    cr->check = CHK_UNKNOWN;
    cr->cand_str = strdup(check);
  } else {
    cr->check = CHK_UNKNOWN;
  }
}

void
read_db(struct res_db *db, const char *fn)
{
  FILE *fp;
  char line[1024];

  if (!fn) {
    return ;
  }
  fp = fopen(fn, "r");
  if (!fp) {
    return ;
  }
  while (fgets(line, 1024, fp)) {
    parse_line(db, line);
  }
  fclose(fp);
}

static void
fill_conv_info(struct word *w, struct cand_elm *elm)
{
  /*w->conv_xs, w->wt*/
  struct dic_ent *de;
  if (elm->nth == -1 ||
      elm->nth >= elm->se->nr_dic_ents) {
    w->conv_xs = NULL;
    w->wt = NULL;
    return ;
  }
  if (!elm->se->dic_ents) {
    w->conv_xs = NULL;
    w->wt = NULL;
    return ;
  }
  /**/
  de = elm->se->dic_ents[elm->nth];
  w->conv_xs = anthy_xstr_dup(&de->str);
  w->wt = de->wt_name;
  w->hash = anthy_xstr_hash(w->conv_xs);
}

static void
init_word(struct word *w, int type)
{
  w->type = type;
  w->raw_xs = NULL;
  w->conv_xs = NULL;
  w->wt = NULL;
}

static void
free_word(struct word *w)
{
  anthy_free_xstr(w->raw_xs);
  anthy_free_xstr(w->conv_xs);
}

/* 自立語を作る */
static void
fill_indep_word(struct word *w, struct cand_elm *elm)
{
  init_word(w, WORD_INDEP);
  /* 変換前の読みを取得する */
  w->raw_xs = anthy_xstr_dup(&elm->str);
  w->yomi_hash = anthy_xstr_hash(w->raw_xs);
  w->hash = 0;
  /**/
  fill_conv_info(w, elm);
}

/* 付属語を作る */
static void
fill_dep_word(struct word *w, struct cand_elm *elm)
{
  init_word(w, WORD_DEP);
  /**/
  w->hash = anthy_xstr_hash(&elm->str);
  w->yomi_hash = w->hash;
  w->raw_xs = anthy_xstr_dup(&elm->str);
}

static void
print_features(struct feature_list *fl)
{
  int i, nr;
  if (!fl) {
    return ;
  }
  nr = anthy_feature_list_nr(fl);
  if (nr == 0) {
    return ;
  }
  printf(" features=");
  for (i = 0; i < nr; i++) {
    if (i > 0) {
      printf(",");
    }
    printf("%d", anthy_feature_list_nth(fl, i));
  }
}

static void
print_word(const char *prefix, struct word *w, struct feature_list *fl)
{
  printf("%s", prefix);
  if (w->type == WORD_DEP) {
    /* 付属語 */
    printf("dep_word hash=%d ", w->hash);
    anthy_putxstrln(w->raw_xs);
    return ;
  }
  /* 自立語 */
  printf("indep_word hash=%d", w->hash);
  /**/
  if (fl) {
    print_features(fl);
  }
  /* 品詞 */
  if (w->wt) {
    printf(" %s", w->wt);
  } else {
    printf(" null");
  }
  /* 文字列 */
  if (w->conv_xs) {
    printf(" ");
    anthy_putxstr(w->conv_xs);
  } else {
    printf(" null");
  }
  printf(" ");
  anthy_putxstrln(w->raw_xs);
}

/** segの文節クラスを返す
 * segがnullであれば、clをクラスとする
 */
static int
get_seg_class(struct seg_ent *seg, int cl)
{
  struct cand_ent *ce;
  if (!seg) {
    return cl;
  }
  ce = selected_candidate(seg);
  if (ce->mw) {
    return ce->mw->seg_class;
  }
  return SEG_BUNSETSU;
}

static void
set_features(struct feature_list *fl,
	     struct seg_ent *prev_seg,
	     struct seg_ent *cur_seg)
{
  int cl, pc;
  cl = get_seg_class(cur_seg, SEG_TAIL);
  pc = get_seg_class(prev_seg, SEG_HEAD);

  anthy_feature_list_set_cur_class(fl, cl);
  if (cur_seg) {
    struct cand_ent *ce =  selected_candidate(cur_seg);
    anthy_feature_list_set_dep_word(fl, ce->dep_word_hash);
    if (ce->mw) {
      anthy_feature_list_set_dep_class(fl, ce->mw->dep_class);
      anthy_feature_list_set_mw_features(fl, ce->mw->mw_features);
      anthy_feature_list_set_noun_cos(fl, ce->mw->core_wt);
    }
  }
  anthy_feature_list_set_class_trans(fl, pc, cl);
  /**/
  anthy_feature_list_sort(fl);
}

static void
print_element(const char *prefix,
	      struct cand_elm *elm, struct feature_list *fl)
{
  struct word w;

  if (elm->str.len == 0) {
    return ;
  }
  if (elm->id != -1) {
    /* 自立語 */
    fill_indep_word(&w, elm);
    print_word(prefix, &w, fl);
  } else {
    /* 付属語 */
    fill_dep_word(&w, elm);
    print_word(prefix, &w, NULL);
  }
  free_word(&w);
}

static void
print_unconverted(struct cand_ent *ce)
{
  printf("unknown ");
  anthy_putxstrln(&ce->str);
}

static void
print_eos(struct seg_ent *prev_seg)
{
  struct feature_list fl;
  anthy_feature_list_init(&fl);
  set_features(&fl, prev_seg, NULL);
  printf("eos ");
  print_features(&fl);
  printf("\n");
  anthy_feature_list_free(&fl);
}

/* 候補のミスには '~'、文節長のミスには '!'を付ける
 * 同じ文節内の二つめ以降の自立語には '^'を付ける
 */
static const char *
get_prefix(int flag)
{
  if (flag & CONV_INVALID) {
    return "^";
  }
  if (flag & CONV_SIZE_MISS) {
    return "!";
  }
  if (flag & CONV_CAND_MISS) {
    return "~";
  }
  return "";
}

static void
print_segment_info(int is_negative,
		   struct seg_ent *prev_seg,
		   struct seg_ent *seg)
{
  int i;
  struct feature_list fl;
  struct cand_ent *ce =  selected_candidate(seg);
  int nr_indep = 0;
  const char *prefix = NULL;

  anthy_feature_list_init(&fl);
  set_features(&fl, prev_seg, seg);
  for (i = 0; i < ce->nr_words; i++) {
    struct cand_elm *elm = &ce->elm[i];
    prefix = get_prefix(is_negative);
    if (nr_indep > 0 && elm->id != -1) {
      prefix = get_prefix(is_negative | CONV_INVALID);
    }
    /* 出力する */
    print_element(prefix, elm, &fl);
    /* 自立語を数える */
    if (elm->id != -1) {
      nr_indep ++;
    }
  }
  anthy_feature_list_free(&fl);
}

void
print_size_miss_segment_info(anthy_context_t ac, int nth)
{
  struct seg_ent *prev_seg = NULL;
  struct seg_ent *seg = anthy_get_nth_segment(&ac->seg_list, nth);
  if (nth > 0) {
    prev_seg = anthy_get_nth_segment(&ac->seg_list, nth - 1);
  }
  print_segment_info(CONV_SIZE_MISS, prev_seg, seg);
}

void
print_cand_miss_segment_info(anthy_context_t ac, int nth)
{
  struct seg_ent *prev_seg = NULL;
  struct seg_ent *seg = anthy_get_nth_segment(&ac->seg_list, nth);
  if (nth > 0) {
    prev_seg = anthy_get_nth_segment(&ac->seg_list, nth - 1);
  }
  print_segment_info(CONV_CAND_MISS, prev_seg, seg);
}

void
print_context_info(anthy_context_t ac, struct conv_res *cr)
{
  int i;
  struct seg_ent *prev_seg = NULL;

  printf("segments: %d\n", ac->seg_list.nr_segments);
  /* 各文節に対して */
  for (i = 0; i < ac->seg_list.nr_segments; i++) {
    struct seg_ent *seg = anthy_get_nth_segment(&ac->seg_list, i);
    struct cand_ent *ce = selected_candidate(seg);
    int is_negative = 0;
    if (cr && cr->cand_check && cr->cand_check[i]) {
      is_negative = CONV_CAND_MISS;
    }

    /* 各要素に対して */
    if (!ce->nr_words) {
      /* 要素が無いものはそのまま表示 */
      print_unconverted(ce);
    } else {
      /* 候補の変更があった場合はそれを表示 */
      if (seg->committed > 0) {
	int tmp = seg->committed;
	seg->committed = 0;
	print_cand_miss_segment_info(ac, i);
	seg->committed = tmp;
      }
      /* 文節の構成を表示 */
      print_segment_info(is_negative, prev_seg, seg);
    }
    /**/
    prev_seg = seg;
  }
  print_eos(prev_seg);
  printf("\n");
}
