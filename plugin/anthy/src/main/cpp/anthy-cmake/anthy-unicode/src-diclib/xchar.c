/*
 * 文字(xchar)のタイプなどを扱う
 *
 * Copyright (C) 2001-2006 TABATA Yusuke
 */
#include <string.h>
#include "config.h"

#include <anthy/xstr.h>
#include <anthy/xchar.h>

#include "diclib_inner.h"

#define PAGE_SIZE 128
#define NR_PAGES 512
#include "e2u.h"
#include "u2e.h"

/* this use UCS4 */
static struct xchar_ent {
  const xchar xc;
  const int type;
  struct xchar_ent *next;/* hash chain */
} xchar_tab[] =
{
  {0xFF40, XCT_OPEN , 0}, /* ｀, 追加 */
  {0x2032, XCT_CLOSE, 0}, /* ′, 追加 */
  {0x2018, XCT_OPEN , 0}, /* ‘, 追加 */
  {0x2019, XCT_CLOSE, 0}, /* ’, 追加 */
  {0x201C, XCT_OPEN , 0}, /* “, 追加 */
  {0x201D, XCT_CLOSE, 0}, /* ”, 文字コードが間違っていると思われるので修正 */
  {0xff08, XCT_OPEN, 0}, /* （　*/
  {0xff09, XCT_CLOSE, 0}, /* ） */
  {0x3014, XCT_OPEN, 0},  /* 〔 */
  {0x3015, XCT_CLOSE, 0}, /* 〕 */
  {0xff3b, XCT_OPEN, 0}, /* ［ */
  {0xff3d, XCT_CLOSE, 0}, /* ] */
  {0xff5b, XCT_OPEN, 0},  /* { */
  {0xff5d, XCT_CLOSE, 0},  /* ｝　*/
  {0x3008, XCT_OPEN, 0},  /* ＜　*/
  {0x3009, XCT_CLOSE, 0},  /* ＞　*/
  {0x300a, XCT_OPEN, 0},  /* 《　*/
  {0x300b, XCT_CLOSE, 0},  /* 》　*/
  {0x300c, XCT_OPEN, 0},  /* 「　*/
  {0x300d, XCT_CLOSE, 0},  /* 」　*/
  {0x300e, XCT_OPEN, 0},  /* 『　*/
  {0x300f, XCT_CLOSE, 0},  /* 』　*/
  {0x3010, XCT_OPEN, 0},  /* 【　*/
  {0x3011, XCT_CLOSE, 0},  /* 】　*/
  {0x3001, XCT_PUNCTUATION, 0},  /* 、　*/
  {0x3002, XCT_PUNCTUATION, 0},  /* 。　*/
  {0xff0c, XCT_PUNCTUATION, 0},  /* ，　*/
  {0xff0e, XCT_PUNCTUATION, 0},  /* ．　*/
  {0xff1f, XCT_PUNCTUATION, 0},  /* ？　*/
  {0xff01, XCT_PUNCTUATION, 0},  /* ！　*/

  {28, XCT_OPEN, 0}, /* ( */
  {133, XCT_OPEN, 0}, /* [ */
  {29, XCT_CLOSE, 0}, /* ) */
  {135, XCT_CLOSE, 0}, /* ] */
  {HK_TO, XCT_DEP, 0},/* と */
  {HK_HA, XCT_DEP, 0},/* は */
  {HK_NO, XCT_DEP, 0},/* の */
  {HK_NI, XCT_DEP, 0},/* に */
  {HK_GA, XCT_DEP, 0},/* が */
  {HK_WO, XCT_DEP, 0},/* を */
  {WIDE_0, XCT_WIDENUM, 0},
  {WIDE_1, XCT_WIDENUM, 0},
  {WIDE_2, XCT_WIDENUM, 0},
  {WIDE_3, XCT_WIDENUM, 0},
  {WIDE_4, XCT_WIDENUM, 0},
  {WIDE_5, XCT_WIDENUM, 0},
  {WIDE_6, XCT_WIDENUM, 0},
  {WIDE_7, XCT_WIDENUM, 0},
  {WIDE_8, XCT_WIDENUM, 0},
  {WIDE_9, XCT_WIDENUM, 0},
  {HK_DDOT, XCT_PART, 0},
  {HK_XA, XCT_PART, 0},
  {HK_XI, XCT_PART, 0},
  {HK_XU, XCT_PART, 0},
  {HK_XE, XCT_PART, 0},
  {HK_XO, XCT_PART, 0},
  {HK_XYA, XCT_PART, 0},
  {HK_XYU, XCT_PART, 0},
  {HK_XYO, XCT_PART, 0},
  {HK_TT, XCT_PART, 0},
  {0, 0, 0},
};

#define DDOT 0x8ede
#define CIRCLE 0x8edf

