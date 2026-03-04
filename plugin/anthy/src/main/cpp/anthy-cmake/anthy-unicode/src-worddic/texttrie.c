/*
 * DEPRECATED, it is too hard to debug.
 * you may use textdict instead
 *
 * Trie in Text
 *
 * *issues
 *  +correct API
 *   -iterator vs callback
 *  +robustness
 *   -error detection
 *   -auto correction
 *   -concurrent access
 *  +efficiency
 *   -lower memory consumption
 *   -disk space?
 *
 * on some file system like jffs2 on linux, writable mmap
 * is not allowed, though you can write it.
 *
 */
/*
 * API
 *  anthy_trie_open()
 *  anthy_trie_close()
 *  anthy_trie_add()
 *  anthy_trie_find()
 *  anthy_trie_delete()
 *  anthy_trie_find_next_key()
 *  anthy_trie_find_prefix()
 *  anthy_trie_print_array()
 *
 * Copyright (C) 2005-2006 TABATA Yusuke
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1gmail.com>
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
/* open & mmap */
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
/**/
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <anthy/filemap.h>
#include <anthy/texttrie.h>
#include <anthy/logger.h>
#include "dic_main.h"

/* configs */
#define OBJ_LEN 20
#define LINE_LEN 32
#define EXPAND_COUNT 16

/* cell type */
#define TT_SUPER 0
#define TT_UNUSED 1
#define TT_ALLOCED 2
#define TT_NODE 3
#define TT_BODY 4
#define TT_TAIL 5

/* cell structure */
struct cell {
  /* (common) type */
  int type;
  /* union */
  union {
    /* unused */
    int next_unused;
    /* super */
    struct {
      int first_unused;
      int root_cell;
      int size;
      int serial;
    } super;
    /* node */
    struct {
      int key;
      int next;
      int child;
      int body;
      int parent;
    } node;
    /* body */
    struct {
      int owner;
      char *obj;
    } body;
    /* tail */
    struct {
      char *obj;
      int prev;
    } tail;
  } u;
  /* body & tail */
  int next_tail;
};

struct text_trie {
  /**/
  int fatal;
  /**/
  char *fn;
  FILE *wfp;
  struct filemapping *mapping;
  char *ptr;
  /**/
  struct cell super;
  int valid_super;
};

struct path {
  /**/
  const char *key_str;
  /**/
  int max_len;
  int *path;
  int len;
  int cur;
};

static void
print_super_cell(struct cell *c)
{
  printf("super first_unused=%d, root_cell=%d, size=%d, serial=%d\n",
	 c->u.super.first_unused, c->u.super.root_cell,
	 c->u.super.size, c->u.super.serial);
}

static void
print_alloced_cell(void)
{
  printf("alloc-ed\n");
}

static void
print_node_cell(struct cell *c)
{
  printf("node key=%d", c->u.node.key);
  if (c->u.node.key > 0 && isprint(c->u.node.key)) {
    printf("(%c)", c->u.node.key);
  }
  printf(" parent=%d next=%d child=%d body=%d\n",
	 c->u.node.parent, c->u.node.next, c->u.node.child, c->u.node.body);
}

static void
print_unused_cell(struct cell *c)
{
  printf("unused next_unused=%d\n",
	 c->u.next_unused);
}

static void
print_body_cell(struct cell *c)
{
  printf("body object=(%s), owner=%d, next_tail=%d\n",
	 c->u.body.obj, c->u.body.owner, c->next_tail);
}

static void
print_tail_cell(struct cell *c)
{
  printf("tail object=(%s), prev=%d, next_tail=%d\n",
	 c->u.tail.obj, c->u.tail.prev, c->next_tail);
}

static void
print_cell(int idx, struct cell *c)
{
  if (!c) {
    printf("idx =%d(null cell):\n", idx);
    return ;
  }
  printf("idx=%d:", idx);
  switch (c->type) {
  case TT_SUPER:
    print_super_cell(c);
    break;
  case TT_ALLOCED:
    print_alloced_cell();
    break;
  case TT_NODE:
    print_node_cell(c);
    break;
  case TT_UNUSED:
    print_unused_cell(c);
    break;
  case TT_BODY:
    print_body_cell(c);
    break;
  case TT_TAIL:
    print_tail_cell(c);
    break;
  default:
    printf("unknown\n");
  }
}

static void
path_setup(struct path *path, const char *key, int len, int *buf)
{
  unsigned char *p = (unsigned char *)key;
  path->key_str = key;
  path->max_len = len;
  path->path = buf;
  path->len = 0;
  path->cur = 0;
  /**/
  assert(p);
  while ((*p) && (path->len < path->max_len)) {
    path->path[path->len] = p[0] * 256 + p[1];
    path->len++;
    p++;
    if (p[0]) {
      p++;
    }
  }
}

