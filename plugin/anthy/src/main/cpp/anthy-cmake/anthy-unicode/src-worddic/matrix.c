/*
 * 疎行列を扱うためのコード
 *
 * (1) 行列(sparse_matrix)のインスタンスを作成し行列の要素を設定する
 * (2) 行列から行列イメージ(matrix_image)を作成する
 *  *  行列イメージをnetwork byteorderでファイルに書き出す
 * (3) 行列イメージを読み込み(or mmapする)要素にアクセスする
 *
 */
/*
 * sparse matrix crammer
 *
 *  sparse matrix storage uses following 2 sparse arrays
 *   *array of row
 *   *array of cells in a row
 *
 *(1/2)
 * sparse row    crammed row
 *  0:0                1:1
 *  1:1     ---->>     3:1
 *  2:0   hash(h)%m    7:1
 *  3:1       /
 *  4:0      /
 *  5:0     /
 *  6:0
 *  7:1
 *  8:0
 *     (?:1 means non-all 0 row)
 *(2/2)
 * crammed row      cram        shift count
 *  1:1    .    .    -> ..      shift 0
 *  3:1 .   .        ->   ..    shift 2
 *  7:1   .  .    .  ->     ... shift 4
 *
 *     contents of        |
 *         matrix        \|/
 *
 *                      ....... unified array of (value.column) pair
 *
 *  matrix image
 *   image[0] : length of hashed row array
 *   image[1] : length of crammed cell array
 *   image[2 ~ 2+image[0]-1] : hashed row array
 *   image[2+image[0] ~ 2+image[0]+image[1]-1] : hashed row array
 *
 * Copyright (C) 2005 TABATA Yusuke
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

#include <anthy/diclib.h>
/* public APIs */
#include <anthy/matrix.h>
#include <anthy/logger.h>

/* maximum length allowed for hash chain */
#define MAX_FAILURE 50

struct list_elm {
  int index;
  int value;
  void *ptr;
  struct list_elm *next;
  /* bypass to mitigate O(n) insertion cost */
  struct list_elm *orig_next;
};

struct array_elm {
  int index;
  int value;
  void *ptr;
};

/*
 * sparse array has two representation
 *
 *  (1) list and (2) hashed array
 * build list first and sparse_array_make_array() to build hashed array
 * this stores one value and one pointer
 *
 */
struct sparse_array {
  /* list representation */
  int elm_count;
  /* sorted */
  struct list_elm head;

  /* array representation */
  int array_len;
  struct array_elm *array;
};

static struct sparse_array *
sparse_array_new(void)
{
  struct sparse_array *a = malloc(sizeof(struct sparse_array));
  /**/
  a->elm_count = 0;
  a->head.next = NULL;
  a->head.orig_next = NULL;
  a->head.index = -1;
  a->head.ptr = NULL;
  a->head.value = 0;
  /**/
  a->array_len = 0;
  a->array = NULL;
  return a;
}

static void
insert_elm_after(struct list_elm *elm, int idx, int val, void *ptr)
{
  struct list_elm *new_elm = malloc(sizeof(struct list_elm));
  new_elm->value = val;
  new_elm->index = idx;
  new_elm->ptr = ptr;
  /**/
  new_elm->next = elm->next;
  new_elm->orig_next = elm->next;
  elm->next = new_elm;
}

static void
sparse_array_set(struct sparse_array *sa, int idx, int val, void *ptr)
{
  struct list_elm *e;
  e = &sa->head;
  while (e) {
    if (e->index == idx) {
      /* find same index and update */
      e->value = val;
      e->ptr = ptr;
      return ;
    }
    /* search */
    if (e->index < idx && (!e->next || idx < e->next->index)) {
      insert_elm_after(e, idx, val, ptr);
      /**/
      sa->elm_count ++;
      return ;
    }
    /* go next */
    if (e->orig_next && e->orig_next->index < idx) {
      /* leap ahead */
      e = e->orig_next;
    } else {
      e = e->next;
    }
  }
}

static int
hash(int val, int max, int nth)
{
  val += nth * 113;
  if (val < 0) {
    val = -val;
  }
  if (max == 0) {
    return 0;
  }
  return val % max;
}

static int
sparse_array_try_make_array(struct sparse_array *s)
{
  int i;
  struct list_elm *e;
  /* initialize */
  free(s->array);
  if (!(s->array = malloc(sizeof(struct array_elm) * s->array_len))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return 1;
  }
  for (i = 0; i < s->array_len; i++) {
    s->array[i].index = -1;
  }

  /* push */
  for (e = s->head.next; e; e = e->next) {
    int ok = 0;
    int n = 0;
    do {
      int h = hash(e->index, s->array_len, n);
      assert((h >= 0) && (h < s->array_len));
      if (s->array[h].index == -1) {
	/* find unused element in this array */
	ok = 1;
	s->array[h].index = e->index;
	s->array[h].value = e->value;
	s->array[h].ptr = e->ptr;
      } else {
	/* collision */
	n++;
	if (n > MAX_FAILURE) {
	  /* too much collision */
	  return 1;
	}
      }
    } while (!ok);
  }
  return 0;
}

