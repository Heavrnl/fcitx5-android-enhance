#ifndef _texttrie_h_included_
#define _texttrie_h_included_

struct text_trie;

/* database manipulation */
struct text_trie *anthy_trie_open(const char *fn, int create);
void anthy_trie_close(struct text_trie *tt);
void anthy_trie_update_mapping(struct text_trie *tt);
/**/
int anthy_trie_add(struct text_trie *tt, const char *key, const char *body);
/* caller should free the result */
char *anthy_trie_find(struct text_trie *a, char *key);
void anthy_trie_delete(struct text_trie *tt, const char *key);
/**/
char *anthy_trie_find_next_key(struct text_trie *tt, char *buf, int len);
/**/
void anthy_trie_find_prefix(struct text_trie *tt, const char *str,
			    char *buf, int len,
			    int (*cb)(const char *key, const char *str));
/**/
void anthy_trie_print_array(struct text_trie *tt);

#endif