static void
path_copy_to_str(struct path *path, char *str, int buf_len)
{
  unsigned char *p = (unsigned char *)str;
  int i, o;
  for (i = 0, o = 0; i < path->cur && o < buf_len - 2; i++) {
    p[o] = (path->path[i]>>8)&255;
    p[o+1] = path->path[i]&255;
    o += 2;
  }
  p[o] = 0;
}

static int
sput_int(char *buf, int num)
{
  unsigned char *tmp = (unsigned char *)buf;
  tmp[0] = (num>>24)&255;
  tmp[1] = (num>>16)&255;
  tmp[2] = (num>>8)&255;
  tmp[3] = num&255;
  return 4;
}

static char *
sget_int(char *buf, int *num)
{
  unsigned int res;
  unsigned char *tmp = (unsigned char *)buf;
  res = 0;
  res += tmp[0] << 24;
  res += tmp[1] << 16;
  res += tmp[2] << 8;
  res += tmp[3];
  *num = res;
  buf += 4;
  return buf;
}

static char *
pass_str(char *buf, const char *str)
{
  buf += strlen(str);
  return buf;
}

static void
encode_super(struct cell *c, char *buf)
{
  buf += sprintf(buf, "S ");
  buf += sput_int(buf, c->u.super.size);
  buf += sput_int(buf, c->u.super.root_cell);
  buf += sput_int(buf, c->u.super.first_unused);
  buf += sput_int(buf, c->u.super.serial);
  sput_int(buf, LINE_LEN);
}

static void
encode_node(struct cell *c, char *buf)
{
  buf += sprintf(buf, "N ");
  buf += sput_int(buf, c->u.node.key);
  buf += sput_int(buf, c->u.node.parent);
  buf += sput_int(buf, c->u.node.next);
  buf += sput_int(buf, c->u.node.child);
  sput_int(buf, c->u.node.body);
}

static void
encode_body(struct cell *c, char *buf)
{
  buf += sprintf(buf, "B");
  buf += sput_int(buf, c->next_tail);
  buf += sput_int(buf, c->u.body.owner);
  sprintf(buf, "\"%s\"",
	  c->u.body.obj);
}

static void
encode_unused(struct cell *c, char *buf)
{
  buf += sprintf(buf, "-next=");
  sput_int(buf, c->u.next_unused);
}

static void
encode_tail(struct cell *c, char *buf)
{
  buf += sprintf(buf, "T");
  buf += sput_int(buf, c->u.tail.prev);
  buf += sput_int(buf, c->next_tail);
  sprintf(buf, "\"%s\"",
	  c->u.tail.obj);
}

static void
encode_unknown(char *buf)
{
  sprintf(buf, "?");
}

static void
encode_cell(struct cell *c, char *buf)
{
  switch (c->type) {
  case TT_SUPER:
    encode_super(c, buf);
    break;
  case TT_NODE:
    encode_node(c, buf);
    break;
  case TT_UNUSED:
    encode_unused(c, buf);
    break;
  case TT_BODY:
    encode_body(c, buf);
    break;
  case TT_TAIL:
    encode_tail(c, buf);
    break;
  default:
    encode_unknown(buf);
    break;
  }
}

static void
write_back_cell(struct text_trie *tt, struct cell *c, int idx)
{
  int i;
  char buf[LINE_LEN+1];
  /* sanity check */
  if (((anthy_mmap_size(tt->mapping) / LINE_LEN) < (idx + 1)) ||
      idx < 0) {
    return ;
  }
  for (i = 0; i < LINE_LEN; i++) {
    buf[i] = ' ';
  }
  encode_cell(c, buf);
  buf[LINE_LEN-1] = '\n';
  if (anthy_mmap_is_writable(tt->mapping)) {
    memcpy(&tt->ptr[idx*LINE_LEN], buf, LINE_LEN);
  } else {
    errno = 0;
    if (fseek(tt->wfp, idx*LINE_LEN, SEEK_SET)) {
      anthy_log(0, "Failed fseek in %s:%d: %s\n",
                __FILE__, __LINE__, anthy_strerror(errno));
    }
    fwrite(buf, LINE_LEN, 1, tt->wfp);
    fflush(tt->wfp);
  }
}

static char *
decode_str(char *raw_buf, int off)
{
  char *head;
  char copy_buf[LINE_LEN + 1];
  char *buf;
  int i;
  /* from off to before last '\n' */
  for (i = 0; i < LINE_LEN - off - 1; i++) {
    copy_buf[i] = raw_buf[i];
  }
  copy_buf[i] = 0;
  buf = copy_buf;
  /* find first double quote */
  while (*buf && *buf != '\"') {
    buf ++;
  }
  if (!*buf) {
    /* cant find double quote */
    return strdup("");
  }
  buf ++;
  head = buf;
  /* go to the end of string */
  while (*buf) {
    buf ++;
  }
  /* find last double quote */
  while (*buf != '\"') {
    buf--;
  }
  *buf = 0;
  /**/
  return strdup(head);
}

