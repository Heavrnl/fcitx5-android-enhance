/* コーパスの構築及びアクセスを行う */
#ifndef _corpus_h_included_
#define _corpus_h_included_

#include <stdio.h>

struct corpus;

#define ELM_NONE 0
#define ELM_BOS 0x10000000
#define ELM_WORD_BORDER 0x20000000
#define ELM_INVALID 0x40000000

/* hash値の下位28bitを使う */
#define CORPUS_KEY_MASK 0x0fffffff

/* コーパス構築用の関数 */
struct corpus *corpus_new(void);
void corpus_push_back(struct corpus *c, int *val, int nr, int flags);
void corpus_build(struct corpus *c);
void corpus_dump(struct corpus *c);
void corpus_write_bucket(FILE *fp, struct corpus *c);
void corpus_write_array(FILE *fp, struct corpus *c);
void corpus_free(struct corpus *c);

#endif
