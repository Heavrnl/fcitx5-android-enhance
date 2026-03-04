/*
 * 用例辞書を扱う
 * Copyright (C) 2003 TABATA Yusuke
 */
#include <string.h>
#include <stdlib.h>

#include <anthy/dic.h>
#include <anthy/xstr.h>
#include <anthy/matrix.h>
#include <anthy/word_dic.h>
#include "dic_main.h"
#include "dic_ent.h"

/**/
int anthy_word_dic_check_word_relation(struct word_dic *wdic,
				       int from, int to)
{
  /* 共有辞書 */
  return anthy_matrix_image_peek((int *)wdic->uc_section, from, to);
}