static void
release_cell_str(struct cell *c)
{
  if (!c) {
    return ;
  }
  if (c->type == TT_BODY) {
    free(c->u.body.obj);
  }
  if (c->type == TT_TAIL) {
    free(c->u.tail.obj);
  }
}

static int
decode_super(struct cell *c, char *buf)
{
  c->type = TT_SUPER;
  buf = pass_str(buf, "S ");
  buf = sget_int(buf, &c->u.super.size);
  buf = sget_int(buf, &c->u.super.root_cell);
  buf = sget_int(buf, &c->u.super.first_unused);
  sget_int(buf, &c->u.super.serial);
  return 0;
}

static int
decode_unuse(struct cell *c, char *buf)
{
  c->type = TT_UNUSED;
  buf = pass_str(buf, "-next=");
  sget_int(buf, &c->u.next_unused);
  return 0;
}

static int
decode_node(struct cell *c, char *buf)
{
  c->type = TT_NODE;
  buf = pass_str(buf, "N ");
  buf = sget_int(buf, &c->u.node.key);
  buf = sget_int(buf, &c->u.node.parent);
  buf = sget_int(buf, &c->u.node.next);
  buf = sget_int(buf, &c->u.node.child);
  sget_int(buf, &c->u.node.body);
  return 0;
}

static int
decode_body(struct cell *c, char *buf)
{
  c->type = TT_BODY;
  buf = pass_str(buf, "B");
  buf = sget_int(buf, &c->next_tail);
  buf = sget_int(buf, &c->u.body.owner);
  c->u.body.obj = decode_str(buf, 9);
  return 0;
}

static int
decode_tail(struct cell *c, char *buf)
{
  c->type = TT_TAIL;
  buf = pass_str(buf, "T");
  buf = sget_int(buf, &c->u.tail.prev);
  buf = sget_int(buf, &c->next_tail);
  c->u.tail.obj = decode_str(buf, 9);
  return 0;
}

static int
decode_alloced(struct cell *c)
{
  c->type = TT_ALLOCED;
  return 0;
}

static struct cell *
decode_nth_cell(struct text_trie *tt, struct cell *c, int nth)
{
  int res;
  char *buf;
  if (nth < 0 ||
      (anthy_mmap_size(tt->mapping) / LINE_LEN) <
      (nth + 1)) {
    return NULL;
  }
  /* GCC 11.0.1 reports dereference of NULL 'buf'
   * in case statement with "-Wanalyzer-null-dereference" option.
   */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-null-dereference"
  buf = &tt->ptr[nth*LINE_LEN];

  res = -1;
  switch (buf[0]) {
  case 'S':
    res = decode_super(c, buf);
    break;
  case '-':
    res = decode_unuse(c, buf);
    break;
  case 'N':
    res = decode_node(c, buf);
    break;
  case 'B':
    res = decode_body(c, buf);
    break;
  case 'T':
    res = decode_tail(c, buf);
    break;
  case '?':
    res =  decode_alloced(c);
    break;
  default:
    /*printf("decode fail (nth=%d::%s).\n", nth, buf);*/
    ;
  }
#pragma GCC diagnostic pop
  if (res) {
    c->type = TT_UNUSED;
  }
  return c;
}

static struct cell *
decode_nth_node(struct text_trie *tt, struct cell *c, int nth)
{
  if (!decode_nth_cell(tt, c, nth)) {
    return NULL;
  }
  if (c->type != TT_NODE) {
    return NULL;
  }
  return c;
}

static int
update_mapping(struct text_trie *tt)
{
  if (tt->mapping) {
    anthy_munmap(tt->mapping);
  }
  tt->mapping = anthy_mmap(tt->fn, 1);
  if (!tt->mapping) {
    /* try to fall back read-only mmap */
    tt->mapping = anthy_mmap(tt->fn, 0);
  }
  if (!tt->mapping) {
    tt->ptr = NULL;
    return 1;
  }
  tt->ptr = anthy_mmap_address(tt->mapping);
  return 0;
}

static int
expand_file(struct text_trie *tt, int count)
{
  char buf[LINE_LEN+1];
  int i;
  for (i = 0; i < LINE_LEN; i++) {
    buf[i] = ' ';
  }
  buf[LINE_LEN-1] = '\n';
  /**/
  for (i = 0; i < count; i++) {
    int res;
    res = fwrite(buf, LINE_LEN, 1, tt->wfp);
    if (res != 1) {
      return 1;
    }
    if (fflush(tt->wfp)) {
      return 1;
    }
  }
  return 0;
}

