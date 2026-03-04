/* 単語の頻度を計算する
 *
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 */

#include <stdlib.h>

#include <anthy/logger.h>
#include "mkdic.h"

static int
count_nr_words(struct yomi_entry_list *yl)
{
  int nr = 0;
  struct yomi_entry *ye;
  for (ye = yl->head; ye; ye = ye->next) {
    nr += ye->nr_entries;
  }
  return nr;
}

static struct word_entry **
make_word_array(struct yomi_entry_list *yl, int nr)
{
  struct word_entry **array;
  int nth = 0;
  struct yomi_entry *ye;
  if (!(array = malloc(sizeof(struct word_entry *) * nr)))
    return NULL;
  for (ye = yl->head; ye; ye = ye->next) {
    int i;
    for (i = 0; i < ye->nr_entries; i++) {
      array[nth] = &ye->entries[i];
      nth ++;
    }
  }
  return array;
}

/** qsort用の比較関数 */
static int
compare_word_entry_by_freq(const void *p1, const void *p2)
{
  const struct word_entry * const *e1 = p1;
  const struct word_entry * const *e2 = p2;
  return abs((*e2)->raw_freq) - abs((*e1)->raw_freq);
}

static void
set_freq(struct word_entry **array, int nr)
{
  int i;
  int percent = nr / 100;
  percent = percent ? percent : 1;
  for (i = 0; i < nr; i++) {
    struct word_entry *we = array[i];
    /* Effect よのなかほんとうにべんりになった in test/test.txt
     * 便利 vs 弁理
     * べんり #T05*300 便利 #T35*180 弁理 in alt-cannadic/gcanna.ctd
     */
    we->freq = (int)(99.0 - ((double)i / percent));
    if (we->freq < 1) {
      we->freq = 1;
    }
    we->freq *= 100;
    we->freq -= we->source_order;
    if (we->raw_freq < 0) {
      we->freq *= -1;
    }
  }
}

void
calc_freq(struct yomi_entry_list *yl)
{
  int nr;
  struct word_entry **we;
  /**/
  nr = count_nr_words(yl);
  if (!(we = make_word_array(yl, nr))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return;
  }
  /**/
  qsort(we, nr,
	sizeof(struct word_entry *),
	compare_word_entry_by_freq);
  set_freq(we, nr);
  /**/
  free(we);
}

