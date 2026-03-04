/*
 * 辞書側でのパーソナリティの管理
 * リファレンスカウントなどはフロントエンドがやる。
 */
#ifndef _dic_personality_h_included_
#define _dic_personality_h_included_

extern struct mem_dic *anthy_current_personal_dic_cache;
extern struct record_stat *anthy_current_record;

/* record */
void anthy_init_record(void);
struct record_stat *anthy_create_record(const char *id);
void anthy_release_record(struct record_stat *);

/* dic_cache */
struct dic_cache *anthy_create_dic_cache(const char *id);
void anthy_release_dic_cache(struct dic_cache *);

#endif