static void
sparse_array_make_array(struct sparse_array *s)
{
  /* estimate length */
  if (s->elm_count == 1) {
    s->array_len = 1;
  } else {
    s->array_len = s->elm_count;
  }
  while (sparse_array_try_make_array(s)) {
    /* expand a little */
    s->array_len ++;
    s->array_len *= 9;
    s->array_len /= 8;
  }
}

static struct array_elm *
sparse_array_get(struct sparse_array *s, int index, struct array_elm *arg)
{
  if (s->array) {
    int n = 0;
    while (1) {
      int h = hash(index, s->array_len, n);
      if (s->array[h].index == index) {
	*arg = s->array[h];
	return arg;
      }
      n ++;
      if (n == MAX_FAILURE) {
	return NULL;
      }
    }
  } else {
    struct list_elm *e = s->head.next;
    while (e) {
      if (e->index == index) {
	arg->value = e->value;
	arg->ptr = e->ptr;
	return arg;
      }
      /* go next */
      if (e->orig_next && e->orig_next->index < index) {
	/* leap ahead */
	e = e->orig_next;
      } else {
	e = e->next;
      }
    }
    return NULL;
  }
}

/* Orphan function */

/* static int */
/* sparse_array_get_int(struct sparse_array *s, int index) */
/* { */
/*   struct array_elm elm; */
/*   if (sparse_array_get(s, index, &elm)) { */
/*     return elm.value; */
/*   } */
/*   return 0; */
/* } */

static void *
sparse_array_get_ptr(struct sparse_array *s, int index)
{
  struct array_elm elm;
  if (sparse_array_get(s, index, &elm)) {
    return elm.ptr;
  }
  return NULL;
}

static void
sparse_array_free (struct sparse_array **array)
{
  struct list_elm *e;

  assert(array);
  if (!(*array))
    return;
  free ((*array)->array);
  (*array)->array = NULL;
  for (e = (*array)->head.next; e;) {
    struct list_elm *next = e->next;
    struct sparse_array *sub = e->ptr;
    sparse_array_free (&sub);
    e->ptr = NULL;
    e->next = NULL;
    free (e);
    e = next;
  }
  (*array)->head.next = NULL;
  if (!(*array))
    return;
  free (*array);
  *array = NULL;
}

/**/
struct sparse_matrix {
  /**/
  struct sparse_array *row_array;
  /* image information */
  int nr_rows;
  int array_length;
};

/* API */
struct sparse_matrix *
anthy_sparse_matrix_new()
{
  struct sparse_matrix *m = malloc(sizeof(struct sparse_matrix));
  m->row_array = sparse_array_new();
  m->nr_rows = 0;
  return m;
}

static struct sparse_array *
find_row(struct sparse_matrix *m, int row, int create)
{
  struct sparse_array *a;
  a = sparse_array_get_ptr(m->row_array, row);
  if (a) {
    return a;
  }
  if (!create) {
    return NULL;
  }
  /* allocate a new row */
  a = sparse_array_new();
  sparse_array_set(m->row_array, row, 0, a);
  m->nr_rows++;
  return a;
}

/* API */
void
anthy_sparse_matrix_set(struct sparse_matrix *m, int row, int column,
			int value, void *ptr)
{
  struct sparse_array *a;
  a = find_row(m, row, 1);
  sparse_array_set(a, column, value, ptr);
}

/* API */
int
anthy_sparse_matrix_get_int(struct sparse_matrix *m, int row, int column)
{
  struct sparse_array *a;
  struct list_elm *e = NULL;
  a = find_row(m, row, 1);
  if (!a)
    return 0;
  for (e = &a->head; e; e = e->next) {
    if (e->index == column) {
      return e->value;
    }
  }
  return 0;
}

/* API */
void
anthy_sparse_matrix_make_matrix(struct sparse_matrix *m)
{
  struct array_elm *ae;
  int i;
  int offset = 0;
  /**/
  sparse_array_make_array(m->row_array);
  /**/
  for (i = 0; i < m->row_array->array_len; i++) {
    struct sparse_array *row;
    ae = &m->row_array->array[i];
    /**/
    ae->value = offset;
    if (ae->index == -1) {
      continue;
    }
    /**/
    row = ae->ptr;
    sparse_array_make_array(row);
    offset += row->array_len;
  }
  m->array_length = offset;
}

/* API */
void
anthy_sparse_matrix_free (struct sparse_matrix *m)
{
  sparse_array_free (&m->row_array);
  free (m);
}