static int
set_file_size(struct text_trie *tt, int len)
{
  int size = LINE_LEN * len;
  int cur_size;
  int err = 0;

  fseek(tt->wfp, 0, SEEK_END);
  cur_size = ftell(tt->wfp);
  if (cur_size == size) {
    return 0;
  }
  if (cur_size > size) {
    errno = 0;
    if (truncate(tt->fn, size)) {
      anthy_log(0, "Failed truncate in %s:%d: %s\n",
                __FILE__, __LINE__, strerror(errno));
    }
  } else {
    err = expand_file(tt, (size - cur_size) / LINE_LEN);
    if (!err) {
      update_mapping(tt);
    } else {
      tt->fatal = 1;
    }
  }

  return err;
}

static struct cell *
get_super_cell(struct text_trie *tt)
{
  /* cached? */
  if (tt->valid_super) {
    return &tt->super;
  }
  /* read */
  if (decode_nth_cell(tt, &tt->super, 0)) {
    tt->valid_super = 1;
    return &tt->super;
  }
  /* create now */
  tt->super.type = TT_SUPER;
  tt->super.u.super.first_unused = 0;
  tt->super.u.super.root_cell = 0;
  tt->super.u.super.size = 1;
  tt->super.u.super.serial = 1;
  if (set_file_size(tt, 1) != 0) {
    return NULL;
  }
  write_back_cell(tt, &tt->super, 0);
  tt->valid_super = 1;
  return &tt->super;
}

/* convenience function */
static int
get_array_size(struct text_trie *a)
{
  struct cell *super = get_super_cell(a);
  int size = super->u.super.size;
  return size;
}

/* convenience function */
static int
get_root_idx(struct text_trie *tt)
{
  struct cell *super = get_super_cell(tt);
  if (!super) {
    return 0;
  }
  return super->u.super.root_cell;
}

static int
expand_array(struct text_trie *tt, int len)
{
  int i;
  struct cell *super;
  int res;
  int size = get_array_size(tt);
  if (size >= len) {
    return 0;
  }
  /* expand file */
  res = set_file_size(tt, len);
  if (res) {
    return 1;
  }
  /* fill unused */
  super = get_super_cell(tt);
  for (i = super->u.super.size; i < len; i++) {
    struct cell ex_cell;
    ex_cell.type = TT_UNUSED;
    ex_cell.u.next_unused = super->u.super.first_unused;
    write_back_cell(tt, &ex_cell, i);
    super->u.super.first_unused = i;
  }
  super->u.super.size = len;
  write_back_cell(tt, super, 0);
  return 0;
}

void
anthy_trie_print_array(struct text_trie *tt)
{
  int i;
  int size = get_array_size(tt);
  print_cell(0, get_super_cell(tt));
  for (i = 1; i < size; i++) {
    struct cell c;
    decode_nth_cell(tt, &c, i);
    print_cell(i, &c);
    release_cell_str(&c);
  }
}

/* get unused cell */
static int
get_unused_index(struct text_trie *tt)
{
  struct cell *super;
  int unuse;
  struct cell new_cell;

  super = get_super_cell(tt);
  unuse = super->u.super.first_unused;
  if (!unuse) {
    /* expand array */
    expand_array(tt, super->u.super.size + EXPAND_COUNT);
    unuse = super->u.super.first_unused;
    if (!unuse) {
      return 0;
    }
  }
  if (!decode_nth_cell(tt, &new_cell, unuse)) {
    tt->fatal = 1;
    return 0;
  }
  super->u.super.first_unused = new_cell.u.next_unused;
  new_cell.type = TT_ALLOCED;
  write_back_cell(tt, &new_cell, unuse);
  write_back_cell(tt, super, 0);
  return unuse;
}

static void
free_cell(struct text_trie *tt, int idx)
{
  struct cell *super = get_super_cell(tt);
  struct cell c;
  if (!decode_nth_cell(tt, &c, idx)) {
    tt->fatal = 1;
  } else {
    c.type = TT_UNUSED;
    c.u.next_unused = super->u.super.first_unused;
    write_back_cell(tt, &c, idx);
  }
  super->u.super.first_unused = idx;
  write_back_cell(tt, super, 0);
}

static void
load_super(struct text_trie *tt)
{
  struct cell root, *super;
  int root_idx;
  super = get_super_cell(tt);
  if (!super) {
    tt->fatal = 1;
    return ;
  }
  /**/
  if (super->u.super.root_cell) {
    return ;
  }
  /**/
  root_idx = get_unused_index(tt);
  if (root_idx == 0) {
    tt->fatal = 1;
    return ;
  }
  root.u.node.key = 0;
  root.type = TT_NODE;
  root.u.node.parent = 0;
  root.u.node.next = 0;
  root.u.node.child = 0;
  root.u.node.body = 0;
  write_back_cell(tt, &root, root_idx);
  /**/
  tt->super.u.super.root_cell = root_idx;
  write_back_cell(tt, super, 0);
}

static void
purge_cache(struct text_trie *tt)
{
  if (tt) {
    tt->valid_super = 0;
  }
}

