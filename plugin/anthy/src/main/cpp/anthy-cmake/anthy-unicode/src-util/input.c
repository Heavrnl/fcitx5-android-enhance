/*
 * Anthyのキーの受け付けやプリエディットの制御を行うレイヤー
 *
 * 今からアプリケーションを書く場合にはuimの利用をお薦めします。
 *
 * Funded by IPA未踏ソフトウェア創造事業 2002 1/23
 * Copyright (C) 2001-2002 UGAWA Tomoharu
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmai.com>
 *
 * $Id: input.c,v 1.25 2002/11/16 03:35:21 yusuke Exp $
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <anthy/anthy.h>
#include <anthy/input.h>
#include <anthy/logger.h>

#include "rkconv.h"
#include "rkhelper.h"

struct anthy_input_context {
  /* ANTHY_INPUT_ST_* */
  int state;

  /* always allocated */
  struct rk_conv_context* rkctx;
  int map_no;  /* RKMAP_* */
  /* 変換する文字列のバッファ*/
  char* hbuf;
  int n_hbuf;
  int s_hbuf;
  char* hbuf_follow;
  int n_hbuf_follow;
  int s_hbuf_follow;

  /* allocated only in conv state */
  anthy_context_t actx;
  struct a_segment* segment;
  struct a_segment* cur_segment;
  int enum_cand_count;
  int enum_cand_limit;
  int enum_reverse;
  int last_gotten_cand;

  /* always allocated by the library */
  /* コミットバッファ */
  char* commit;
  int n_commit;
  int s_commit;

  /* always allocated by the library */
  /* カットバッファ */
  char* cut;
  int n_cut;
  int s_cut;

  struct anthy_input_config* cfg;
  struct anthy_input_context* next_cfg_owner;
};

int anthy_input_errno;

#define DEFAULT_ENUM_CAND_LIMIT 3

#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define is_eucchar(s)  (((s)[0] & 0x80) && ((s)[1] & 0x80))

static int utf8_char_len(char *str) {
  int len = 0;
  if ((*str & 0x80) == 0x00) {
    len = 1;
  } else if ((*str & 0xe0) == 0xc0) {
    len = 2;
  } else if ((*str & 0xf0) == 0xe0) {
    len = 3;
  } else if ((*str & 0xf8) == 0xf0) {
    len = 4;
  }
  return len;
}

struct anthy_input_config {
  struct rk_option* rk_option;
  /* 6はsrc-util/rkhelper.h の NR_RKMAPに相当 */
  struct rk_map*   rk_map[6];
  struct anthy_input_context* owners;
  /**/
  int break_into_roman;
  int preedit_mode;
};

struct a_segment {
  int index;
  int pos;
  struct anthy_segment_stat ass;
  int cand;
  struct a_segment* next, * prev;
};

static int
ensure_buffer(char** buf, int* size, int to_size)
{
  if (*size < to_size) {
    *buf = (char*) realloc(*buf, to_size);
    if (*buf == NULL) {
      anthy_input_errno = AIE_NOMEM;
      return -1;
    }
    *size = to_size;
  }
  return 0;
}

static void
leave_edit_state(struct anthy_input_context* ictx)
{
  /* do noting */
  (void) ictx;
}


static void
enter_none_state(struct anthy_input_context* ictx)
{
  ictx->state = ANTHY_INPUT_ST_NONE;
}

static void
enter_edit_state(struct anthy_input_context* ictx)
{
  ictx->state = ANTHY_INPUT_ST_EDIT;
  rk_flush(ictx->rkctx);
  rk_select_registered_map(ictx->rkctx, ictx->map_no);
  ictx->n_hbuf = 0;
  ictx->n_hbuf_follow = 0;
}

static void
enter_edit_state_noinit(struct anthy_input_context* ictx)
{
  ictx->state = ANTHY_INPUT_ST_EDIT;
}

static void
leave_conv_state(struct anthy_input_context* ictx)
{
  struct a_segment* as, * next;
  anthy_release_context(ictx->actx);
  for (as = ictx->segment; as; as = next) {
    next = as->next;
    free(as);
  }
  anthy_reset_context(ictx->actx);
}

static void
reset_anthy_input_context(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    leave_edit_state(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    leave_conv_state(ictx);
    break;
  }
  enter_none_state(ictx);
}

static void
read_rk_result(struct anthy_input_context* ictx)
{
  int ret;

  ret = rk_result(ictx->rkctx, ictx->hbuf + ictx->n_hbuf,
		  ictx->s_hbuf - ictx->n_hbuf);
  if (ret > 0) {
    if (ictx->s_hbuf - ictx->n_hbuf > 0)
      ictx->n_hbuf = ictx->s_hbuf - 1;

    ensure_buffer(&ictx->hbuf, &ictx->s_hbuf, ictx->n_hbuf + ret + 1);

    rk_result(ictx->rkctx, ictx->hbuf + ictx->n_hbuf,
	      ictx->s_hbuf - ictx->n_hbuf);
  }
  if (ictx->hbuf)
    ictx->n_hbuf += strlen(ictx->hbuf + ictx->n_hbuf);
}

static void
terminate_rk(struct anthy_input_context* ictx)
{
  rk_terminate(ictx->rkctx);
  read_rk_result(ictx);
  rk_flush(ictx->rkctx);
}

static void
join_noconv_string(struct anthy_input_context* ictx)
{
  if (ictx->n_hbuf_follow > 0) {
    ensure_buffer(&ictx->hbuf, &ictx->s_hbuf,
		  ictx->n_hbuf + ictx->n_hbuf_follow);
    memcpy(ictx->hbuf + ictx->n_hbuf, ictx->hbuf_follow, ictx->n_hbuf_follow);
    ictx->n_hbuf += ictx->n_hbuf_follow;
    ictx->n_hbuf_follow = 0;
  }
}

