#ifndef RKCONV_H_INCLUDED
#define RKCONV_H_INCLUDED

struct rk_map;
struct rk_rule
{
	const char* lhs;
	const char* rhs;
	const char* follow;
};
struct rk_conv_context;

struct rk_map*
rk_map_create(const struct rk_rule* rules);
struct rk_conv_context*
rk_context_create(int brk);
struct rk_map*
rk_select_map(struct rk_conv_context* cc, struct rk_map* map);
int
rk_push_key(struct rk_conv_context* cc, int c);
int
rk_result(struct rk_conv_context* cc, char* buf, int size);
void
rk_context_free(struct rk_conv_context* cc);
int
rk_map_free(struct rk_map* map);
int
rk_partial_result(struct rk_conv_context* cc, char* buf, int size);

void
rk_flush(struct rk_conv_context* cc);
void
rk_terminate(struct rk_conv_context* cc);
int
rk_get_pending_str(struct rk_conv_context* cc, char* buf, int size);
struct rk_map* 
rk_register_map(struct rk_conv_context* cc, int mapn, struct rk_map* map);
void
rk_select_registered_map(struct rk_conv_context* cc, int mapn);
int
rk_selected_map(struct rk_conv_context* cc);

struct rk_rule*
rk_merge_rules(const struct rk_rule* r1, const struct rk_rule* r2);
void
rk_rules_free(struct rk_rule* rules);

const char *brk_roman_get_previous_pending(struct rk_conv_context *);
int         brk_roman_get_decided_len(struct rk_conv_context *);

#endif /* RKCONV_H_INCLUDED */