static const struct half_kana_table half_kana_tab[] = {
  {HK_A,0x8eb1,0},
  {HK_I,0x8eb2,0},
  {HK_U,0x8eb3,0},
  {HK_E,0x8eb4,0},
  {HK_O,0x8eb5,0},
  {HK_KA,0x8eb6,0},
  {HK_KI,0x8eb7,0},
  {HK_KU,0x8eb8,0},
  {HK_KE,0x8eb9,0},
  {HK_KO,0x8eba,0},
  {HK_SA,0x8ebb,0},
  {HK_SI,0x8ebc,0},
  {HK_SU,0x8ebd,0},
  {HK_SE,0x8ebe,0},
  {HK_SO,0x8ebf,0},
  {HK_TA,0x8ec0,0},
  {HK_TI,0x8ec1,0},
  {HK_TU,0x8ec2,0},
  {HK_TE,0x8ec3,0},
  {HK_TO,0x8ec4,0},
  {HK_NA,0x8ec5,0},
  {HK_NI,0x8ec6,0},
  {HK_NU,0x8ec7,0},
  {HK_NE,0x8ec8,0},
  {HK_NO,0x8ec9,0},
  {HK_HA,0x8eca,0},
  {HK_HI,0x8ecb,0},
  {HK_HU,0x8ecc,0},
  {HK_HE,0x8ecd,0},
  {HK_HO,0x8ece,0},
  {HK_MA,0x8ecf,0},
  {HK_MI,0x8ed0,0},
  {HK_MU,0x8ed1,0},
  {HK_ME,0x8ed2,0},
  {HK_MO,0x8ed3,0},
  {HK_YA,0x8ed4,0},
  {HK_YU,0x8ed5,0},
  {HK_YO,0x8ed6,0},
  {HK_RA,0x8ed7,0},
  {HK_RI,0x8ed8,0},
  {HK_RU,0x8ed9,0},
  {HK_RE,0x8eda,0},
  {HK_RO,0x8edb,0},
  {HK_WA,0x8edc,0},
  {HK_WI,0,0},
  {HK_WE,0,0},
  {HK_WO,0x8ea6,0},
  {HK_N,0x8edd,0},
  {HK_TT,0x8eaf,0},
  {HK_XA,0x8ea7,0},
  {HK_XI,0x8ea8,0},
  {HK_XU,0x8ea9,0},
  {HK_XE,0x8eaa,0},
  {HK_XO,0x8eab,0},
  {HK_GA,0x8eb6,DDOT},
  {HK_GI,0x8eb7,DDOT},
  {HK_GU,0x8eb8,DDOT},
  {HK_GE,0x8eb9,DDOT},
  {HK_GO,0x8eba,DDOT},
  {HK_ZA,0x8ebb,DDOT},
  {HK_ZI,0x8ebc,DDOT},
  {HK_ZU,0x8ebd,DDOT},
  {HK_ZE,0x8ebe,DDOT},
  {HK_ZO,0x8ebf,DDOT},
  {HK_DA,0x8ec0,DDOT},
  {HK_DI,0x8ec1,DDOT},
  {HK_DU,0x8ec2,DDOT},
  {HK_DE,0x8ec3,DDOT},
  {HK_DO,0x8ec4,DDOT},
  {HK_BA,0x8eca,DDOT},
  {HK_BI,0x8ecb,DDOT},
  {HK_BU,0x8ecc,DDOT},
  {HK_BE,0x8ecd,DDOT},
  {HK_BO,0x8ece,DDOT},
  {HK_PA,0x8eca,CIRCLE},
  {HK_PI,0x8ecb,CIRCLE},
  {HK_PU,0x8ecc,CIRCLE},
  {HK_PE,0x8ecd,CIRCLE},
  {HK_PO,0x8ece,CIRCLE},
  {HK_XYA,0x8eac,0},
  {HK_XYU,0x8ead,0},
  {HK_XYO,0x8eae,0},
  {HK_XWA,0,0},
  {HK_DDOT,DDOT,0},
  {HK_BAR,0x8eb0,0},
  {0,0,0}
};

static const struct half_wide_ent {
  const xchar half;
  const xchar wide;
} half_wide_tab[] = {
  {'!', 0xff01},
  {'\"', 0x201d},
  {'#', 0xff03},
  {'$', 0xff04},
  {'%', 0xff05},
  {'&', 0xff06},
  {'\'', 0x2019},
  {'(', 0xff08},
  {')', 0xff09},
  {'*', 0xff0a},
  {'+', 0xff0b},
  {',', 0xff0c},
  {'-', 0xff0d},
  {'.', 0xff0e},
  {'/', 0xff0f},
  {':', 0xff1a},
  {';', 0xff1b},
  {'<', 0xff1c},
  {'=', 0xff1d},
  {'>', 0xff1e},
  {'?', 0xff1f},
  {'@', 0xff20},
  {'[', 0xff3b},
  {'\\', 0xff3c},
  {']', 0xff3d},
  {'^', 0xff3e},
  {'_', 0xff3f},
  {'`', 0xff40},
  {'{', 0xff5b},
  {'|', 0xff5c},
  {'}', 0xff5d},
  {'~', 0xff5e},
  {0, 0}
};