static FILE *
do_fopen(const char *fn, int create)
{
  int fd;
  if (!create) {
    /* check file existance */
    FILE *fp;
    fp = fopen(fn, "r");
    if (!fp) {
      return NULL;
    }
    fclose(fp);
  }
  fd = open(fn, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    return NULL;
  }
  return fdopen(fd, "w");
}

static struct text_trie *
alloc_tt(const char *fn, FILE *wfp)
{
  struct text_trie *tt;
  if (!(tt = malloc(sizeof(struct text_trie)))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return NULL;
  }
  tt->fatal = 0;
  tt->wfp = wfp;
  tt->valid_super = 0;
  tt->fn = strdup(fn);
  tt->mapping = NULL;
  return tt;
}

static void
clear_file(const char *fn)
{
  FILE *fp = fopen(fn, "w");
  if (fp) {
    fclose(fp);
  }
}

static struct text_trie *
trie_open(const char *fn, int create, int do_retry)
{
  struct text_trie *tt;
  FILE *fp;
  /**/
  fp = do_fopen(fn, create);
  if (!fp) {
    return NULL;
  }
  /**/
  tt = alloc_tt(fn, fp);
  if (!tt) {
    fclose(fp);
    return NULL;
  }
  /**/
  update_mapping(tt);
  load_super(tt);
  /**/
  if (tt->fatal) {
    anthy_trie_close(tt);
    if (!do_retry) {
      return NULL;
    }
    clear_file(fn);
    return trie_open(fn, create, 0);
  }
  /**/
  return tt;
}


/* API */
struct text_trie *
anthy_trie_open(const char *fn, int create)
{
  struct text_trie *tt;
  anthy_priv_dic_lock();
  tt = trie_open(fn, create, 1);
  anthy_priv_dic_unlock();
  purge_cache(tt);
  return tt;
}

/* API */
void
anthy_trie_close(struct text_trie *tt)
{
  if (!tt) {
    return ;
  }
  fclose(tt->wfp);
  anthy_munmap(tt->mapping);
  free(tt->fn);
  free(tt);
}

/* API */
void
anthy_trie_update_mapping(struct text_trie *tt)
{
  if (!tt) {
    return ;
  }
  anthy_priv_dic_lock();
  update_mapping(tt);
  anthy_priv_dic_unlock();
}

static void
graft_child(struct text_trie *tt, int parent_idx, int new_idx)
{
  struct cell parent_cell;
  struct cell new_cell;
  struct cell cur_youngest_cell;
  int cur_idx;
  /**/
  if (!decode_nth_node(tt, &parent_cell, parent_idx)) {
    return ;
  }
  /**/
  if (parent_cell.u.node.child == 0) {
    /* 1st child */
    parent_cell.u.node.child = new_idx;
    write_back_cell(tt, &parent_cell, parent_idx);
    return ;
  }

  if (!decode_nth_node(tt, &cur_youngest_cell, parent_cell.u.node.child)) {
    return ;
  }
  if (!decode_nth_node(tt, &new_cell, new_idx)) {
    return ;
  }
  if (new_cell.u.node.key < cur_youngest_cell.u.node.key) {
    /* newly added younger child */
    new_cell.u.node.next = parent_cell.u.node.child;
    parent_cell.u.node.child = new_idx;
    write_back_cell(tt, &new_cell, new_idx);
    write_back_cell(tt, &parent_cell, parent_idx);
    return;
  }

  /* insert some order */
  cur_idx = parent_cell.u.node.child;
  while (cur_idx) {
    int next_idx;
    struct cell cur_cell, tmp_cell;
    struct cell *next_cell = NULL;
    if (!decode_nth_node(tt, &cur_cell, cur_idx)) {
      return ;
    }
    next_idx = cur_cell.u.node.next;
    /**/
    if (next_idx) {
      next_cell = decode_nth_node(tt, &tmp_cell, next_idx);
    }
    if (!next_cell) {
      /* append */
      new_cell.u.node.next = 0;
      cur_cell.u.node.next = new_idx;
      write_back_cell(tt, &cur_cell, cur_idx);
      break;
    } else {
      if (cur_cell.u.node.key < new_cell.u.node.key &&
	  new_cell.u.node.key < next_cell->u.node.key) {
	cur_cell.u.node.next = new_idx;
	new_cell.u.node.next = next_idx;
	write_back_cell(tt, &cur_cell, cur_idx);
	break;
      }
    }
    cur_idx = next_idx;
  }
  write_back_cell(tt, &new_cell, new_idx);
}