static void
enter_conv_state(struct anthy_input_context* ictx)
{
  int ret;
  struct anthy_conv_stat acs;
  struct a_segment* as_tail, ** as_tailp;
  int i;
  int last_pos;

  ictx->state = ANTHY_INPUT_ST_CONV;

  terminate_rk(ictx);

  join_noconv_string(ictx);

  if (ictx->n_hbuf == 0) {
    ensure_buffer(&ictx->commit, &ictx->s_commit, ictx->n_commit + 1);
    ictx->commit[ictx->n_commit++] = ' ';
    enter_none_state(ictx);
    return;
  }

  ensure_buffer(&ictx->hbuf, &ictx->s_hbuf, ictx->n_hbuf + 1);
  ictx->hbuf[ictx->n_hbuf] = '\0';

  ictx->enum_cand_count = 0;
  ictx->actx = anthy_create_context();
  anthy_context_set_encoding(ictx->actx, ANTHY_UTF8_ENCODING);
  if (!ictx->actx) {
    enter_none_state(ictx);
    anthy_input_errno = AIE_NOMEM;
    return;
  }
  anthy_reset_context(ictx->actx);
  ret = anthy_set_string(ictx->actx, ictx->hbuf);
  if (ret < 0) {
    anthy_release_context(ictx->actx);
    enter_none_state(ictx);
    return;
  }

  anthy_get_stat(ictx->actx, &acs);
  as_tail = NULL;
  as_tailp = &ictx->segment;
  last_pos = 0;
  for (i = 0; i < acs.nr_segment; i++) {
    struct a_segment* as;
    as = (struct a_segment*) malloc(sizeof(struct a_segment));
    as->index = i;
    as->pos = last_pos;
    anthy_get_segment_stat(ictx->actx, i, &as->ass);
    last_pos += as->ass.seg_len;
    as->cand = 0;
    as->prev = as_tail;
    *as_tailp = as;
    as->next = NULL;
    as_tailp = &as->next;
    as_tail = as;
  }
  ictx->cur_segment = ictx->segment;
  ictx->last_gotten_cand = 0;
}

static void
enter_conv_state_noinit(struct anthy_input_context* ictx)
{
  ictx->state = ANTHY_INPUT_ST_CONV;
}

static void
enter_cseg_state(struct anthy_input_context* ictx)
{
  ictx->state = ANTHY_INPUT_ST_CSEG;
  ictx->enum_cand_count = 0;
}

static void
leave_cseg_state(struct anthy_input_context* ictx)
{
  /* do nothing */
  (void)ictx;
}

static int
cmdh_map_select(struct anthy_input_context* ictx, int map)
{
  switch (map) {
  case ANTHY_INPUT_MAP_ALPHABET:
    ictx->map_no = RKMAP_ASCII;
    break;
  case ANTHY_INPUT_MAP_WALPHABET:
    ictx->map_no = RKMAP_WASCII;
    break;
  case ANTHY_INPUT_MAP_HIRAGANA:
    ictx->map_no = RKMAP_HIRAGANA;
    break;
  case ANTHY_INPUT_MAP_KATAKANA:
    ictx->map_no = RKMAP_KATAKANA;
    break;
  case ANTHY_INPUT_MAP_HANKAKU_KANA:
    ictx->map_no = RKMAP_HANKAKU_KANA;
    break;
  default:
    anthy_input_errno = AIE_INVAL;
    return -1;
  }

  rk_select_registered_map(ictx->rkctx, ictx->map_no);

  return 0;
}

static struct anthy_input_segment*
cmdh_get_candidate(struct anthy_input_context* ictx, int cand_no)
{
  struct a_segment* cs;
  struct anthy_input_segment* seg;
  int len;

  cs = ictx->cur_segment;
  if (cand_no >= cs->ass.nr_candidate) {
    anthy_input_errno = AIE_INVAL;
    return NULL;
  }
  ictx->last_gotten_cand = cand_no;

  seg = (struct anthy_input_segment*)
    malloc(sizeof(struct anthy_input_segment));
  if (!seg) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return NULL;
  }
  len = anthy_get_segment(ictx->actx, cs->index, cand_no, NULL, 0);
  seg->str = (char*) malloc(len + 1);
  anthy_get_segment(ictx->actx, cs->index, cand_no, seg->str, len + 1);
  seg->cand_no = cand_no;
  seg->noconv_len = anthy_get_segment(ictx->actx, cs->index,
				      NTH_UNCONVERTED_CANDIDATE, NULL, 0);
  seg->nr_cand = cs->ass.nr_candidate;
  seg->flag = ANTHY_INPUT_SF_CURSOR;
  if (ictx->enum_cand_count >= ictx->enum_cand_limit)
	  seg->flag |= (ictx->enum_reverse ?
			ANTHY_INPUT_SF_ENUM_REVERSE : ANTHY_INPUT_SF_ENUM);

  return seg;
}

static void
do_cmd_commit(struct anthy_input_context* ictx)
{
  struct a_segment* as;

  for (as = ictx->segment; as; as = as->next) {
    int len;

    len = anthy_get_segment(ictx->actx, as->index, as->cand, NULL, 0);
    ensure_buffer(&ictx->commit, &ictx->s_commit, ictx->n_commit + len + 1);
    anthy_get_segment(ictx->actx, as->index, as->cand,
		      ictx->commit + ictx->n_commit, len + 1);
    ictx->n_commit += len;
    anthy_commit_segment(ictx->actx, as->index, as->cand);
  }
}

static int
cmdh_select_candidate(struct anthy_input_context* ictx,
		      int cand_no)
{
  struct a_segment* cs;

  cs = ictx->cur_segment;
  if (cand_no >= cs->ass.nr_candidate) {
    anthy_input_errno = AIE_INVAL;
    return -1;
  }
  cs->cand = cand_no;

  if (cs->next) {
    ictx->cur_segment = cs->next;
    ictx->last_gotten_cand = ictx->cur_segment->cand;
    ictx->enum_cand_count = 0;
  } else {
    ictx->last_gotten_cand = ictx->cur_segment->cand;
    ictx->enum_cand_count = 0;
  }

  return 0;
}

