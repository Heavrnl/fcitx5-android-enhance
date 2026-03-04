/*
 * 構造体アロケータ
 * Funded by IPA未踏ソフトウェア創造事業 2001 8/15
 *
 * $Id: alloc.c,v 1.12 2002/05/15 11:21:10 yusuke Exp $
 *
 * Copyright (C) 2005 YOSHIDA Yuichi
 * Copyright (C) 2000-2005 TABATA Yusuke, UGAWA Tomoharu
 * Copyright (C) 2002, 2005 NIIBE Yutaka
 *
 * dtor: destructor
 *
 * ページ中のフリーなchunkは単方向リストに継がれている
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anthy/alloc.h>
#include <anthy/logger.h>

/**/
#define PAGE_MAGIC 0x12345678
#define PAGE_SIZE 2048

/* ページ使用量の合計、デバッグの時等に用いる */
static int nr_pages;

/* page内のオブジェクトを表すオブジェクト */
struct chunk {
  void *storage[1];
};
#define CHUNK_HEADER_SIZE ((size_t)&((struct chunk *)0)->storage)
/* CPUもしくは、OSの種類によって要求されるアライメント */
#define CHUNK_ALIGN (sizeof(double))

/*
 * pageのstorage中には
 * max_obj = (PAGE_SIZE - PAGE_HEADER_SIZE) / (size + CHUNK_HEADER_SIZE)個の
 * スロットがある。そのうちuse_count個のスロットがfree_listにつながっている、
 * もしくは使用中である。
 */
struct page {
  int magic;
  struct page *prev, *next;
};


#define PAGE_HEADER_SIZE (sizeof(struct page))
#define PAGE_AVAIL(p) ((unsigned char*)p + sizeof(struct page))
#define PAGE_STORAGE(a, p) (((unsigned char *)p) + (a->storage_offset))
#define PAGE_CHUNK(a, p, i) (struct chunk*)(&PAGE_STORAGE(a, p)[((a->size) + CHUNK_HEADER_SIZE) * (i)])


/**/
struct allocator_priv {
  /* 構造体のサイズ */
  int size;
  /* ページ内に入れることができるオブジェクトの数 */
  int max_num;
  /*
     実際のデータが格納され始める場所のオフセット
     ページ中のこれより手前には対応する場所のデータが使われているかどうかを0/1で表す
     ビットマップがある
   */
  int storage_offset;
  /* このallocatorが使用しているページのリスト */
  struct page page_list;
  /* allocatorのリスト */
  struct allocator_priv *next;
  /* sfreeした際に呼ばれる */
  void (*dtor)(void *);
};

static struct allocator_priv *allocator_list;

static int bit_test(unsigned char* bits, int pos)
{
  /*
     bit_getとほぼ同じだがbit != 0の時に0以外を返すことしか保証しない
   */
  return bits[pos >> 3] & (1 << (7 - (pos & 0x7)));
}


static int bit_set(unsigned char* bits, int pos, int bit)
{
  unsigned char filter = 1 << (7 - (pos & 0x7));
  if (bit == 0) {
    return bits[pos >> 3] &= ~filter;
  } else {
    return bits[pos >> 3] |= filter;
  }
}


static struct chunk *
get_chunk_address(void *s)
{
  return (struct chunk *)
    ((unsigned long)s - CHUNK_HEADER_SIZE);
}

static struct page *
alloc_page(struct allocator_priv *ator)
{
  struct page *p;
  unsigned char* avail;

  p = malloc(PAGE_SIZE);
  if (!p) {
    return NULL;
  }

  p->magic = PAGE_MAGIC;
  avail = PAGE_AVAIL(p);
  memset(avail, 0, (ator->max_num >> 3) + 1);
  return p;
}

static struct chunk *
get_chunk_from_page(allocator a, struct page *p)
{
  int i;

  int num = a->max_num;
  unsigned char* avail = PAGE_AVAIL(p);

  for (i = 0; i < num; ++i) {
    if (bit_test(avail, i) == 0) {
      bit_set(avail, i, 1);
      return PAGE_CHUNK(a, p, i);
    }
  }
  return NULL;
}

static int
roundup_align(int num)
{
  num = num + (CHUNK_ALIGN - 1);
  num /= CHUNK_ALIGN;
  num *= CHUNK_ALIGN;
  return num;
}

