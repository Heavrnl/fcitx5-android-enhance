/*
 * 例文の中を検索できるデータ構造を作る
 * 現時点では例文をすべて入れているが、そのうちフィルターすることも考えられる
 *
 * Copyright (C) 2007 TABATA Yusuke
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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <anthy/corpus.h>
#include <anthy/logger.h>

#define MAX_NR_VAL 8
#define BUCKET_SIZE 8192
#define MAX_COLLISION 8

/* word in source */
struct node {
  int nr;
  int val[MAX_NR_VAL];
  int flags;
};

/* word feature in corpus file */
struct element {
  /* hash値 */
  int val;
  /* 有効な(ELM_INVALIDの無い)エントリとしてのindex */
  int idx;
  /* このhash値の次の出現場所 */
  int next_idx;
  /**/
  int flags;
};

/* index */
struct bucket {
  /* 検索のキー */
  int key;
  /* 最初の出現場所 */
  int first_idx;
  /**/
  int last_idx;
};

struct corpus {
  /**/
  struct node *array;
  int nr_node;
  int array_size;

  /**/
  int nr_values;
  struct element *elms;
  /**/
  int nr_buckets;
  struct bucket *buckets;

  /**/
  int bucket_collision;
};

static void
corpus_setup_bucket(struct corpus *c, int nr)
{
  int i;
  free(c->buckets);
  /**/
  c->nr_buckets = nr;
  c->buckets = malloc(sizeof(struct bucket) * nr);
  for (i = 0; i < nr; i++) {
    c->buckets[i].key = -1;
    c->buckets[i].first_idx = -1;
    c->buckets[i].last_idx = -1;
  }
}

struct corpus *
corpus_new(void)
{
  struct corpus *c = malloc(sizeof(*c));
  if (!c) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return NULL;
  }
  c->nr_node = 0;
  c->array_size = 0;
  c->array = NULL;
  c->nr_values = 0;
  c->elms = NULL;
  c->buckets = NULL;
  c->bucket_collision = 0;
  return c;
}

static void
corpus_ensure_array(struct corpus *c, int nr)
{
  int i, size;
  if (c->array_size >= nr) {
    return ;
  }
  size = nr * 2;
  c->array = realloc(c->array, sizeof(struct node) * size);
  for (i = c->array_size; i < size; i++) {
    c->array[i].nr = 0;
  }
  c->array_size = nr;
}

void
corpus_dump(struct corpus *c)
{
  int i;
  for (i = 0; i < c->nr_values; i++) {
    if (c->elms[i].flags & ELM_WORD_BORDER) {
      printf("%d:", i);
    }
    printf("%d(%d) ", c->elms[i].val, c->elms[i].next_idx);
  }
  printf("\n");
}

static int
count_nr_valid_values(struct corpus *c)
{
  int i;
  int nr = 0;
  for (i = 0; i < c->nr_node; i++) {
    struct node *nd = &c->array[i];
    if (nd->flags & ELM_INVALID) {
      continue;
    }
    nr += nd->nr;
  }
  return nr;
}

static void
corpus_build_flatten(struct corpus *c)
{
  int i, j;
  int idx = 0;
  int nr_valid_elms = count_nr_valid_values(c);
  assert(c);
  if (!(c->elms = calloc(nr_valid_elms, sizeof(struct element)))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  for (i = 0; i < c->nr_node; i++) {
    struct node *nd = &c->array[i];
    if (nd->flags & ELM_INVALID) {
      continue;
    }
    for (j = 0; j < nd->nr; j++) {
      c->elms[idx].val = nd->val[j];
      c->elms[idx].next_idx = -1;
      c->elms[idx].flags = nd->flags;
      if (j == 0)
        c->elms[idx].flags |= ELM_WORD_BORDER;
      c->elms[idx].idx = idx;
      idx++;
    }
  }
}

static struct bucket *
find_bucket(struct corpus *c, int val)
{
  int i;
  int h;
  assert(c);
  assert(c->buckets);
  h = val % c->nr_buckets;
  for (i = 0; i < MAX_COLLISION; i++) {
    struct bucket *bkt = &c->buckets[h];
    if (bkt->key == val) {
      return bkt;
    }
    if (bkt->key == -1) {
      bkt->key = val;
      return bkt;
    }
    /**/
    h++;
    h %= c->nr_buckets;
  }
  c->bucket_collision++;
  return NULL;
}

static void
corpus_build_link(struct corpus *c)
{
  int i;
  assert(c);
  if (!(c->elms)) {
    anthy_log(0, "c->elms should not be null.\n");
    return;
  }
  for (i = 0; i < c->nr_values; i++) {
    struct element *elm = &c->elms[i];
    struct bucket *bkt = find_bucket(c, elm->val);
    if (!bkt) {
      continue;
    }
    if (bkt->first_idx < 0) {
      /* 最初の出現 */
      bkt->first_idx = c->elms[i].idx;
    } else {
      c->elms[bkt->last_idx].next_idx = c->elms[i].idx;
    }
    bkt->last_idx = c->elms[i].idx;
    c->elms[i].next_idx = -1;
  }
}

void
corpus_build(struct corpus *c)
{
  corpus_setup_bucket(c, BUCKET_SIZE);
  /**/
  corpus_build_flatten(c);
  corpus_build_link(c);
  /**/
}

void
corpus_push_back(struct corpus *c, int *val, int nr, int flags)
{
  struct node nd;
  int i;
  for (i = 0; i < nr; i++) {
    nd.val[i] = val[i];
  }
  nd.nr = nr;
  nd.flags = flags;
  /**/
  corpus_ensure_array(c, c->nr_node+1);
  c->array[c->nr_node] = nd;
  c->nr_node++;
  c->nr_values += nd.nr;
}

void
corpus_write_bucket(FILE *fp, struct corpus *c)
{
  int i;
  fprintf(fp, "0 %d\n", c->nr_buckets);
  for (i = 0; i < c->nr_buckets; i++) {
    fprintf(fp, "%d,%d\n", (c->buckets[i].key & CORPUS_KEY_MASK),
	    c->buckets[i].first_idx);
  }
  printf(" %d collisions in corpus bucket\n", c->bucket_collision);
}

void
corpus_write_array(FILE *fp, struct corpus *c)
{
  int i;
  fprintf(fp, "0 %d\n", c->nr_values);
  for (i = 0; i < c->nr_values; i++) {
    struct element *elm = &c->elms[i];
    int val;
    val = elm->val;
    val &= CORPUS_KEY_MASK;
    if (elm->flags & ELM_BOS) {
      val |= ELM_BOS;
    }
    if (elm->flags & ELM_WORD_BORDER) {
      val |= ELM_WORD_BORDER;
    }
    fprintf(fp, "%d,%d\n", val,
	    c->elms[i].next_idx);
  }
}

void
corpus_free (struct corpus *c)
{
  if (!c)
    return;
  free (c->array);
  free (c->elms);
  free (c->buckets);
  free (c);
}
