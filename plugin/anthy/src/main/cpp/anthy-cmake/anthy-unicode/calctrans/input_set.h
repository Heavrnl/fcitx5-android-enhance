/**/
#ifndef _input_set_h_included_
#define _input_set_h_included_

#include <stdio.h>

struct input_set;
struct int_map;

struct input_line {
  /**/
  int weight;
  int negative_weight;
  /**/
  int nr_features;
  int *features;
  /**/
  struct input_line *next_line;
  struct input_line *next_in_hash;
};

struct input_set *input_set_create(void);
void input_set_set_features(struct input_set *is, int *features,
			    int nr, int strength);
struct input_set *input_set_filter(struct input_set *is,
				   double pos, double neg);
void input_set_output_feature_freq(FILE *fp, struct input_set *is);
/**/
struct input_line *input_set_get_input_line(struct input_set *is);
void input_set_free(struct input_set *is);


struct int_map *int_map_new(void);
int int_map_peek(struct int_map *im, int idx);
void int_map_set(struct int_map *im, int idx, int val);
void int_map_flatten(struct int_map *im);

#endif
