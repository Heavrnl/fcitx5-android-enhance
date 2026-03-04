/* 辞書ライブラリ(libanthydic)と
 * 辞書生成の両方から使う
 * ファイル辞書の構造
 */
#ifndef _word_dic_h_included_
#define _word_dic_h_included_

/* 読みhashのbit mapの大きさ */
#define YOMI_HASH_ARRAY_SIZE (65536*4)
#define YOMI_HASH_ARRAY_SHIFT 3
#define YOMI_HASH_ARRAY_BITS (1<<YOMI_HASH_ARRAY_SHIFT)

/* 汎用のhash */
#define VERSATILE_HASH_SIZE (128*1024)

/* 1ページ内にいくつの単語を入れるか */
#define WORDS_PER_PAGE 64

/** 辞書ファイル 
 * 辞書ライブラリ用
 */
struct word_dic {
  /** 辞書ファイル自体のポインタ */
  char *dic_file;
  /** 辞書エントリのインデックスの配列(ネットワークバイトオーダー) */
  int *entry_index;
  /** 辞書エントリ */
  char *entry;
  /** インデックスへのインデックス */
  int *page_index;
  /** 辞書のインデックス */
  char *page;
  /** 用例辞書 */
  char *uc_section;

  /* 単語辞書 */
  int nr_pages;
  unsigned char *hash_ent;
};

#endif