static void
do_cmd_push_key(struct anthy_input_context* ictx, const char* str)
{
  const char* p;

  if (!str) {
    anthy_log(0, "str should not be null in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  for (p = str; *p; p++) {
    if (isspace((int)(unsigned char) *p) && *p != ' ')
      continue;

    rk_push_key(ictx->rkctx, *p);
    read_rk_result(ictx);
  }
}

static void
cmd_push_key(struct anthy_input_context* ictx, const char* str)
{
  do_cmd_push_key(ictx, str);
}

static void
cmd_move_cursor(struct anthy_input_context* ictx, int d)
{
  if (rk_get_pending_str(ictx->rkctx, NULL, 0) > 1) {
    rk_flush(ictx->rkctx);
    return;
  }

  if (d > 0) {
    char* p;
    int len;
    if (ictx->n_hbuf_follow == 0)
      return;
    for (p = ictx->hbuf_follow;
	 p < ictx->hbuf_follow + ictx->n_hbuf_follow && d > 0; p++, d--) {
      if (p < ictx->hbuf_follow + ictx->n_hbuf_follow - 3 && utf8_char_len(p) == 4) {
	p += 3;
      } else if (p < ictx->hbuf_follow + ictx->n_hbuf_follow - 2 && utf8_char_len(p) == 3) {
	p += 2;
      } else if (p < ictx->hbuf_follow + ictx->n_hbuf_follow - 1 && utf8_char_len(p) == 2) {
	p++;
      }
    }
    len = p - ictx->hbuf_follow;
    ensure_buffer(&ictx->hbuf, &ictx->s_hbuf, ictx->n_hbuf + len);
    memcpy(ictx->hbuf + ictx->n_hbuf, ictx->hbuf_follow, len);
    ictx->n_hbuf += len;
    ictx->n_hbuf_follow -= len;
    memmove(ictx->hbuf_follow, p, ictx->n_hbuf_follow);
  } else {
    char* p;
    int len;
    if (ictx->n_hbuf == 0)
      return;
    for (p = ictx->hbuf + ictx->n_hbuf;
	 p > ictx->hbuf && d < 0; p--, d++) {
      if (p - 3 > ictx->hbuf && utf8_char_len(p - 4) == 4) {
	p -= 3;
      } else if (p - 2 > ictx->hbuf && utf8_char_len(p - 3) == 3) {
	p -= 2;
      } else if (p - 1 > ictx->hbuf && utf8_char_len(p - 2) == 2) {
	p--;
      }
    }
    len = (ictx->hbuf + ictx->n_hbuf) - p;
    ensure_buffer(&ictx->hbuf_follow, &ictx->s_hbuf_follow,
		  ictx->n_hbuf_follow + len);
    if (ictx->n_hbuf_follow > 0)
      memmove(ictx->hbuf_follow + len, ictx->hbuf_follow, ictx->n_hbuf_follow);
    memcpy(ictx->hbuf_follow, p, len);
    ictx->n_hbuf_follow += len;
    ictx->n_hbuf -= len;
  }
}

static void
cmd_backspace(struct anthy_input_context* ictx)
{
  int len;

  len = rk_get_pending_str(ictx->rkctx, NULL, 0);
  if (len > 1) {
    char* buf;
    /* 確定されていないローマ字があるので、最後の文字をカット */
    len--;

    buf = (char*) malloc(len);
    rk_get_pending_str(ictx->rkctx, buf, len);
    rk_flush(ictx->rkctx);
    do_cmd_push_key(ictx, buf);
    free(buf);
  } else {
    if (brk_roman_get_previous_pending(ictx->rkctx)) {
      char *buf;
      buf = strdup(brk_roman_get_previous_pending(ictx->rkctx));
      ictx->n_hbuf -= brk_roman_get_decided_len(ictx->rkctx);

      rk_flush(ictx->rkctx);
      do_cmd_push_key(ictx,buf);
      free(buf);
    } else {
      if (ictx->n_hbuf >= 4 && utf8_char_len(ictx->hbuf + ictx->n_hbuf - 4) == 4) {
	ictx->n_hbuf -= 4;
      } else if (ictx->n_hbuf >= 3 && utf8_char_len(ictx->hbuf + ictx->n_hbuf - 3) == 3) {
	ictx->n_hbuf -= 3;
      } else if (ictx->n_hbuf >= 2 && utf8_char_len(ictx->hbuf + ictx->n_hbuf - 2) == 2) {
	ictx->n_hbuf -= 2;
      } else if (ictx->n_hbuf >= 1) {
	ictx->n_hbuf--;
      }
    }
  }

  if (ictx->n_hbuf + ictx->n_hbuf_follow <= 0 && len <= 1) {
    leave_edit_state(ictx);
    enter_none_state(ictx);
  }
}

static void
cmd_delete(struct anthy_input_context* ictx)
{
  int len;

  if (rk_get_pending_str(ictx->rkctx, NULL, 0) > 1)
    return;
  if (ictx->n_hbuf_follow <= 0)
    return;

  if (ictx->n_hbuf_follow >= 4 && utf8_char_len(ictx->hbuf_follow) == 4) {
    len = 4;
  } else if (ictx->n_hbuf_follow >= 3 && utf8_char_len(ictx->hbuf_follow) == 3) {
    len = 3;
  } else if (ictx->n_hbuf_follow >= 2 && utf8_char_len(ictx->hbuf_follow) == 2) {
    len = 2;
  } else {
    len = 1;
  }

  if (ictx->n_hbuf_follow <= len)
    ictx->n_hbuf_follow = 0;
  else {
    ictx->n_hbuf_follow -= len;
    memmove(ictx->hbuf_follow, ictx->hbuf_follow + len, ictx->n_hbuf_follow);
  }

  if (ictx->n_hbuf + ictx->n_hbuf_follow <= 0) {
    leave_edit_state(ictx);
    enter_none_state(ictx);
  }
}

static void
cmd_commit_unconv(struct anthy_input_context* ictx)
{
  ensure_buffer(&ictx->commit, &ictx->s_commit,
		ictx->n_commit + ictx->n_hbuf + ictx->n_hbuf_follow);
  memcpy(ictx->commit + ictx->n_commit, ictx->hbuf, ictx->n_hbuf);
  ictx->n_commit += ictx->n_hbuf;
  if (ictx->n_hbuf_follow > 0)
    memcpy(ictx->commit + ictx->n_commit,
	   ictx->hbuf_follow, ictx->n_hbuf_follow);
  ictx->n_commit += ictx->n_hbuf_follow;
}

static void
cmd_resize(struct anthy_input_context* ictx, int d)
{
  int i;
  struct anthy_conv_stat acs;
  struct a_segment* as;
  int last_pos;

  anthy_resize_segment(ictx->actx, ictx->cur_segment->index, d);
  anthy_get_stat(ictx->actx, &acs);

  anthy_get_segment_stat(ictx->actx,
			 ictx->cur_segment->index, &ictx->cur_segment->ass);
  ictx->cur_segment->cand = NTH_UNCONVERTED_CANDIDATE;
  last_pos = ictx->cur_segment->ass.seg_len;
  for (as = ictx->cur_segment, i = as->index + 1; i < acs.nr_segment; i++) {
    if (as->next == NULL) {
      struct a_segment* as2;

      if (!(as2 = (struct a_segment*) malloc(sizeof(struct a_segment)))) {
        anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
        break;
      }
      as2->index = i;
      as2->prev = as;
      as->next = as2;
      as2->next = NULL;
      as = as2;
    } else
      as = as->next;
    as->pos = last_pos;
    anthy_get_segment_stat(ictx->actx, i, &as->ass);
    last_pos += as->ass.seg_len;
    as->cand = NTH_UNCONVERTED_CANDIDATE;
  }
  ictx->last_gotten_cand = NTH_UNCONVERTED_CANDIDATE;

  as = as->next;            /* 不正なメモリアクセスの修正 */
  if (as) {
    as->prev->next = NULL;
    for (; as; ) {
      struct a_segment* const next = as->next;
      free(as);
      as = next;
    }
  }
}

static void
commit_noconv_string(struct anthy_input_context* ictx)
{
  join_noconv_string(ictx);
  ensure_buffer(&ictx->commit, &ictx->s_commit,
		ictx->n_commit + ictx->n_hbuf + 1);
  	/* +1 is just for an optimization */
  memcpy(ictx->commit + ictx->n_commit,
	 ictx->hbuf, ictx->n_hbuf);
  ictx->n_commit += ictx->n_hbuf;
  ictx->n_hbuf = 0;
}

static void
cmd_commit(struct anthy_input_context* ictx)
{
  do_cmd_commit(ictx);
}

static void
cmd_next_candidate(struct anthy_input_context* ictx)
{
  struct a_segment* as;

  ictx->enum_cand_count++;

  as = ictx->cur_segment;

  if (!ictx->enum_reverse)
    as->cand = ictx->last_gotten_cand;
  else
    ictx->enum_reverse = 0;

  if (as->cand == NTH_UNCONVERTED_CANDIDATE) {
    while (as) {
      if (as->cand == NTH_UNCONVERTED_CANDIDATE) {
	as->cand = 0;
      }
      as = as->next;
    }
    ictx->last_gotten_cand = 0;
  } else {
    if (++as->cand >= as->ass.nr_candidate)
      as->cand = 0;
    ictx->last_gotten_cand = as->cand;
  }
}

static void
cmd_prev_candidate(struct anthy_input_context* ictx)
{
  struct a_segment* as;

  ictx->enum_cand_count++;

  as = ictx->cur_segment;

  if (ictx->enum_reverse)
    as->cand = ictx->last_gotten_cand;
  else
    ictx->enum_reverse = 1;

  if (as->cand == NTH_UNCONVERTED_CANDIDATE) {
    while (as) {
      if (as->cand == NTH_UNCONVERTED_CANDIDATE) {
	as->cand = 0;
      }
      as = as->next;
    }
    ictx->last_gotten_cand = 0;
  } else {
    if (--as->cand < 0)
      as->cand = as->ass.nr_candidate - 1;
    ictx->last_gotten_cand = as->cand;
  }
}

static void
cmd_move_selection(struct anthy_input_context* ictx, int d)
{
  if (d > 0)
    while (d-- > 0 && ictx->cur_segment->next) {
      ictx->enum_cand_count = 0;
      ictx->cur_segment = ictx->cur_segment->next;
      ictx->last_gotten_cand = ictx->cur_segment->cand;
    }
  else
    while (d++ < 0 && ictx->cur_segment->prev) {
      ictx->enum_cand_count = 0;
      ictx->cur_segment = ictx->cur_segment->prev;
      ictx->last_gotten_cand = ictx->cur_segment->cand;
    }
}

static void
cmd_move_to_bol_seg(struct anthy_input_context* ictx)
{
  ictx->cur_segment = ictx->segment;
  ictx->enum_cand_count = 0;
  ictx->last_gotten_cand = ictx->cur_segment->cand;
}

static void
cmd_move_to_eol_seg(struct anthy_input_context* ictx)
{
  while (ictx->cur_segment->next)
    ictx->cur_segment = ictx->cur_segment->next;
  ictx->enum_cand_count = 0;
  ictx->last_gotten_cand = ictx->cur_segment->cand;
}

static void
cmd_unhiragana_candidate(struct anthy_input_context* ictx)
{
  struct a_segment* as;

  for (as = ictx->cur_segment->next; as; as = as->next)
    as->cand = 0;
}

static void
cmd_move_to_bol(struct anthy_input_context* ictx)
{
  terminate_rk(ictx);

  if (ictx->hbuf_follow == NULL) { /* 最適化 */
    ictx->hbuf_follow = ictx->hbuf;
    ictx->n_hbuf_follow = ictx->n_hbuf;
    ictx->s_hbuf_follow = ictx->s_hbuf;
    ictx->hbuf = NULL;
    ictx->n_hbuf = 0;
    ictx->s_hbuf = 0;
    return;
  }

  ensure_buffer(&ictx->hbuf_follow, &ictx->s_hbuf_follow,
		ictx->n_hbuf + ictx->n_hbuf_follow);
  memmove(ictx->hbuf_follow + ictx->n_hbuf,
	  ictx->hbuf_follow, ictx->n_hbuf_follow);
  memcpy(ictx->hbuf_follow, ictx->hbuf, ictx->n_hbuf);
  ictx->n_hbuf_follow += ictx->n_hbuf;
  ictx->n_hbuf = 0;
}

static void
cmd_move_to_eol(struct anthy_input_context* ictx)
{
  terminate_rk(ictx);

  if (ictx->hbuf == NULL) { /* 最適化 */
    ictx->hbuf = ictx->hbuf_follow;
    ictx->n_hbuf = ictx->n_hbuf_follow;
    ictx->s_hbuf = ictx->s_hbuf_follow;
    ictx->hbuf_follow = NULL;
    ictx->n_hbuf_follow = 0;
    ictx->s_hbuf_follow = 0;
    return;
  }

  ensure_buffer(&ictx->hbuf, &ictx->s_hbuf,
		ictx->n_hbuf + ictx->n_hbuf_follow);
  memcpy(ictx->hbuf + ictx->n_hbuf, ictx->hbuf_follow, ictx->n_hbuf_follow);
  ictx->n_hbuf += ictx->n_hbuf_follow;
  ictx->n_hbuf_follow = 0;
}

static void
cmd_cut(struct anthy_input_context* ictx)
{
  char* tmp_str;
  int   tmp_int;

  terminate_rk(ictx);

  /* バッファの入れ換えで済ませる */
  tmp_str = ictx->cut;
  tmp_int = ictx->s_cut;
  ictx->cut = ictx->hbuf_follow;
  ictx->n_cut = ictx->n_hbuf_follow;
  ictx->s_cut = ictx->s_hbuf_follow;
  ictx->hbuf_follow = tmp_str;
  ictx->n_hbuf_follow = 0;
  ictx->s_hbuf_follow = tmp_int;
}

/*****************************************************************/

/* pure function */
struct anthy_input_context*
anthy_input_create_context(struct anthy_input_config* cfg)
{
  struct anthy_input_context* ictx;
  int i;

  ictx =
    (struct anthy_input_context*) malloc(sizeof(struct anthy_input_context));
  if (!ictx) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      return NULL;
  }
  ictx->state = ANTHY_INPUT_ST_NONE;
  ictx->rkctx = rk_context_create(cfg->break_into_roman);
  for (i = 0; i < NR_RKMAP; i++)
    rk_register_map(ictx->rkctx, i, cfg->rk_map[i]);
  ictx->map_no = RKMAP_HIRAGANA;
  rk_select_registered_map(ictx->rkctx, ictx->map_no);
  ictx->hbuf = NULL;
  ictx->n_hbuf = 0;
  ictx->s_hbuf = 0;
  ictx->hbuf_follow = NULL;
  ictx->n_hbuf_follow = 0;
  ictx->s_hbuf_follow = 0;
  ictx->enum_cand_limit = DEFAULT_ENUM_CAND_LIMIT;
  ictx->enum_cand_count = 0;
  ictx->actx = NULL;
  ictx->segment = NULL;
  ictx->cur_segment = NULL;
  ictx->enum_reverse = 0;       /* 初期化忘れの修正 */
  ictx->last_gotten_cand = 0;   /* 初期化忘れの修正 */
  ictx->commit = NULL;
  ictx->n_commit = 0;
  ictx->s_commit = 0;
  ictx->cut = NULL;
  ictx->n_cut = 0;
  ictx->s_cut = 0;
  ictx->cfg = cfg;
  ictx->next_cfg_owner = cfg->owners;
  cfg->owners = ictx;
  return ictx;
}

