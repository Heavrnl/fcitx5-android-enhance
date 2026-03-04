/*
 * 辞書モジュールのインターフェース
 */
#ifndef _dic_h_included_
#define _dic_h_included_

#include "xstr.h"
#include "wtype.h"

/** 辞書の読みに対するハンドル(sequence entry) */
typedef struct seq_ent *seq_ent_t;
/***/
typedef struct dic_ent *compound_ent_t;

/* 全体の初期化、解放 */
int anthy_init_dic(void);
void anthy_quit_dic(void);

/* 他プロセスに対する排他制御 */
void anthy_lock_dic(void);
void anthy_unlock_dic(void);

/**/
void anthy_gang_load_dic(xstr *xs, int is_reverse);

/* 文字列の取得 */
seq_ent_t anthy_get_seq_ent_from_xstr(xstr *xs, int is_reverse);
/* 文字列の情報 */
int anthy_get_nr_dic_ents(seq_ent_t se, xstr *xs);
int anthy_has_compound_ents(seq_ent_t se);
int anthy_has_non_compound_ents(seq_ent_t se);
int anthy_get_nth_dic_ent_is_compound(seq_ent_t se, int nth);
/* 非複合語 */
/* caller should free @res */
int anthy_get_nth_dic_ent_str(seq_ent_t, xstr *orig, int, xstr *res);
int anthy_get_nth_dic_ent_freq(seq_ent_t, int nth);
int anthy_get_nth_dic_ent_wtype(seq_ent_t, xstr *, int nth, wtype_t *w);
/*  品詞 */
int anthy_get_seq_ent_pos(seq_ent_t, int pos);
int anthy_get_seq_ent_ct(seq_ent_t, int pos, int ct);
int anthy_get_seq_ent_wtype_freq(seq_ent_t, wtype_t);
int anthy_get_seq_ent_indep(seq_ent_t se);
/* 複合語 */
compound_ent_t anthy_get_nth_compound_ent(seq_ent_t se, int nth);
int anthy_get_seq_ent_wtype_compound_freq(seq_ent_t se, wtype_t wt);
/**/
int anthy_compound_get_wtype(compound_ent_t, wtype_t *w);
int anthy_compound_get_freq(compound_ent_t ce);
int anthy_compound_get_nr_segments(compound_ent_t ce);
int anthy_compound_get_nth_segment_len(compound_ent_t ce, int nth);
int anthy_compound_get_nth_segment_xstr(compound_ent_t ce, int nth, xstr *xs);



/** 辞書セッション
 *
 */
typedef struct mem_dic *dic_session_t;
/*typedef struct dic_session *dic_session_t;*/

dic_session_t anthy_dic_create_session(void);
void anthy_dic_activate_session(dic_session_t );
void anthy_dic_release_session(dic_session_t);

/* personality */
void anthy_dic_set_personality(const char *);
/**/
#define ANON_ID ""


/** 用例辞書
 */
int anthy_dic_check_word_relation(int from, int to);

/** 未知語の学習
 */
void anthy_add_unknown_word(xstr *yomi, xstr *word);
void anthy_forget_unused_unknown_word(xstr *xs);

#endif
