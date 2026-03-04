/*
 * roma kana converter
 *
 * 理解するためには，構文解析について書かれたテキストで
 * SLR(1)について調べるよろし．
 *
 * $Id: rkconv.c,v 1.16 2002/11/16 03:35:21 yusuke Exp $
 *
 * Copyright (C) 2001-2002 UGAWA Tomoharu
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 *
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anthy/logger.h>

#include "rkconv.h"

#define MAX_CONV_CHARS  1024
#define MAX_MAP_PALETTE 10
#define SPECIAL_CHAR '\xff'
#define TERMINATE_CHAR '\n'

/* break_into_roman */
struct break_roman {
  int	decided_length;
  int   pending_size;
  char*	pending;
};

struct rk_rule_set
{
  struct rk_rule* rules;
  int nr_rules;
};

struct next_array
{
  struct rk_slr_closure* array[128];
};

struct rk_slr_closure
{
  char* prefix;
  struct rk_rule* r;
  int is_reduction_only;
  struct next_array *next;
};

struct rk_map
{
  struct rk_rule_set* rs;
  struct rk_slr_closure* root_cl;
  int refcount;
};

struct rk_conv_context
{
  struct rk_map* map;
  int map_no;
  int old_map_no;
  struct rk_slr_closure* cur_state;
  char cur_str[MAX_CONV_CHARS + 1];
  int cur_str_len;
  struct rk_map* map_palette[MAX_MAP_PALETTE];
  struct break_roman *brk_roman;
};

static void
rk_rule_set_free(struct rk_rule_set* rs)
{
  int i;

  for (i = 0; i < rs->nr_rules; i++) {
    free((void *) rs->rules[i].lhs);
    free((void *) rs->rules[i].rhs);
    free((void *) rs->rules[i].follow);
  }
  free(rs->rules);
  free(rs);
}

static int
rk_rule_copy_to(const struct rk_rule* from, struct rk_rule* to)
{
  char *lhs, *rhs;

  if ((lhs = strdup(from->lhs))) {
    if ((rhs = strdup(from->rhs))) {
      if (!(to->follow = from->follow)
	  || (to->follow = strdup(from->follow))) {
	to->lhs    = lhs;
	to->rhs    = rhs;
	return 0;
      }
      free(rhs);
    }
    free(lhs);
  }
  to->lhs    = NULL;
  to->rhs    = NULL;
  return -1;
}

static struct rk_rule_set*
rk_rule_set_create(const struct rk_rule* rules)
{
  int i;
  struct rk_rule_set* rs;

  rs = (struct rk_rule_set*) malloc(sizeof(struct rk_rule_set));
  if (rs == NULL) {
    return NULL;
  }

  for (i = 0; rules[i].lhs != NULL; i++);
  rs->nr_rules = i;
  rs->rules = (struct rk_rule*) malloc(sizeof(struct rk_rule) * i);
  if (rs->rules == NULL) {
    free(rs);
    return NULL;
  }
  for (i = 0; i < rs->nr_rules; i++) {
    if (rk_rule_copy_to(rules + i, rs->rules + i) != 0) {
      rs->nr_rules = i;
      rk_rule_set_free(rs);
      return NULL;
    }
  }
  return rs;
}


static void
rk_slr_closure_free(struct rk_slr_closure* cl)
{
  int i;
  free(cl->prefix);
  if (cl->next) {
    for (i = 0; i < 128; i++) {
      if (cl->next->array[i]) {
	rk_slr_closure_free(cl->next->array[i]);
      }
    }
    free(cl->next);
  }
  free(cl);
}

static struct next_array *
alloc_next_array(void)
{
  int i;
  struct next_array *na = malloc(sizeof(struct next_array));
  for (i = 0; i < 128; i++) {
    na->array[i] = NULL;
  }
  return na;
}