/* API */
struct matrix_image *
anthy_matrix_image_new(struct sparse_matrix *s)
{
  struct matrix_image *mi;
  int i;
  int offset;
  /**/
  assert(s && s->row_array);
  if (!(mi = malloc(sizeof(struct matrix_image)))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return NULL;
  }
  mi->size = 2 + s->row_array->array_len * 2 + s->array_length * 2;
  if (!(mi->image = malloc(sizeof(int) * mi->size))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      free(mi);
      return NULL;
  }
  mi->image[0] = s->row_array->array_len;
  mi->image[1] = s->array_length;
  /* row index */
  offset = 2;
  for (i = 0; i < s->row_array->array_len; i++) {
    struct array_elm *ae;
    assert(s->row_array->array);
    ae = &s->row_array->array[i];
    mi->image[offset + i*2] = ae->index;
    mi->image[offset + i*2 + 1] = ae->value;
  }
  /* cells */
  offset = 2 + s->row_array->array_len * 2;
  for (i = 0; i < s->row_array->array_len; i++) {
    struct array_elm *ae;
    struct sparse_array *sa;
    int j;
    assert(s->row_array->array);
    ae = &s->row_array->array[i];
    if (ae->index == -1) {
      continue;
    }
    sa = ae->ptr;
    if (!sa) {
      continue;
    }
    for (j = 0; j < sa->array_len; j++) {
      struct array_elm *cell;
      assert(sa->array);
      cell = &sa->array[j];
      assert(cell);
      mi->image[offset] = cell->index;
      if (cell->index == -1) {
	mi->image[offset + 1] = -1;
      } else {
	mi->image[offset + 1] = cell->value;
      }
      offset += 2;
    }
  }
  /**/
  return mi;
}

static int
read_int(int *image, int idx, int en)
{
  if (en) {
    return anthy_dic_ntohl(image[idx]);
  }
  return image[idx];
}

static int
do_matrix_peek(int *image, int row, int col, int en)
{
  int n, h, shift, next_shift;
  int row_array_len = read_int(image, 0, en);
  int column_array_len;
  int cell_offset;

  /* find row */
  if (row_array_len == 0) {
    return 0;
  }
  for (n = 0; ; n++) {
    h = hash(row, row_array_len, n);
    if (read_int(image, 2+ h * 2, en) == row) {
      shift = read_int(image, 2+h*2+1, en);
      break;
    }
    if (read_int(image, 2+ h * 2, en) == -1) {
      return 0;
    }
    if (n > MAX_FAILURE) {
      return 0;
    }
  }

  /* find shift count of next row */
  if (h == row_array_len - 1) {
    /* last one */
    next_shift = read_int(image, 1, en);
  } else {
    /* not last one */
    next_shift = read_int(image, 2+h*2+2+1, en);
  }

  /* crammed width of this row */
  column_array_len = next_shift - shift;

  /* cells in this image */
  cell_offset = 2 + row_array_len * 2;
  for (n = 0; ; n++) {
    h = hash(col, column_array_len, n);
    if (read_int(image, cell_offset + shift * 2+ h * 2, en) == col) {
      return read_int(image, cell_offset + shift * 2 + h*2+1, en);
    }
    if (read_int(image, cell_offset + shift * 2+ h * 2, en) == -1) {
      /* not exist */
      return 0;
    }
    if (n > MAX_FAILURE) {
      return 0;
    }
  }
  return 0;
}

/* API */
int
anthy_matrix_image_peek(int *image, int row, int col)
{
  if (!image) {
    return 0;
  }
  return do_matrix_peek(image, row, col, 1);
}

#ifdef DEBUG
/* for debug purpose */
static void
sparse_array_dump(struct sparse_array *s)
{
  struct list_elm *e;
  int i;
  printf("list(%d):", s->elm_count);
  for (e = s->head.next; e; e = e->next) {
    printf(" %d:%d(%x)", e->index, e->value, (unsigned long)e->ptr);
  }
  printf("\n");
  if (!s->array) {
    return ;
  }
  printf("array(%d):", s->array_len);
  for (i = 0; i < s->array_len; i ++) {
    struct array_elm *ae = &s->array[i];
    if (ae->index != -1) {
      printf(" %d:%d,%d(%x)", i, ae->index, ae->value, (unsigned long)ae->ptr);
    }
  }
  printf("\n");
  return ;
  /**/
}

/* for debug purpose */
void
sparse_matrix_dump(struct sparse_matrix *m)
{
  struct list_elm *e;
  struct array_elm *ae;
  int i, offset;
  if (!m->row_array) {
    for (e = m->row_array->head.next; e; e = e->next) {
      sparse_array_dump(e->ptr);
    }
    return ;
  }
  printf("\nnumber of row=%d, row array size=%d, cell array size=%d\n\n",
	 m->nr_rows, m->row_array->array_len, m->array_length);
  /* row part */
  for (i = 0; i < m->row_array->array_len; i++) {
    struct array_elm *ae;
    ae = &m->row_array->array[i];
    if (ae->index != -1) {
      printf(" [%d] row=%d, shift=%d\n", i, ae->index, ae->value);
    }
  }
  printf("\n");
  offset = 0;
  for (i = 0; i < m->row_array->array_len; i++) {
    struct array_elm *ae;
    struct sparse_array *sa;
    int j;
    ae = &m->row_array->array[i];
    sa = ae->ptr;
    if (!sa) {
      continue;
    }
    for (j = 0; j < sa->array_len; j++) {
      struct array_elm *cell = &sa->array[j];
      if (cell->index != -1) {
	printf("  [%d] column=%d, value=%d\n", offset, cell->index, cell->value);
      }
      offset ++;
    }
  }
  printf("\n");
}
#endif /* DEBUG */