xchar
anthy_lookup_half_wide(xchar xc)
{
  const struct half_wide_ent *hw;
  for (hw = half_wide_tab; hw->half; hw ++) {
    if (hw->half == xc) {
      return hw->wide;
    }
    if (hw->wide == xc) {
      return hw->half;
    }
  }
  return 0;
}

const struct half_kana_table *
anthy_find_half_kana(xchar xc)
{
  const struct half_kana_table *tab;
  for (tab = half_kana_tab; tab->src; tab ++) {
    if (tab->src == xc && tab->dst) {
      return tab;
    }
  }
  return NULL;
}

static int
find_xchar_type(xchar xc)
{
  struct xchar_ent *xe = xchar_tab;

  for (; xe->xc; xe++) {
    if (xe->xc == xc) {
      return xe->type;
    }
  }

  return XCT_NONE;
}

static int
is_hira(xchar xc)
{
  if (xc == HK_DDOT) {
    return 1;
  }
  if (xc == HK_BAR) {
    return 1;
  }
  xc = anthy_ucs_to_euc(xc);
  if ((xc & 0xff00) == 0xa400) {
    return 1;
  }
  return 0;
}

static int
is_kata(xchar xc)
{
  if (xc == HK_BAR) {
    return 1;
  }
  xc = anthy_ucs_to_euc(xc);
  if ((xc & 0xff00) == 0xa500) {
    return 1;
  }
  return 0;
}

static int
is_symbol(xchar xc)
{
  if (xc == UCS_GETA) {
    return 1;
  }
  xc = anthy_ucs_to_euc(xc);
  if (xc == EUC_GETA) {
    return 0;
  }
  if ((xc & 0xff00) == 0xa100) {
    return 1;
  }
  if ((xc & 0xff00) == 0xa200) {
    return 1;
  }
  return 0;
}

static int
is_kanji(xchar xc)
{
  if (xc > 0x4e00 && xc < 0xa000) {
    return 1;
  }
  return 0;
}

static int
search(const int *tab[], int v, int geta)
{
  int page = v / PAGE_SIZE;
  int off = v % PAGE_SIZE;
  const int *t;
  if (page >= NR_PAGES) {
    return geta;
  }
  t = tab[page];
  if (!t) {
    return geta;
  }
  if (!t[off] && v) {
    return geta;
  }
  return t[off];
}

int
anthy_euc_to_ucs(int ec)
{
  return search(e2u_index, ec, UCS_GETA);
}

int
anthy_ucs_to_euc(int uc)
{
  int r = search(u2e_index, uc, EUC_GETA);
  if (r > 65536) {
    return EUC_GETA;
  }
  return r;
}

int
anthy_get_xchar_type(const xchar xc)
{
  int t = find_xchar_type(xc);
  if (xc > 47 && xc < 58) {
    t |= XCT_NUM;
  }
  if (xc < 128) {
    t |= XCT_ASCII;
  }
  if (is_hira(xc)) {
    t |= XCT_HIRA;
  }
  if (is_kata(xc)) {
    t |= XCT_KATA;
  }
  if (is_symbol(xc)) {
    if (!(t & XCT_OPEN) && !(t & XCT_CLOSE)) {
      t |= XCT_SYMBOL;
    }
  }
  if (is_kanji(xc)) {
    t |= XCT_KANJI;
  }
  return t;
}

int
anthy_get_xstr_type(const xstr *xs)
{
  int i, t = XCT_ALL;
  for (i = 0; i < xs->len; i++) {
    t &= anthy_get_xchar_type(xs->str[i]);
  }
  return t;
}

int
anthy_xchar_to_num(xchar xc)
{
  switch (xc) {
  case WIDE_0:return 0;
  case WIDE_1:return 1;
  case WIDE_2:return 2;
  case WIDE_3:return 3;
  case WIDE_4:return 4;
  case WIDE_5:return 5;
  case WIDE_6:return 6;
  case WIDE_7:return 7;
  case WIDE_8:return 8;
  case WIDE_9:return 9;
  }
  if (xc >= '0' && xc <= '9') {
    return xc - (int)'0';
  }
  return -1;
}

xchar
anthy_xchar_wide_num_to_num(xchar c)
{
  switch (c) {
  case WIDE_0:return '0';
  case WIDE_1:return '1';
  case WIDE_2:return '2';
  case WIDE_3:return '3';
  case WIDE_4:return '4';
  case WIDE_5:return '5';
  case WIDE_6:return '6';
  case WIDE_7:return '7';
  case WIDE_8:return '8';
  case WIDE_9:return '9';
  default:return c;
  }
}

void
anthy_init_xchar_tab(void)
{
}
