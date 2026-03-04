/* 設定を取得するためのインタフェース */
#ifndef _conf_h_included_
#define _conf_h_included_

void anthy_do_conf_init(void);
void anthy_do_conf_override(const char *, const char *);
void anthy_conf_free(void);

const char *anthy_conf_get_str(const char *var);

#endif
