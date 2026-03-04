#ifndef _sorter_h_included_
#define _sorter_h_included_

/* candswap.c */
/* 一位として出した候補ではない候補がコミットされた */
void anthy_swap_cand_ent(struct cand_ent *old_one, struct cand_ent *new_one);
/**/
void anthy_proc_swap_candidate(struct seg_ent *se);
/* コミット時にcandswapの記録をagingする */
void anthy_cand_swap_ageup(void);

/**/
void anthy_reorder_candidates_by_relation(struct segment_list *sl, int nth);

void anthy_learn_cand_history(struct segment_list *sl);
void anthy_reorder_candidates_by_history(struct seg_ent *se);

#endif
