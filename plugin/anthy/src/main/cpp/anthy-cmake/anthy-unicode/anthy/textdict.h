#ifndef _textdict_h_included_
#define _textdict_h_included_

struct textdict;

struct textdict *anthy_textdict_open(const char *fn);
void anthy_textdict_close(struct textdict *td);
/**/
void anthy_textdict_scan(struct textdict *td, int offset, void *ptr,
			 int (*fn)(void *, int, const char *, const char *));
int anthy_textdict_insert_line(struct textdict *td,
			       int offset, const char *line);
int anthy_textdict_delete_line(struct textdict *td, int offset);

#endif