static struct rk_slr_closure*
rk_slr_closure_create(struct rk_rule_set* rs,
		      const char* prefix, int pflen)
{
  struct rk_slr_closure* cl;
  int i;

  cl = (struct rk_slr_closure*) malloc(sizeof(struct rk_slr_closure));
  if (cl == NULL) {
    return NULL;
  }

  if (prefix != NULL) {
    cl->prefix = (char*) malloc(pflen + 1);
    if (cl->prefix == NULL) {
      free(cl);
      return NULL;
    }
    memcpy(cl->prefix, prefix, pflen);
    cl->prefix[pflen] = '\0';
  } else {
    cl->prefix = strdup("");
    if (cl->prefix == NULL) {
      free(cl);
      return NULL;
    }
  }

  cl->r = NULL;
  cl->is_reduction_only = 1;
  cl->next = NULL;

  for (i = 0; i < rs->nr_rules; i++) {
    struct rk_rule* r;
    int c;
    r = rs->rules + i;
    if (pflen > 0 && strncmp(prefix ? prefix : "", r->lhs, pflen) != 0)
      continue;

    c = r->lhs[pflen] & 0x7f;
    if (c == '\0') { /* reduce */
      cl->r = r;
      if (r->follow != NULL)
	cl->is_reduction_only = 0;
    } else {
      cl->is_reduction_only = 0;
      if (cl->next == NULL) {
	cl->next = alloc_next_array();
      }
      if (cl->next->array[c] == NULL) {
	cl->next->array[c] = rk_slr_closure_create(rs, r->lhs,
						   pflen + 1);
	if (cl->next->array[c] == NULL) {
	  rk_slr_closure_free(cl);
	  return NULL;
	}
      }
    }
  }

  return cl;
}

struct rk_map*
rk_map_create(const struct rk_rule* rules)
{
  struct rk_map* map;

  map = (struct rk_map*) malloc(sizeof(struct rk_map));
  if (map == NULL) {
    return NULL;
  }

  map->rs = rk_rule_set_create(rules);
  if (map->rs == NULL) {
    free(map);
    return NULL;
  }

  map->root_cl = rk_slr_closure_create(map->rs, NULL, 0);
  if (map->root_cl == NULL) {
    rk_rule_set_free(map->rs);
    free(map);
    return NULL;
  }

  map->refcount = 0;

  return map;
}

int
rk_map_free(struct rk_map* map)
{
  if (map->refcount > 0) {
    return -1;
  }
  rk_rule_set_free(map->rs);
  rk_slr_closure_free(map->root_cl);
  free(map);
  return 0;
}


static int
rk_reduce(struct rk_conv_context* cc,
	  struct rk_slr_closure* cur_state, char* buf, int size)
{
  struct rk_rule* r;
  const char* p;
  char* q, * end;

  r = cur_state->r;
  if (r == NULL || size <= 0)
    return 0;

  if (r->rhs[0] == SPECIAL_CHAR) {
    if (r->rhs[1] == 'o')
      rk_select_registered_map(cc, cc->old_map_no);
    else {
      int mapn = r->rhs[1] - '0';
      rk_select_registered_map(cc, mapn);
    }
    return 0;
  }

  p = r->rhs;
  q = buf;
  end = buf + size - 1;
  while (*p && q < end)
    *q ++ = *p++;
  *q = '\0';

  return q - buf;
}

static void
rk_convert_iterative(struct rk_conv_context* cc, int c,
		     char* buf, int size)
{
  struct rk_slr_closure* cur_state = cc->cur_state;

  if (cc->map == NULL)
    return;
  if (size > 0)
    *buf = '\0';
 AGAIN:

  if (cur_state->next && cur_state->next->array[c]) {
    struct rk_slr_closure* next_state = cur_state->next->array[c];

    if (next_state->is_reduction_only) {
      rk_reduce(cc, next_state, buf, size);
      if (cc->map == NULL) {
	cc->cur_state = NULL;
	return;
      }
      cur_state = cc->map->root_cl;
    } else
      cur_state = next_state;
  } else if (cur_state->r != NULL &&
	     (cur_state->r->follow == NULL ||
	      strchr(cur_state->r->follow, c))) {
    int len;

    len = rk_reduce(cc, cur_state, buf, size);
    if (cc->map == NULL) {
      cc->cur_state = NULL;
      return;
    }
    cur_state = cc->map->root_cl;
    buf += len;
    size -= len;
    goto AGAIN;
  } else if (cur_state != cc->map->root_cl) {
    cur_state = cc->map->root_cl;
    goto AGAIN;
  }
  cc->cur_state = cur_state;
}