void
anthy_input_free_context(struct anthy_input_context* ictx)
{
  struct anthy_input_context **p;

  reset_anthy_input_context(ictx);
  rk_context_free(ictx->rkctx);

  for (p = &ictx->cfg->owners; *p; p = &(*p)->next_cfg_owner)
    if (*p == ictx) {
      *p = ictx->next_cfg_owner;
      break;
    }

  free(ictx->hbuf);
  free(ictx->hbuf_follow);
  free(ictx->commit);
  free(ictx->cut);
  free(ictx);
}

void
anthy_input_free_preedit(struct anthy_input_preedit* pedit)
{
  struct anthy_input_segment* p, * q;

  free(pedit->commit);
  free(pedit->cut_buf);
  for (p = pedit->segment; p; p = q) {
    q = p->next;
    anthy_input_free_segment(p);
  }
  free(pedit);
}

void
anthy_input_free_segment(struct anthy_input_segment* seg)
{
  free(seg->str);
  free(seg);
}

void
anthy_input_str(struct anthy_input_context* ictx, const char* str)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    enter_edit_state(ictx);
    cmd_push_key(ictx, str);
    if (ictx->map_no == RKMAP_ASCII ||
	ictx->map_no == RKMAP_WASCII) {
      commit_noconv_string(ictx);
      leave_edit_state(ictx);
      enter_none_state(ictx);
    }
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_push_key(ictx, str);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_commit(ictx);
    leave_conv_state(ictx);
    enter_edit_state(ictx);
    cmd_push_key(ictx, str);
    break;
  case ANTHY_INPUT_ST_CSEG:
    cmd_commit(ictx);
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    leave_conv_state(ictx);
    enter_edit_state(ictx);
    cmd_push_key(ictx, str);
    break;
  }
}