static int
find_child(struct text_trie *tt, int parent_idx, int key, int exact)
{
  int child_idx;
  int prev_key;
  struct cell parent_cell;

  if (!decode_nth_node(tt, &parent_cell, parent_idx)) {
    return 0;
  }

  /**/
  prev_key = 0;
  child_idx = parent_cell.u.node.child;

  /**/
  while (child_idx) {
    struct cell child_cell;
    int this_key;
    /**/
    if (!decode_nth_node(tt, &child_cell, child_idx)) {
      return 0;
    }
    this_key = child_cell.u.node.key;
    if (this_key <= prev_key) {
      return 0;
    }
    /**/
    if (exact && this_key == key) {
      return child_idx;
    }
    if (!exact && (this_key & 0xff00) == (key & 0xff00)) {
      return child_idx;
    }
    child_idx = child_cell.u.node.next;
    prev_key = this_key;
  }
  return 0;
}

static int
trie_search_rec(struct text_trie *tt, struct path *p,
		int parent_idx, int create)
{
  int child_idx;
  int key;
  assert(p);
  assert(p->path);
  assert((p->cur < p->max_len) && (p->cur >= 0));
  key = p->path[p->cur];
  /* special case */
  if (p->cur == p->len) {
    return parent_idx;
  }
  /* scan child */
  child_idx = find_child(tt, parent_idx, key, 1);
  if (!child_idx) {
    struct cell child_cell;
    if (!create) {
      return 0;
    }
    /* add child */
    child_idx = get_unused_index(tt);
    if (!child_idx) {
      return 0;
    }
    if (!decode_nth_cell(tt, &child_cell, child_idx)) {
      return 0;
    }
    child_cell.type = TT_NODE;
    child_cell.u.node.parent = parent_idx;
    child_cell.u.node.key = key;
    child_cell.u.node.next = 0;
    child_cell.u.node.child = 0;
    child_cell.u.node.body = 0;
    write_back_cell(tt, &child_cell, child_idx);
    /* graft */
    graft_child(tt, parent_idx, child_idx);
  }
  p->cur ++;
  key ++;
  if (!key) {
    return child_idx;
  }
  return trie_search_rec(tt, p, child_idx, create);
}

static char *
get_str_part(const char *str, int from)
{
  char buf[OBJ_LEN+1];
  int i;
  for (i = 0; i < OBJ_LEN; i++) {
    buf[i] = str[from+i];
  }
  buf[i] = 0;
  return strdup(buf);
}

static void
release_body(struct text_trie *tt, int idx)
{
  struct cell c;
  int tail_idx;
  if (!decode_nth_cell(tt, &c, idx) ||
      c.type != TT_BODY) {
    return ;
  }
  tail_idx = c.next_tail;
  while (tail_idx) {
    struct cell tail_cell;
    int tmp;
    if (!decode_nth_cell(tt, &tail_cell, tail_idx)) {
      break;
    }
    tmp = tail_cell.next_tail;
    free_cell(tt, tail_idx);
    tail_idx = tmp;
  }
  free_cell(tt, idx);
}

static void
set_body(struct text_trie *tt, int idx, const char *body_str)
{
  int body_idx = get_unused_index(tt);
  int len;
  int i;
  struct cell node_cell;
  struct cell body_cell;
  struct cell prev_cell;
  struct cell tail_cell;
  int prev_idx;
  /**/
  if (!decode_nth_cell(tt, &node_cell, idx)) {
    return ;
  }
  if (node_cell.u.node.body) {
    release_body(tt, node_cell.u.node.body);
  }
  len = strlen(body_str);
  /**/
  node_cell.u.node.body = body_idx;
  write_back_cell(tt, &node_cell, idx);
  /**/
  if (!decode_nth_cell(tt, &body_cell, body_idx)) {
    return ;
  }
  body_cell.type = TT_BODY;
  body_cell.u.body.obj = get_str_part(body_str, 0);
  body_cell.u.body.owner = idx;
  body_cell.next_tail = 0;
  write_back_cell(tt, &body_cell, body_idx);
  release_cell_str(&body_cell);
  /**/
  if (!decode_nth_cell(tt, &body_cell, body_idx)) {
    return ;
  }
  /**/
  prev_idx = body_idx;
  prev_cell = body_cell;
  for (i = OBJ_LEN; i < len; i += OBJ_LEN) {
    int tail_idx = get_unused_index(tt);
    if (!decode_nth_cell(tt, &tail_cell, tail_idx)) {
      return ;
    }
    tail_cell.type = TT_TAIL;
    tail_cell.u.tail.obj = get_str_part(body_str, i);
    tail_cell.u.tail.prev = prev_idx;
    tail_cell.next_tail = 0;
    prev_cell.next_tail = tail_idx;
    write_back_cell(tt, &tail_cell, tail_idx);
    write_back_cell(tt, &prev_cell, prev_idx);
    release_cell_str(&prev_cell);
    /**/
    prev_idx = tail_idx;
    prev_cell = tail_cell;
  }
  if (i != OBJ_LEN) {
    release_cell_str(&tail_cell);
  }
}