static int
calc_max_num(int size)
{
  int area, bits;
  /* ビット数で計算
   * 厳密な最適解ではない
   */
  area = (PAGE_SIZE - PAGE_HEADER_SIZE - CHUNK_ALIGN) * 8;
  bits = (size + CHUNK_HEADER_SIZE) * 8 + 1;
  return (int)(area / bits);
}

allocator
anthy_create_allocator(int size, void (*dtor)(void *))
{
  allocator a;
size=roundup_align(size);
  if (size > (int)(PAGE_SIZE - PAGE_HEADER_SIZE - CHUNK_HEADER_SIZE)) {
    anthy_log (0, "Fatal error: too big allocator is requested.\n");
    abort ();
  }
  a = malloc(sizeof(*a));
  if (!a) {
    anthy_log (0, "Fatal error: Failed to allocate memory.\n");
    abort ();
  }
  a->size = size;
  a->max_num = calc_max_num(size);
  a->storage_offset = roundup_align(sizeof(struct page) + a->max_num / 8 + 1);
  /*printf("size=%d max_num=%d offset=%d area=%d\n", size, a->max_num, a->storage_offset, size*a->max_num + a->storage_offset);*/
  a->dtor = dtor;
  a->page_list.next = &a->page_list;
  a->page_list.prev = &a->page_list;
  a->next = allocator_list;
  allocator_list = a;
  return a;
}

static void
anthy_free_allocator_internal(allocator a)
{
  struct page *p, *p_next;

  /* 各ページのメモリを解放する */
  for (p = a->page_list.next; p != &a->page_list; p = p_next) {
    unsigned char* avail = PAGE_AVAIL(p);
    int i;

    p_next = p->next;
    if (a->dtor) {
      for (i = 0; i < a->max_num; i++) {
	if (bit_test(avail, i)) {
	  struct chunk *c;

	  bit_set(avail, i, 0);
	  c = PAGE_CHUNK(a, p, i);
	  a->dtor(c->storage);
	}
      }
    }
    free(p);
    nr_pages--;
  }
  free(a);
}

void
anthy_free_allocator(allocator a)
{
  allocator a0, *a_prev_p;

  /* リストからaの前の要素を見付ける */
  a_prev_p = &allocator_list;
  for (a0 = allocator_list; a0; a0 = a0->next) {
    if (a == a0)
      break;
    else
      a_prev_p = &a0->next;
  }
  /* aをリストから外す */
  *a_prev_p = a->next;

  anthy_free_allocator_internal(a);
}

void *
anthy_smalloc(allocator a)
{
  struct page *p;
  struct chunk *c;

  /* 空いてるページをさがす */
  for (p = a->page_list.next; p != &a->page_list; p = p->next) {
    c = get_chunk_from_page(a, p);
    if (c) {
      return c->storage;
    }
  }
  /* ページを作って、リンクする */
  p = alloc_page(a);
  if (!p) {
    anthy_log(0, "Fatal error: Failed to allocate memory.\n");
    return NULL;
  }
  nr_pages++;

  p->next = a->page_list.next;
  p->prev = &a->page_list;
  a->page_list.next->prev = p;
  a->page_list.next = p;
  /* やり直す */
  return anthy_smalloc(a);
}

void
anthy_sfree(allocator a, void *ptr)
{
  struct chunk *c = get_chunk_address(ptr);
  struct page *p;
  int index;
  /* ポインタの含まれるページを探す */
  for (p = a->page_list.next; p != &a->page_list; p = p->next) {
    if ((unsigned long)p < (unsigned long)c &&
	(unsigned long)c < (unsigned long)p + PAGE_SIZE) {
      break;
    }
  }

  /* sanity check */
  if (!p || p->magic != PAGE_MAGIC) {
    anthy_log(0, "sfree()ing Invalid Object\n");
    abort();
  }

  /* ページ中の何番目のオブジェクトかを求める */
  index = ((unsigned long)c - (unsigned long)PAGE_STORAGE(a, p)) /
    (a->size + CHUNK_HEADER_SIZE);
  bit_set(PAGE_AVAIL(p), index, 0);

  /* デストラクタを呼ぶ */
  if (a->dtor) {
    a->dtor(ptr);
  }
}

void
anthy_quit_allocator(void)
{
  allocator a, a_next;
  for (a = allocator_list; a; a = a_next) {
    a_next = a->next;
    anthy_free_allocator_internal(a);
  }
  allocator_list = NULL;
}