void
anthy_input_next_candidate(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    enter_conv_state(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_next_candidate(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    cmd_unhiragana_candidate(ictx);
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    cmd_next_candidate(ictx);
    break;
  }
}


void
anthy_input_prev_candidate(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    enter_conv_state(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_prev_candidate(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    cmd_prev_candidate(ictx);
    break;
  }
}

void
anthy_input_quit(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    leave_edit_state(ictx);
    enter_none_state(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    leave_conv_state(ictx);
    enter_edit_state_noinit(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    leave_conv_state(ictx);
    enter_edit_state_noinit(ictx);
    break;
  }
}

void
anthy_input_erase_prev(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_backspace(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    leave_conv_state(ictx);
    enter_edit_state_noinit(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    leave_conv_state(ictx);
    enter_edit_state_noinit(ictx);
    break;
  }
}

void
anthy_input_erase_next(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_delete(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    break;
  case ANTHY_INPUT_ST_CSEG:
    break;
  }
}

void
anthy_input_commit(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    terminate_rk(ictx);
    cmd_commit_unconv(ictx);
    leave_edit_state(ictx);
    enter_none_state(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_commit(ictx);
    leave_conv_state(ictx);
    enter_none_state(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    cmd_commit(ictx);
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    leave_conv_state(ictx);
    enter_none_state(ictx);
    break;
  }
}

void
anthy_input_move(struct anthy_input_context* ictx, int lr)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_move_cursor(ictx, lr);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_move_selection(ictx, lr);
    break;
  case ANTHY_INPUT_ST_CSEG:
    cmd_unhiragana_candidate(ictx);
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    cmd_move_selection(ictx, lr);
    break;
  }
}

void
anthy_input_resize(struct anthy_input_context* ictx, int lr)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    break;
  case ANTHY_INPUT_ST_CONV:
    enter_cseg_state(ictx);
    cmd_resize(ictx, lr);
    break;
  case ANTHY_INPUT_ST_CSEG:
    cmd_resize(ictx, lr);
    break;
  }
}

void
anthy_input_beginning_of_line(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_move_to_bol(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_move_to_bol_seg(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    break;
  }
}

void
anthy_input_end_of_line(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_move_to_eol(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_move_to_eol_seg(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    break;
  }
}

void
anthy_input_cut(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    cmd_cut(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    break;
  case ANTHY_INPUT_ST_CSEG:
    break;
  }
}

/* key oriented function */
void
anthy_input_key(struct anthy_input_context* ictx, int c)
{
  char buf[2];

  buf[0] = (char) c;
  buf[1] = '\0';
  anthy_input_str(ictx, buf);
}

void
anthy_input_space(struct anthy_input_context* ictx)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
    enter_edit_state(ictx);
    do_cmd_push_key(ictx, " ");
    commit_noconv_string(ictx);
    leave_edit_state(ictx);
    enter_none_state(ictx);
    break;
  case ANTHY_INPUT_ST_EDIT:
    terminate_rk(ictx);
    if (rk_selected_map(ictx->rkctx) == RKMAP_SHIFT_ASCII)
      do_cmd_push_key(ictx, " ");
    else
      enter_conv_state(ictx);
    break;
  case ANTHY_INPUT_ST_CONV:
    cmd_next_candidate(ictx);
    break;
  case ANTHY_INPUT_ST_CSEG:
    cmd_unhiragana_candidate(ictx);
    leave_cseg_state(ictx);
    enter_conv_state_noinit(ictx);
    cmd_next_candidate(ictx);
    break;
  }
}

/* meta function command */

int
anthy_input_get_state(struct anthy_input_context* ictx)
{
  return ictx->state;
}

static struct anthy_input_segment *
alloc_segment(int flag, int len, int noconv_len)
{
  struct anthy_input_segment *seg;
  seg = (struct anthy_input_segment*)
    malloc(sizeof(struct anthy_input_segment));
  if (!seg)
    return NULL;
  seg->flag = flag;
  seg->cand_no = -1;
  seg->nr_cand = -1;
  seg->noconv_len = noconv_len;
  if (len) {
    seg->str = (char *)malloc(len);
  } else {
    seg->str = NULL;
  }
  seg->next = NULL;
  return seg;
}

static void
get_edit_mode_preedit(struct anthy_input_context* ictx,
		      struct anthy_input_preedit* pedit)
{
  struct anthy_input_segment** p;
  int len;
  /* 左の文字列pending|カーソル|右の文字列 */

  p = &pedit->segment;

  /* left */
  if (ictx->n_hbuf > 0) {
    *p = alloc_segment(ANTHY_INPUT_SF_EDITING, ictx->n_hbuf + 1,
		       ictx->n_hbuf);
    if (!(*p) || !((*p)->str)) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      ictx->n_hbuf = 0;
      return;
    }

    memcpy((*p)->str, ictx->hbuf, ictx->n_hbuf);
    (*p)->str[ictx->n_hbuf] = '\0';
    p = &(*p)->next;
  }

  if (ictx->cfg->preedit_mode) {
    len = rk_partial_result(ictx->rkctx, NULL, 0);
    if (len > 1) {
      if (!(*p = alloc_segment(ANTHY_INPUT_SF_PENDING, len, len - 1))) {
        anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
        pedit->cur_segment = NULL;
        return;
      }

      rk_partial_result(ictx->rkctx, (*p)->str, len);
      p = &(*p)->next;
    }
  } else {
    len = rk_get_pending_str(ictx->rkctx, NULL, 0);
    if (len > 1) {
      if (!(*p = alloc_segment(ANTHY_INPUT_SF_PENDING, len, len - 1))) {
        anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
        pedit->cur_segment = NULL;
        return;
      }

      rk_get_pending_str(ictx->rkctx, (*p)->str, len);
      p = &(*p)->next;
    }
  }

  /* cursor */
  if (!(*p = alloc_segment(ANTHY_INPUT_SF_CURSOR, 0, 0))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    pedit->cur_segment = NULL;
    return;
  }
  pedit->cur_segment = *p;
  p = &(*p)->next;

  /* right */
  if (ictx->n_hbuf_follow > 0) {
    *p = alloc_segment(ANTHY_INPUT_SF_EDITING,
		       ictx->n_hbuf_follow + 1,
		       ictx->n_hbuf_follow);
    if (!(*p) || !((*p)->str)) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    } else {
      memcpy((*p)->str, ictx->hbuf_follow, ictx->n_hbuf_follow);
      (*p)->str[ictx->n_hbuf_follow] = '\0';
    }
  }
}

