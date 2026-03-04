/* 候補の順序を決定するためのモジュール */
#ifndef _ordering_h_included_
#define _ordering_h_included_

#include <anthy/xstr.h>

struct segment_list;
struct splitter_context;

/** ordering_contextのwrapper構造体
 */
struct ordering_context_wrapper{
  struct ordering_context *oc;
};

void anthy_proc_commit(struct segment_list *, struct splitter_context *);

void anthy_sort_candidate(struct segment_list *c, int nth);
void anthy_sort_metaword(struct segment_list *seg);

void anthy_do_commit_prediction(xstr *src, xstr *xs);

/**/
void anthy_infosort_init(void);
void anthy_relation_init(void);

#endif