static int
trie_add(struct text_trie *tt, struct path *p, const char *body)
{
  int root_idx = get_root_idx(tt);
  int target_idx;
  /**/
  if (root_idx == 0) {
    return -1;
  }
  target_idx = trie_search_rec(tt, p, root_idx, 1);
  if (target_idx) {
    set_body(tt, target_idx, body);
  }
  return 0;
}

/* API */
int
anthy_trie_add(struct text_trie *tt, const char *key, const char *body)
{
  int res;
  int len;
  struct path p;
  if (!tt || tt->fatal) {
    return -1;
  }
  len = strlen(key);
  path_setup(&p, key, len, alloca(sizeof(int)*len));
  anthy_priv_dic_lock();
  res = trie_add(tt, &p, body);
  anthy_priv_dic_unlock();
  purge_cache(tt);
  return res;
}

static int
get_object_length(struct text_trie *tt, int body_idx)
{
  int len = 0;
  int idx = body_idx;
  while (idx) {
    struct cell c;
    if (!decode_nth_cell(tt, &c, idx)) {
      return 0;
    }
    idx = c.next_tail;
    len += OBJ_LEN;
    release_cell_str(&c);
  }
  return len;
}

static char *
gather_str(struct text_trie *tt, int body_idx)
{
  int idx;
  char *buf;
  int len;
  /* count length */
  len = get_object_length(tt, body_idx);
  if (len == 0) {
    return NULL;
  }
  /**/
  buf = malloc(len + 1);
  idx = body_idx;
  len = 0;
  while (idx) {
    struct cell c;
    if (!decode_nth_cell(tt, &c, idx)) {
      free(buf);
      return NULL;
    }
    if (len == 0) {
      sprintf(&buf[len], "%s", c.u.body.obj);
    } else {
      sprintf(&buf[len], "%s", c.u.tail.obj);
    }
    idx = c.next_tail;
    len += OBJ_LEN;
    release_cell_str(&c);
  }
  return buf;
}

static char *
trie_find(struct text_trie *tt, struct path *p)
{
  int root_idx;
  int target_idx;
  root_idx = get_root_idx(tt);
  if (!root_idx) {
    return NULL;
  }
  target_idx = trie_search_rec(tt, p, root_idx, 0);
  if (target_idx) {
    struct cell target_cell;
    int body_idx;
    if (!decode_nth_node(tt, &target_cell, target_idx)) {
      return NULL;
    }
    body_idx = target_cell.u.node.body;
    if (body_idx) {
      return gather_str(tt, body_idx);
    }
  }
  return NULL;
}

/* API */
char *
anthy_trie_find(struct text_trie *tt, char *key)
{
  char *res;
  struct path p;
  int len;
  if (!tt || tt->fatal) {
    return NULL;
  }
  len = strlen(key);
  path_setup(&p, key, len, alloca(sizeof(int)*len));
  anthy_priv_dic_lock();
  res = trie_find(tt, &p);
  anthy_priv_dic_unlock();
  purge_cache(tt);
  return res;
}

static int
do_find_next_key(struct text_trie *tt, struct path *p,
		 int root_idx, int target_idx)
{
  struct cell *target_cell, tmp_cell;
  int prev_is_up = 0;
  target_cell = decode_nth_node(tt, &tmp_cell, target_idx);
  /**/
  do {
    /* one step */
    if (!target_cell) {
      return -1;
    }
    if (!prev_is_up && target_cell->u.node.child) {
      prev_is_up = 0;
      target_idx = target_cell->u.node.child;
      p->cur++;
    } else if (target_cell->u.node.next) {
      prev_is_up = 0;
      target_idx = target_cell->u.node.next;
    } else if (target_cell->u.node.parent) {
      prev_is_up = 1;
      target_idx = target_cell->u.node.parent;
      p->cur--;
    } else {
      return -1;
    }
    target_cell = decode_nth_node(tt, &tmp_cell, target_idx);
    if (!target_cell) {
      return -1;
    }
    if (p->cur >= p->max_len) {
      continue;
    }
    if (p->cur == 0) {
      return -1;
    }
    p->path[p->cur-1] = target_cell->u.node.key;
    if (!prev_is_up && target_cell->u.node.body) {
      return 0;
    }
  } while (target_idx != root_idx);
  return -1;
}

static int
find_partial_key(struct text_trie *tt, struct path *p, int idx)
{
  struct cell c;
  if (!decode_nth_node(tt, &c, idx)) {
    return -1;
  }
  if (c.type != TT_NODE) {
    return -1;
  }
  p->len ++;
  p->path[p->cur] = c.u.node.key;
  p->cur ++;
  return 0;
}