struct anthy_input_preedit*
anthy_input_get_preedit(struct anthy_input_context* ictx)
{
  struct anthy_input_preedit* pedit;

  pedit = (struct anthy_input_preedit*)
    malloc(sizeof(struct anthy_input_preedit));
  if (!pedit) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return NULL;
  }

  pedit->state = ictx->state;

  /* 未コミットの文字列 */
  if (ictx->n_commit > 0) {
    if (!(pedit->commit = (char*) malloc(ictx->n_commit + 1))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    } else {
      memcpy(pedit->commit, ictx->commit, ictx->n_commit);
      pedit->commit[ictx->n_commit] = '\0';
    }
    ictx->n_commit = 0;
  } else {
    pedit->commit = NULL;
  }

  /* カットバッファの文字列 */
  if(ictx->n_cut > 0) {
    if (!(pedit->cut_buf = (char*) malloc(ictx->n_cut + 1))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    } else {
      memcpy(pedit->cut_buf, ictx->cut, ictx->n_cut);
      pedit->cut_buf[ictx->n_cut] = '\0';
    }
    ictx->n_cut = 0;
  } else {
    pedit->cut_buf = NULL;
  }

  pedit->segment = NULL;
  pedit->cur_segment = NULL;
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
  case ANTHY_INPUT_ST_NONE:
    break;
  case ANTHY_INPUT_ST_EDIT:
    get_edit_mode_preedit(ictx, pedit);
    break;
  case ANTHY_INPUT_ST_CONV:
  case ANTHY_INPUT_ST_CSEG:
    {
      struct anthy_input_segment** p;
      struct a_segment* as;

      for (as = ictx->segment, p = &pedit->segment; as; as = as->next) {
	/* 各文節に対して */
	int len, noconv_len;

	noconv_len = anthy_get_segment(ictx->actx, as->index,
				       NTH_UNCONVERTED_CANDIDATE,
				       NULL, 0);
	len = anthy_get_segment(ictx->actx, as->index, as->cand, NULL, 0);
	if (!(*p = alloc_segment(ANTHY_INPUT_SF_NONE, len + 1, noconv_len))) {
          anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
          return pedit;
	}

	anthy_get_segment(ictx->actx, as->index, as->cand, (*p)->str, len + 1);
	(*p)->cand_no = as->cand;
	(*p)->nr_cand = as->ass.nr_candidate;
	(*p)->next = NULL;

	if (as == ictx->cur_segment) {
	  pedit->cur_segment = *p;
	  (*p)->flag |= ANTHY_INPUT_SF_CURSOR;
	  if (ictx->enum_cand_count >= ictx->enum_cand_limit)
	    (*p)->flag |= (ictx->enum_reverse ?
			   ANTHY_INPUT_SF_ENUM_REVERSE : ANTHY_INPUT_SF_ENUM);

	  if (ictx->state == ANTHY_INPUT_ST_CSEG) {
	    struct a_segment* as1;

	    for (as1 = as->next, len = 0; as1; as1 = as1->next)
	      len += anthy_get_segment(ictx->actx, as1->index,
				       NTH_UNCONVERTED_CANDIDATE, NULL, 0);
	    if (len > 0) {
	      char* s;

	      p = &(*p)->next;
	      *p = alloc_segment(ANTHY_INPUT_SF_FOLLOWING, len + 1, len);
	      if (!(*p) || !((*p)->str)) {
                anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
                break;
	      }
	      for (as1 = as->next, s = (*p)->str; as1; as1 = as1->next) {
		anthy_get_segment(ictx->actx, as1->index,
				  NTH_UNCONVERTED_CANDIDATE,
				  s, len - (s - (*p)->str) + 1);
		s += anthy_get_segment(ictx->actx, as1->index,
				       NTH_UNCONVERTED_CANDIDATE, NULL, 0);
	      }
	      (*p)->str[len] = '\0';
	      (*p)->next = NULL;
	    }
	    break;
	  }
	}

	p = &(*p)->next;
      }
    }
    break;
  }

  return pedit;
}

