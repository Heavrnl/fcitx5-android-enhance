#ifndef _dic_main_h_included_
#define _dic_main_h_included_

#include <anthy/dic.h>
#include <anthy/word_dic.h>
#include <anthy/wtype.h>
#include <anthy/xstr.h>


/* 辞書中の頻度に対して内部の頻度の倍率 */
#define FREQ_RATIO 8


/* dic_main.c */
int anthy_init_dic_cache(void);
struct seq_ent *anthy_cache_get_seq_ent(xstr *x, int is_reverse);


/* word_dic.c */
/* 辞書検索のキーに使用する部分文字列 */
struct gang_elm {
  char *key;
  xstr xs;
  union {
    /* 省メモリのためにunionにしている */
    int idx;
    struct gang_elm *next;
  } tmp;
};
struct seq_ent *anthy_cache_get_seq_ent(xstr *xs, int is_reverse);
struct seq_ent *anthy_validate_seq_ent(struct seq_ent *seq, xstr *xs,
				       int is_reverse);


/* word_lookup.c */
void anthy_init_word_dic(void);
struct word_dic* anthy_create_word_dic(void);
void anthy_release_word_dic(struct word_dic *);
void anthy_gang_fill_seq_ent(struct word_dic *wd,
			     struct gang_elm **array, int nr,
			     int is_reverse);


/* use_dic.c */
void anthy_init_use_dic(void);
void anthy_quit_use_dic(void);
int anthy_word_dic_check_word_relation(struct word_dic *,
				       int from, int to);

struct dic_session *anthy_create_session(void);
void anthy_activate_session(struct dic_session *);
void anthy_release_session(struct dic_session *);
int anthy_get_current_session_mask(void);

/* mem_dic.c */
void anthy_init_mem_dic(void);
void anthy_quit_mem_dic(void);
struct mem_dic * anthy_create_mem_dic(void);
void anthy_release_mem_dic(struct mem_dic * );
/* node がなければ作る */
struct seq_ent *anthy_mem_dic_alloc_seq_ent_by_xstr(struct mem_dic * d,
						    xstr *, int is_reverse);
/* node がなければ作らない */
struct seq_ent *anthy_mem_dic_find_seq_ent_by_xstr(struct mem_dic * d,
						   xstr *, int is_reverse);
/**/
void anthy_mem_dic_push_back_dic_ent(struct seq_ent *se, int is_compound,
				     xstr *xs, wtype_t wt,
				     const char *wt_name, int freq,
				     int feature);
void anthy_mem_dic_release_seq_ent(struct mem_dic * d, xstr *, int is_reverse);


/* priv_dic.c */
void anthy_init_private_dic(const char *id);
void anthy_copy_words_from_private_dic(struct seq_ent *seq, xstr *xs,
				       int is_reverse);
void anthy_release_private_dic(void);
const char *anthy_get_user_dir(int is_old);
void anthy_check_user_dir(void);
void anthy_priv_dic_lock(void);
void anthy_priv_dic_unlock(void);
void anthy_priv_dic_update(void);
struct word_line {
  char wt[10];
  int freq;
  const char *word;
};
int anthy_parse_word_line(const char *line, struct word_line *res);
struct textdict;
void anthy_ask_scan(void (*request_scan)(struct textdict *, void *),
		    void *arg);

#endif
