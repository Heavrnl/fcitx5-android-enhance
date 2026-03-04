/*
 * 品詞型を管理する
 * 中身はwtype_tの内部のレイアウトに強く依存する。
 *
 * Copyright (C) 2000-2007 TABATA Yusuke
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
#include <string.h>

#include <anthy/wtype.h>
#include "dic_main.h"

wtype_t anthy_wt_none, anthy_wt_all;

struct wttable {
  const char *name;
  int pos;
  int cos;
  int scos;
  int cc;
  int ct;/*カ変など*/
  int flags;
};

/* 品詞の日本語の名前を品詞に変換するテーブル */
static struct wttable pos_name_tab[]= {
#include "ptab.h"
};

/* 辞書中の品詞の名前を品詞に変換するテーブル */
static struct wttable wt_name_tab[]= {
#include "wtab.h"
};

static struct wttable *
find_wttab(struct wttable *array, const char *name)
{
  struct wttable *w;
  for (w = array; w->name; w++) {
    if (!strcmp(w->name, name)) {
      return w;
    }
  }
  return NULL;
}

void
anthy_init_wtypes(void)
{
  anthy_wt_all.pos = POS_NONE;
  anthy_wt_all.cc = CC_NONE;
  anthy_wt_all.ct = CT_NONE;
  anthy_wt_all.cos = COS_NONE;
  anthy_wt_all.scos = SCOS_NONE;
  anthy_wt_all.wf = WF_NONE;

  anthy_wt_none = anthy_wt_all;
  anthy_wt_none.pos = POS_INVAL;
}

/*
 * 返り値には品詞の名前
 * tには品詞が返される
 */
const char *
anthy_type_to_wtype(const char *s, wtype_t *t)
{
  struct wttable *w;
  if (s[0] != '#') {
    *t = anthy_wt_none;
    return NULL;
  }
  w = find_wttab(wt_name_tab, s);
  if (!w) {
    *t = anthy_wt_all;
    return NULL;
  }
  *t = anthy_get_wtype(w->pos, w->cos, w->scos, w->cc, w->ct, w->flags);
  return w->name;
}

wtype_t
anthy_init_wtype_by_name(const char *name)
{
  struct wttable *p;
  p = find_wttab(pos_name_tab, name);

  if (p) {
    return anthy_get_wtype(p->pos, p->cos, p->scos, p->cc, p->ct, p->flags);
  }

  printf("Failed to find wtype(%s).\n", name);
  return anthy_wt_all;
}

/* 二つの品詞が完全に一致しているかどうか */
int
anthy_wtype_equal(wtype_t lhs, wtype_t rhs)
{
  if (lhs.pos == rhs.pos &&
      lhs.cos == rhs.cos &&
      lhs.scos == rhs.scos &&
      lhs.cc == rhs.cc &&
      lhs.ct == rhs.ct &&
      lhs.wf == rhs.wf) {
    return 1;
  } else {
    return 0;
  }
}


/* n は hs の一部かどうか？ */
int
anthy_wtype_include(wtype_t hs, wtype_t n)
{
  /*printf("POS %d,%d\n", hs.type[WT_POS], n.type[WT_POS]);*/
  if (hs.pos != POS_NONE &&
      hs.pos != n.pos) {
    return 0;
  }
  if (hs.cc != CC_NONE &&
      hs.cc != n.cc) {
    return 0;
  }
  if (hs.ct != CT_NONE &&
      hs.ct != n.ct) {
    return 0;
  }
  if (hs.cos != COS_NONE &&
      hs.cos != n.cos) {
    return 0;
  }
  if (hs.scos != SCOS_NONE &&
      hs.scos != n.scos) {
    return 0;
  }
  return 1;
}

int
anthy_wtype_get_cc(wtype_t t)
{
  return t.cc;
}

int
anthy_wtype_get_ct(wtype_t t)
{
  return t.ct;
}

int
anthy_wtype_get_pos(wtype_t t)
{
  return t.pos;
}

int
anthy_wtype_get_cos(wtype_t t)
{
  return t.cos;
}

int
anthy_wtype_get_scos(wtype_t t)
{
  return t.scos;
}

int
anthy_wtype_get_wf(wtype_t t)
{
  return t.wf;
}

int
anthy_wtype_get_indep(wtype_t t)
{
  return t.wf & WF_INDEP;
}

int
anthy_wtype_get_meisi(wtype_t w)
{
  return w.wf & WF_MEISI;
}

int
anthy_wtype_get_sv(wtype_t w)
{
  return w.wf & WF_SV;
}

int
anthy_wtype_get_ajv(wtype_t w)
{
  return w.wf & WF_AJV;
}

void
anthy_wtype_set_cc(wtype_t *w, int cc)
{
  w->cc = cc;
}

void
anthy_wtype_set_ct(wtype_t *w, int ct)
{
  w->ct = ct;
}

void
anthy_wtype_set_pos(wtype_t *w, int pos)
{
  w->pos = pos;
}

void
anthy_wtype_set_cos(wtype_t *w, int cs)
{
  w->cos = cs;
}

void
anthy_wtype_set_scos(wtype_t *w, int sc)
{
  w->scos = sc;
}

void
anthy_wtype_set_dep(wtype_t *w, int isDep)
{
  if (isDep) {
    w->wf &= (~WF_INDEP);
  }else{
    w->wf |= WF_INDEP;
  }
}

void
anthy_print_wtype(wtype_t w)
{
  printf("(POS=%d,COS=%d,SCOS=%d,CC=%d,CT=%d,flags=%d)\n",
	 anthy_wtype_get_pos(w),
	 anthy_wtype_get_cos(w),
	 anthy_wtype_get_scos(w),
	 anthy_wtype_get_cc(w),
	 anthy_wtype_get_ct(w),
	 anthy_wtype_get_wf(w));
}

wtype_t
anthy_get_wtype_with_ct(wtype_t base, int ct)
{
  wtype_t w;

  w = base;
  w.ct = ct;

  return w;
}

wtype_t
anthy_get_wtype(int pos, int cos, int scos, int cc, int ct, int wf)
{
  wtype_t w;

  w.pos = pos;
  w.cos = cos;
  w.scos = scos;
  w.cc = cc;
  w.ct = ct;
  w.wf = wf;

  return w;
}