int
anthy_input_map_select(struct anthy_input_context* ictx, int map)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
    break;
  case ANTHY_INPUT_ST_NONE:
  case ANTHY_INPUT_ST_EDIT:
  case ANTHY_INPUT_ST_CONV:
  case ANTHY_INPUT_ST_CSEG:
    return cmdh_map_select(ictx, map);
    break;
  }

  anthy_input_errno = AIE_INVAL;
  return -1;
}

int
anthy_input_get_selected_map(struct anthy_input_context* ictx)
{
  return ictx->map_no;
}

struct anthy_input_segment*
anthy_input_get_candidate(struct anthy_input_context* ictx, int cand_no)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_CONV:
    return cmdh_get_candidate(ictx, cand_no);
    break;
  case ANTHY_INPUT_ST_OFF:
  case ANTHY_INPUT_ST_NONE:
  case ANTHY_INPUT_ST_EDIT:
  case ANTHY_INPUT_ST_CSEG:
    break;
  }

  anthy_input_errno = AIE_INVAL;
  return NULL;
}

int
anthy_input_select_candidate(struct anthy_input_context* ictx, int cand)
{
  switch (ictx->state) {
  case ANTHY_INPUT_ST_OFF:
  case ANTHY_INPUT_ST_NONE:
  case ANTHY_INPUT_ST_EDIT:
    break;
  case ANTHY_INPUT_ST_CONV:
    return cmdh_select_candidate(ictx, cand);
    break;
  case ANTHY_INPUT_ST_CSEG:
    break;
  }

  anthy_input_errno = AIE_INVAL;
  return -1;
}

