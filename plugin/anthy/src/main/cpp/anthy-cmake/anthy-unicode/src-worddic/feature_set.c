/* features
 *
 * 素性の番号と意味を隠蔽して管理する
 *
 * Copyright (C) 2006-2007 TABATA Yusuke
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <anthy/segclass.h>
#include <anthy/feature_set.h>
/* for MW_FEATURE* constants */
#include <anthy/splitter.h>

/* 素性の番号
 *
 * 0-19 クラス素性
 * 30-319(30+SEG_SIZE^2) クラス遷移属性
 * 540-579 その他
 * 580- (1024個) 付属語の種類
 */

#define CUR_CLASS_BASE 0
#define DEP_TYPE_FEATURE_BASE 20
#define CLASS_TRANS_BASE 30
#define FEATURE_SV 542
#define FEATURE_WEAK 543
#define FEATURE_SUFFIX 544
#define FEATURE_NUM 546
#define FEATURE_CORE1 547
#define FEATURE_HIGH_FREQ 548
#define FEATURE_WEAK_SEQ 549
#define COS_BASE 573
#define DEP_FEATURE_BASE 580

void
anthy_feature_list_init(struct feature_list *fl)
{
  fl->nr = 0;
  fl->size = NR_EM_FEATURES;
}

void
anthy_feature_list_free(struct feature_list *fl)
{
  (void)fl;
}

void
anthy_feature_list_add(struct feature_list *fl, int f)
{
  if (fl->nr < NR_EM_FEATURES) {
    fl->u.index[fl->nr] = f;
    fl->nr++;
  }
}

int
anthy_feature_list_nr(const struct feature_list *fl)
{
  return fl->nr;
}

int
anthy_feature_list_nth(const struct feature_list *fl, int nth)
{
  return fl->u.index[nth];
}

static int
cmp_short(const void *p1, const void *p2)
{
  return *((short *)p1) - *((short *)p2);
}

void
anthy_feature_list_sort(struct feature_list *fl)
{
  qsort(fl->u.index, fl->nr, sizeof(fl->u.index[0]),
	cmp_short);
}


void
anthy_feature_list_set_cur_class(struct feature_list *fl, int cl)
{
  anthy_feature_list_add(fl, CUR_CLASS_BASE + cl);
}

void
anthy_feature_list_set_class_trans(struct feature_list *fl, int pc, int cc)
{
  anthy_feature_list_add(fl, CLASS_TRANS_BASE + pc * SEG_SIZE + cc);
}

void
anthy_feature_list_set_dep_word(struct feature_list *fl, int h)
{
  anthy_feature_list_add(fl, h + DEP_FEATURE_BASE);
}

void
anthy_feature_list_set_dep_class(struct feature_list *fl, int c)
{
  anthy_feature_list_add(fl, c + DEP_TYPE_FEATURE_BASE);
}

void
anthy_feature_list_set_noun_cos(struct feature_list *fl, wtype_t wt)
{
  int c;
  if (anthy_wtype_get_pos(wt) != POS_NOUN) {
    return ;
  }
  c = anthy_wtype_get_cos(wt);
  if (c == COS_SUFFIX) {
    anthy_feature_list_add(fl, COS_BASE + c);
  }
}

void
anthy_feature_list_set_mw_features(struct feature_list *fl, int mask)
{
  if (mask & MW_FEATURE_WEAK_CONN) {
    anthy_feature_list_add(fl, FEATURE_WEAK);
  }
  if (mask & MW_FEATURE_SUFFIX) {
    anthy_feature_list_add(fl, FEATURE_SUFFIX);
  }
  if (mask & MW_FEATURE_SV) {
    anthy_feature_list_add(fl, FEATURE_SV);
  }
  if (mask & MW_FEATURE_NUM) {
    anthy_feature_list_add(fl, FEATURE_NUM);
  }
  if (mask & MW_FEATURE_CORE1) {
    anthy_feature_list_add(fl, FEATURE_CORE1);
  }
  if (mask & MW_FEATURE_HIGH_FREQ) {
    anthy_feature_list_add(fl, FEATURE_HIGH_FREQ);
  }
  if (mask & MW_FEATURE_WEAK_SEQ) {
    anthy_feature_list_add(fl, FEATURE_WEAK_SEQ);
  }
}

void
anthy_feature_list_print(struct feature_list *fl)
{
  int i;
  printf("features=");
  for (i = 0; i < fl->nr; i++) {
    if (i) {
      printf(",");
    }
    printf("%d", fl->u.index[i]);
  }
  printf("\n");
}

static int
compare_line(const void *kp, const void *cp)
{
  const int *f = kp;
  const struct feature_freq *c = cp;
  int i;
  for (i = 0; i < NR_EM_FEATURES; i++) {
    if (f[i] != (int)ntohl(c->f[i])) {
      return f[i] - ntohl(c->f[i]);
    }
  }
  return 0;
}

struct feature_freq *
anthy_find_array_freq(const void *image, int *f, int nr,
		      struct feature_freq *arg)
{
  struct feature_freq *res;
  int nr_lines, i;
  const int *array = (int *)image;
  int n[NR_EM_FEATURES];
  if (!image) {
    return NULL;
  }
  /* コピーする */
  for (i = 0; i < NR_EM_FEATURES; i++) {
    if (i < nr) {
      n[i] = f[i];
    } else {
      n[i] = 0;
    }
  }
  /**/
  nr_lines = ntohl(array[1]);
  res = bsearch(n, &array[16], nr_lines,
		sizeof(struct feature_freq),
		compare_line);
  if (!res) {
    return NULL;
  }
  for (i = 0; i < NR_EM_FEATURES + 2; i++) {
    arg->f[i] = ntohl(res->f[i]);
  }
  return arg;
}

struct feature_freq *
anthy_find_feature_freq(const void *image,
			const struct feature_list *fl,
			struct feature_freq *arg)
{
  int i, nr;
  int f[NR_EM_FEATURES + 2];

  /* 配列にコピーする */
  nr = anthy_feature_list_nr(fl);
  /* From anthy_feature_list_add(), nr should <= NR_EM_FEATURES.
   * And i should be < NR_EM_FEATURES for anthy_feature_list_nth()
   * which accesses fl->u.index[i].
   */
  assert(nr <= NR_EM_FEATURES);
  for (i = 0; i < NR_EM_FEATURES + 2; i++) {
    if (i < nr) {
      f[i] = anthy_feature_list_nth(fl, i);
    } else {
      f[i] = 0;
    }
  }
  return anthy_find_array_freq(image, f, NR_EM_FEATURES, arg);
}

void
anthy_init_features(void)
{
}