static void
brk_roman_init(struct rk_conv_context *rkctx)
{
  rkctx->brk_roman = (struct break_roman *)malloc(sizeof(struct break_roman));
  if (!rkctx->brk_roman) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  rkctx->brk_roman->pending = NULL;
  rkctx->brk_roman->pending_size = 0;
}

static void
brk_roman_free(struct rk_conv_context *rkctx)
{
  struct break_roman *br=rkctx->brk_roman;

  if(!br)
    return;

  free(br->pending);
  br->pending = NULL;
  free(br);
  rkctx->brk_roman = NULL;
}


static void
brk_roman_save_pending(struct rk_conv_context *rkctx)
{
  struct break_roman *br=rkctx->brk_roman;
  int len;

  if(!br)
    return;

  len = rk_get_pending_str(rkctx,NULL,0);

  if(br->pending_size < len){
    br->pending_size=len;
    free(br->pending);
    br->pending=(char *)malloc(len);
  }

  rk_get_pending_str(rkctx,br->pending,len);
}


static void
brk_roman_set_decided_len(struct rk_conv_context *rkctx,int len)
{
  struct break_roman *br;
  if (!rkctx) {
    anthy_log(0, "Failed rkctx != NULL in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  br=rkctx->brk_roman;

  if(!br)
    return;

  br->decided_length=len;
}

static void
brk_roman_flush(struct rk_conv_context *rkctx)
{
  struct break_roman *br=rkctx->brk_roman;

  if(!br)
    return;

  if(br->pending)
    br->pending[0]='\0';
  br->decided_length=0;
}

struct rk_conv_context*
rk_context_create(int brk)
{
  struct rk_conv_context* cc;

  cc = (struct rk_conv_context*) malloc(sizeof(struct rk_conv_context));
  if (cc == NULL) {
    return NULL;
  }

  cc->map = NULL;
  memset(&cc->map_palette, 0, sizeof(struct rk_map*) * MAX_MAP_PALETTE);
  cc->map_no = -1;
  cc->old_map_no = -1;
  cc->brk_roman = NULL;
  if (brk) {
    brk_roman_init(cc);
  }
  rk_flush(cc);

  return cc;
}

void
rk_context_free(struct rk_conv_context* cc)
{
  int i;

  brk_roman_free(cc);
  rk_select_map(cc, NULL);
  for (i = 0; i < MAX_MAP_PALETTE; i++) {
    rk_register_map(cc, i, NULL);
  }
  free(cc);
}

int
rk_push_key(struct rk_conv_context* cc, int c)
{
  int increased_length;
  if (!cc) {
    anthy_log(0, "Failed cc != NULL in %s:%d\n", __FILE__, __LINE__);
    return -1;
  }
  c &= 0x7f;
  if (cc->cur_state == NULL)
    return -1;

  brk_roman_save_pending(cc);
  rk_convert_iterative(cc, c,
		       cc->cur_str + cc->cur_str_len,
		       MAX_CONV_CHARS + 1 - cc->cur_str_len);
  increased_length = strlen(cc->cur_str + cc->cur_str_len);
  brk_roman_set_decided_len(cc,increased_length);
  cc->cur_str_len += increased_length;

  return 0;
}

void
rk_terminate(struct rk_conv_context* cc)
{
  rk_push_key(cc, TERMINATE_CHAR);
}

void
rk_flush(struct rk_conv_context* cc)
{
  brk_roman_flush(cc);
  cc->cur_state = (cc->map == NULL) ? NULL : cc->map->root_cl;
  cc->cur_str[0] = '\0';
  cc->cur_str_len = 0;
}

int
rk_partial_result(struct rk_conv_context* cc, char* buf, int size)
{
  int nr_rules = cc->map->rs->nr_rules;
  int i, pending_len;
  char *pending_buf;
  struct rk_rule *rule = cc->map->rs->rules;

  pending_len = rk_get_pending_str(cc, NULL, 0);
  if (pending_len == 0) {
    return 0;
  }
  pending_buf = alloca(pending_len);
  rk_get_pending_str(cc, pending_buf, pending_len);

  for (i = 0; i < nr_rules; i++) {
    if (!strcmp(rule[i].lhs, pending_buf)) {
      const char *res = rule[i].rhs;
      if (size <= 0) {
	return strlen(res) + 1;
      }
      return snprintf(buf, size, "%s", res);
    }
  }
  return 0;
}

int
rk_result(struct rk_conv_context* cc, char* buf, int size)
{
  int copy_len;

  if (size <= 0)
    return cc->cur_str_len;
  copy_len = (size - 1 < cc->cur_str_len) ? size - 1 : cc->cur_str_len;
  memcpy(buf, cc->cur_str, copy_len);
  buf[copy_len] = '\0';
  if (copy_len < cc->cur_str_len)
    memmove(cc->cur_str, cc->cur_str + copy_len,
	    cc->cur_str_len - copy_len + 1);
  cc->cur_str_len -= copy_len;

  return cc->cur_str_len;
}

struct rk_map*
rk_select_map(struct rk_conv_context* cc, struct rk_map* map)
{
  struct rk_map* old_map;

  if (!cc) {
    anthy_log(0, "Failed cc != NULL in %s:%d\n", __FILE__, __LINE__);
    return NULL;
  }
  cc->old_map_no = cc->map_no;
  old_map = cc->map;
  if (old_map) {
    old_map->refcount--;
  }

  cc->map = map;
  if (cc->map == NULL) {
    cc->cur_state = NULL;
  } else {
    map->refcount++;
    cc->cur_state = map->root_cl;
    rk_flush(cc);
  }
  cc->map_no = -1;

  return old_map;
}


int
rk_get_pending_str(struct rk_conv_context* cc, char* buf, int size)
{
  const char* p, *end;
  char *q;

  p = (cc->cur_state == NULL) ? "" : cc->cur_state->prefix;

  if (size <= 0)
    return strlen(p) + 1;
  if (!buf) {
    anthy_log(0, "Failed buf != NULL in %s:%d\n", __FILE__, __LINE__);
    return 0;
  }

  q = buf;
  end = buf + size - 1;
  while (*p && q < end)
    *q++ = *p++;
  *q = '\0';
  return strlen(p);
}

struct rk_map*
rk_register_map(struct rk_conv_context* cc, int mapn, struct rk_map* map)
{
  struct rk_map* old_map;

  if (mapn < 0 || MAX_MAP_PALETTE <= mapn)
    return NULL;

  old_map = cc->map_palette[mapn];
  if (old_map)
    old_map->refcount--;

  cc->map_palette[mapn] = map;
  if (map)
    map->refcount++;

  return old_map;
}

void
rk_select_registered_map(struct rk_conv_context* cc, int mapn)
{
  assert(cc);
  if (0 <= mapn && mapn < 0 + MAX_MAP_PALETTE) {
    rk_select_map(cc, cc->map_palette[mapn]);
    cc->map_no = mapn;
  } else {
    rk_select_map(cc, NULL);
    cc->map_no = -1;
  }
}

int
rk_selected_map(struct rk_conv_context* cc)
{
  return cc->map_no;
}

/* some utitlity functions to merge rk_rule */
static int
rk_rule_length(const struct rk_rule* rules)
{
  int i;
  for (i = 0; rules[i].lhs != NULL; i++);
  return i;
}

static int
rk_my_strcmp(const char *s1, const char *s2)
{
  while (*s1 == *s2) {
    if (!(*s1)) {
      return 0;
    }
    s1++;
    s2++;
  }
  return (*s1) - (*s2);
}

static int
rk_rule_compare_func(const void *p, const void *q)
{
  const struct rk_rule *r1, *r2;
  r1 = p;
  r2 = q;
  return rk_my_strcmp(r1->lhs, r2->lhs);
}

/*
 * ソートされたrk_ruleを作って返す
 */
static struct rk_rule *
rk_sort_rule(const struct rk_rule *src)
{
  struct rk_rule* rules;
  int size = rk_rule_length(src);
  int i, ret;

  rules = (struct rk_rule*) malloc(sizeof(struct rk_rule) * (size + 1));
  if (!rules) {
    return NULL;
  }
  for (i = 0; i < size; i++) {
    ret = rk_rule_copy_to (&src[i], &rules[i]);
    if (ret == -1) {
      goto ERROR;
    }
  }
  qsort(rules, size, sizeof(struct rk_rule),
	rk_rule_compare_func);

  rules[i].lhs = NULL;
  return rules;

 ERROR:
  rules[i].lhs = NULL;
  rk_rules_free(rules);
  return NULL;
}

/* 一つ目のルールが優先される */
static struct rk_rule*
rk_do_merge_rules(const struct rk_rule* r1,
		  const struct rk_rule* r2)
{
  int size;
  int ret;
  struct rk_rule* rules;
  struct rk_rule* p, *q;
  struct rk_rule* r;
  struct rk_rule* tmp;
  int i;

  size = rk_rule_length(r1) + rk_rule_length(r2);
  rules = (struct rk_rule*) malloc(sizeof(struct rk_rule) * (size + 1));
  if (rules == NULL) {
    return NULL;
  }

  r = rules;
  p = (struct rk_rule *)r1;
  q = (struct rk_rule *)r2;
  /* ソート済の列に対してマージソートをする */
  for (i = 0; i < size; i++) {
    if (p->lhs && q->lhs) {
      /* p,qを比較してどちらから取り出すかを選ぶ */
      ret = rk_my_strcmp(p->lhs, q->lhs);
      if (ret > 0) {
	tmp = q;
	q++;
      } else if (ret < 0) {
	tmp = p;
	p++;
      } else {
	/* キーが両方同じなのでqの方を優先する */
	tmp = q;
	p++;q++;
      }
    } else if (p->lhs) {
      tmp = p;
      p++;
    } else if (q->lhs) {
      tmp = q;
      q++;
    } else {
      continue;
    }
    /* ここまでに選択したものをcopyする */
    ret = rk_rule_copy_to(tmp, r);
    if (ret == -1) {
      r->lhs = NULL;
      goto ERROR;
    }
    r++;
  }
  r->lhs = NULL;

  return rules;

 ERROR:
  rk_rules_free (rules);
  return NULL;
}

struct rk_rule*
rk_merge_rules(const struct rk_rule* r1,
	       const struct rk_rule* r2)
{
  struct rk_rule *t1, *t2;
  struct rk_rule* rules;

  t1 = rk_sort_rule(r1);
  if (!t1) {
    return NULL;
  }
  t2 = rk_sort_rule(r2);
  if (!t2) {
    rk_rules_free(t1);
    return NULL;
  }
  rules = rk_do_merge_rules(t1, t2);
  rk_rules_free(t1);
  rk_rules_free(t2);
  return rules;

}

void
rk_rules_free(struct rk_rule* rules)
{
  struct rk_rule* p;

  for (p = rules; p->lhs != NULL; p++) {
    free((void *) p->lhs);
    free((void *) p->rhs);
    free((void *) p->follow);
  }

  free(rules);
}

const char *
brk_roman_get_previous_pending(struct rk_conv_context *rkctx)
{
  struct break_roman *br=rkctx->brk_roman;

  if(!br)
    return NULL;

  return br->pending[0] ? br->pending : NULL;
}

int
brk_roman_get_decided_len(struct rk_conv_context *rkctx)
{
  struct break_roman *br=rkctx->brk_roman;

  if(!br)
    return 0;

  return br->decided_length;
}