int
anthy_input_edit_toggle_config(struct anthy_input_config *cfg, char tg)
{
  return anthy_input_do_edit_toggle_option(cfg->rk_option, tg);
}

int
anthy_input_edit_rk_config(struct anthy_input_config *cfg, int map,
			   const char *from, const char *to, const char *follow)
{
  return
    anthy_input_do_edit_rk_option(cfg->rk_option, map,
				  from, to, follow);
}

int
anthy_input_clear_rk_config(struct anthy_input_config *cfg,
			    int use_default)
{
  return
    anthy_input_do_clear_rk_option(cfg->rk_option, use_default);
}

int
anthy_input_break_into_roman_config(struct anthy_input_config *cfg,
				    int brk)
{
  int old_val;
  old_val = cfg->break_into_roman;
  cfg->break_into_roman = brk;
  return old_val;
}

int
anthy_input_preedit_mode_config(struct anthy_input_config *cfg,
				int val)
{
  int old_val;
  old_val = cfg->preedit_mode;
  cfg->preedit_mode = val;
  return old_val;
}

void
anthy_input_change_config(struct anthy_input_config* cfg)
{
  struct anthy_input_context* p;

  struct rk_map* h_map = cfg->rk_map[RKMAP_HIRAGANA];
  struct rk_map* k_map = cfg->rk_map[RKMAP_KATAKANA];
  struct rk_map* s_map = cfg->rk_map[RKMAP_SHIFT_ASCII];
  struct rk_map* hk_map = cfg->rk_map[RKMAP_HANKAKU_KANA];

  cfg->rk_map[RKMAP_HIRAGANA] = make_rkmap_hiragana(cfg->rk_option);
  cfg->rk_map[RKMAP_KATAKANA] = make_rkmap_katakana(cfg->rk_option);
  cfg->rk_map[RKMAP_SHIFT_ASCII] = make_rkmap_shiftascii(cfg->rk_option);
  cfg->rk_map[RKMAP_HANKAKU_KANA] = make_rkmap_hankaku_kana(cfg->rk_option);

  /* このconfigを共有するコンテキストすべてに対して */
  for (p = cfg->owners; p; p = p->next_cfg_owner) {
    reset_anthy_input_context(p);
    rk_register_map(p->rkctx, RKMAP_HIRAGANA, cfg->rk_map[RKMAP_HIRAGANA]);
    rk_register_map(p->rkctx, RKMAP_KATAKANA, cfg->rk_map[RKMAP_KATAKANA]);
    rk_register_map(p->rkctx, RKMAP_SHIFT_ASCII,
		    cfg->rk_map[RKMAP_SHIFT_ASCII]);
    rk_register_map(p->rkctx, RKMAP_HANKAKU_KANA,
		    cfg->rk_map[RKMAP_HANKAKU_KANA]);
    rk_select_registered_map(p->rkctx, RKMAP_HIRAGANA);
  }

  rk_map_free(h_map);
  rk_map_free(k_map);
  rk_map_free(s_map);
  rk_map_free(hk_map);
}

struct anthy_input_config*
anthy_input_create_config(void)
{
  struct anthy_input_config* cfg;

  cfg = (struct anthy_input_config*) malloc(sizeof(struct anthy_input_config));
  if (!cfg) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return NULL;
  }

  cfg->rk_option = anthy_input_create_rk_option();
  cfg->break_into_roman = 0;
  cfg->preedit_mode = 0;
  cfg->rk_map[RKMAP_ASCII] = make_rkmap_ascii(cfg->rk_option);
  cfg->rk_map[RKMAP_SHIFT_ASCII] = make_rkmap_shiftascii(cfg->rk_option);
  cfg->rk_map[RKMAP_HIRAGANA] = make_rkmap_hiragana(cfg->rk_option);
  cfg->rk_map[RKMAP_KATAKANA] = make_rkmap_katakana(cfg->rk_option);
  cfg->rk_map[RKMAP_WASCII] = make_rkmap_wascii(cfg->rk_option);
  cfg->rk_map[RKMAP_HANKAKU_KANA] = make_rkmap_hankaku_kana(cfg->rk_option);
  cfg->owners = NULL;

  return cfg;
}

void
anthy_input_free_config(struct anthy_input_config* cfg)
{
  /* int err; */

  /* このconfigを共有する全てのcontextを事前に解放する事 */
  assert(!cfg->owners);

  rk_map_free(cfg->rk_map[RKMAP_ASCII]);
  rk_map_free(cfg->rk_map[RKMAP_SHIFT_ASCII]);
  rk_map_free(cfg->rk_map[RKMAP_HIRAGANA]);
  rk_map_free(cfg->rk_map[RKMAP_KATAKANA]);
  rk_map_free(cfg->rk_map[RKMAP_WASCII]);
  rk_map_free(cfg->rk_map[RKMAP_HANKAKU_KANA]);


  /* There should be some error code checking here I think, but for now it's just producing compile warnings */
  /* err =  */ anthy_input_free_rk_option(cfg->rk_option);
  free(cfg);
}

int
anthy_input_init(void)
{
  return anthy_init();
}

void
anthy_input_set_personality(const char *personality)
{
  anthy_set_personality(personality);
}

anthy_context_t
anthy_input_get_anthy_context(struct anthy_input_context *ictx)
{
  return ictx->actx;
}