static int
trie_find_next_key(struct text_trie *tt, struct path *p)
{
  int root_idx = get_root_idx(tt);
  int target_idx;
  int tmp_idx;
  /**/
  target_idx = trie_search_rec(tt, p, root_idx, 0);
  if (target_idx > 0) {
    /* easy case */
    return do_find_next_key(tt, p, root_idx, target_idx);
  }
  if ((p->path[p->len-1] & 0xff) != 0) {
    /* simply not exist in tree */
    return -1;
  }
  /* special case */
  p->len --;
  p->cur = 0;
  target_idx = trie_search_rec(tt, p, root_idx, 0);
  tmp_idx = find_child(tt, target_idx, p->path[p->len], 0);
  if (tmp_idx) {
    return find_partial_key(tt, p, tmp_idx);
  }
  return do_find_next_key(tt, p, root_idx, target_idx);
}


/* API */
char *
anthy_trie_find_next_key(struct text_trie *tt, char *buf, int buf_len)
{
  int res;
  struct path p;
  if (!tt || tt->fatal) {
    return NULL;
  }
  path_setup(&p, buf, buf_len, alloca(sizeof(int)*buf_len));
  anthy_priv_dic_lock();
  res = trie_find_next_key(tt, &p);
  anthy_priv_dic_unlock();
  purge_cache(tt);
  if (res) {
    return NULL;
  }
  path_copy_to_str(&p, buf, buf_len);
  return buf;
}

static void
trie_find_prefix(struct text_trie *tt, const char *str,
		       char *buf, int buf_len,
		       int (*cb)(const char *key, const char *str))
{
  int idx = get_root_idx(tt);
  int i, len = strlen(str);
  for (i = 0; i < len && i < buf_len; i++) {
    struct cell c;
    idx = find_child(tt, idx, str[i], 1);
    if (!idx) {
      return ;
    }
    if (!decode_nth_node(tt, &c, idx)) {
      return ;
    }
    buf[i] = idx;
    buf[i+1] = 0;
    if (c.u.node.body) {
      char *s = gather_str(tt, c.u.node.body);
      if (cb) {
	cb(buf, s);
      }
      free(s);
    }
  }
}

void
anthy_trie_find_prefix(struct text_trie *tt, const char *str,
		       char *buf, int buf_len,
		       int (*cb)(const char *key, const char *str))
{
  if (!tt || tt->fatal) {
    return ;
  }
  anthy_priv_dic_lock();
  trie_find_prefix(tt, str, buf, buf_len, cb);
  anthy_priv_dic_unlock();
  purge_cache(tt);
}

static void
disconnect(struct text_trie *tt, int parent_idx, int target_idx)
{
  struct cell parent_cell;
  struct cell target_cell;

  if (!decode_nth_node(tt, &parent_cell, parent_idx) ||
      !decode_nth_node(tt, &target_cell, target_idx)) {
    return ;
  }

  if (parent_cell.u.node.child == target_idx) {
    /* 1st child */
    parent_cell.u.node.child = target_cell.u.node.next;
    write_back_cell(tt, &parent_cell, parent_idx);
    if (!target_cell.u.node.next &&
	!parent_cell.u.node.body) {
      /* only child and parent does not have body, so traverse upward */
      disconnect(tt, parent_cell.u.node.parent, parent_idx);
      free_cell(tt, target_idx);
      return ;
    }
    free_cell(tt, target_idx);
  } else {
    /* not 1st child */
    int child_idx = parent_cell.u.node.child;
    while (child_idx) {
      struct cell cur = { 0, };
      if (!decode_nth_cell(tt, &cur, child_idx)) {
	return ;
      }
      if (cur.u.node.next == target_idx) {
	/**/
	cur.u.node.next = target_cell.u.node.next;
	write_back_cell(tt, &cur, child_idx);
	free_cell(tt, target_idx);
	return ;
      }
      child_idx = cur.u.node.next;
    }
  }
}

static void
trie_delete(struct text_trie *tt, struct path *p)
{
  struct cell target_cell;
  int root_idx = get_root_idx(tt);
  int target_idx, parent_idx;
  target_idx = trie_search_rec(tt, p, root_idx, 0);
  if (!target_idx) {
    return ;
  }
  if (!decode_nth_node(tt, &target_cell, target_idx)) {
    return ;
  }
  release_body(tt, target_cell.u.node.body);
  target_cell.u.node.body = 0;
  write_back_cell(tt, &target_cell, target_idx);
  if (target_cell.u.node.child) {
    return ;
  }
  parent_idx = target_cell.u.node.parent;
  disconnect(tt, parent_idx, target_idx);
}

/* API */
void
anthy_trie_delete(struct text_trie *tt, const char *key)
{
  struct path p;
  int len;
  if (!tt || tt->fatal) {
    return ;
  }
  len = strlen(key);
  path_setup(&p, key, len, alloca(sizeof(int)*len));
  anthy_priv_dic_lock();
  trie_delete(tt, &p);
  anthy_priv_dic_unlock();
  purge_cache(tt);
}
