/* 
 * Funded by IPA未踏ソフトウェア創造事業 2001
 * Copyright (C) 2001-2002 UGAWA Tomoharu
 */

#ifndef RKHELPER_H_INCLUDE
#define RKHELPER_H_INCLUDE

#define RKOPT_US 0
#define RKOPT_JP 1

enum {
  RKMAP_ASCII, RKMAP_SHIFT_ASCII,
  RKMAP_HIRAGANA, RKMAP_KATAKANA,
  RKMAP_WASCII, RKMAP_HANKAKU_KANA,
  NR_RKMAP
};

#define RK_OPTION_SYMBOL  0
#define RK_OPTION_TOGGLE  1
#define RK_OPTION_ERROR  -1

struct rk_option;

/* rk_optionの初期化と変更 */
struct rk_option *anthy_input_create_rk_option(void);
int anthy_input_free_rk_option(struct rk_option *opt);
int anthy_input_do_edit_rk_option(struct rk_option* opt, int map,
				  const char* from, const char* to, const char *follow);
int anthy_input_do_edit_toggle_option(struct rk_option *opt, char toggle);
int anthy_input_do_clear_rk_option(struct rk_option *opt, int enable_default);

/* rk_mapの生成 */
struct rk_map* make_rkmap_ascii(struct rk_option* opt);
struct rk_map* make_rkmap_wascii(struct rk_option* opt);
struct rk_map* make_rkmap_shiftascii(struct rk_option* opt);
struct rk_map* make_rkmap_hiragana(struct rk_option* opt);
struct rk_map* make_rkmap_katakana(struct rk_option* opt);
struct rk_map* make_rkmap_hankaku_kana(struct rk_option* opt);

#endif /* RKHELPER_H_INCLUDE */
