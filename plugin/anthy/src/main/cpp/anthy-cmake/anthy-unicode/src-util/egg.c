/*
 * Copyright (C) 2002 The Free Software Initiative of Japan
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 * Author: NIIBE Yutaka
 */

/*
 * ANTHY Low Level Agent
 */

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <anthy/anthy.h>
#include <anthy/logger.h>

#include "config.h"

/*
 *            connection        context
 *  S -- open -> [ ] -- new ----> [ ] -- convert --> [ ] --> get_candidates
 *   <- close --     <- release -     <-- commit --      --> resize_segment
 *                                    <-- cancel --      --> select_candidate
 *
 */

struct context {
  struct anthy_context *ac;
  int buflen;
  unsigned char *buf;
  int removed, inserted;
  int sellen;
  int *selection;
};

#define MAX_CONTEXT 16
static struct context contexts[MAX_CONTEXT];

extern int use_eucjp;

#define INITIAL_BUFLEN 512
#define INITIAL_SELLEN 128

/*
 * Returns -1 on error.
 * Returns >= 0 on success, and the number is  context descriptor.
 */
static int
new_context (void)
{
  int i;

  for (i = 0; i < MAX_CONTEXT; i++)
    if (contexts[i].buflen == 0) { /* Found free one */
      struct context *c = &contexts[i];

      if ((c->buf = (unsigned char *)malloc (INITIAL_BUFLEN)) == NULL)
	return -1;

      if ((c->selection = (int *)malloc (INITIAL_SELLEN)) == NULL) {
	free (c->buf);
	c->buf = NULL;
	return -1;
      }

      if ((c->ac = anthy_create_context ()) == NULL) {
	free (c->buf);
	c->buf = NULL;
	free (c->selection);
	c->selection = NULL;
	return -1;
      }
      if (use_eucjp) {
	anthy_context_set_encoding(c->ac, ANTHY_EUC_JP_ENCODING);
      } else {
	anthy_context_set_encoding(c->ac, ANTHY_UTF8_ENCODING);
      }

      c->buflen = INITIAL_BUFLEN;
      c->sellen = INITIAL_SELLEN;
      return i;
    }

  /* No free context to be used */
  return -1;
}

static int
release_context (int c_desc)
{
  struct context *c = &contexts[c_desc];

  anthy_release_context(c->ac);
  free (c->buf);
  c->buf = NULL;
  c->buflen = 0;
  free (c->selection);
  c->selection = NULL;
  c->sellen = 0;

  return 0;
}

static struct context *
c_desc_to_context (int c_desc)
{
  return &contexts[c_desc];
}

static int
get_number_of_segments (struct context *c)
{
  struct anthy_conv_stat cs;

  if (anthy_get_stat(c->ac, &cs) < 0)
    return -1;

  return cs.nr_segment;
}

static int
begin_conversion (struct context *c, const char *input)
{
  int i;
  int seg_num;
  if (anthy_set_string(c->ac, (char *)input) < 0)
    return -1;

  seg_num = get_number_of_segments (c);
  if (seg_num >= c->sellen) {
    c->sellen *= 2;
    if (sizeof(int) * c->sellen >= INT_MAX || c->sellen < 0) {
      anthy_log(0, "Exceed max allocation size: %lu >= %d\n",
                (unsigned long)sizeof(int) * c->sellen, INT_MAX);
      return -1;
    }
    c->selection = realloc (c->selection, c->sellen);
    if (c->selection == NULL) { /* Fatal */
      c->sellen = -1;
      return -1;
    }
  }

  for (i = 0; i < seg_num; i++)
    c->selection[i] = 0;

  return 0;
}

static int
end_conversion (struct context *c, int cancel)
{
  int n;
  int seg_num;
  int can_num;

  if (!cancel) {
    n = get_number_of_segments (c);
    for (seg_num = 0; seg_num < n; seg_num++) {
      can_num = c->selection[seg_num];
      anthy_commit_segment(c->ac, seg_num, can_num);
    }
  }

  return 0;
}

static int
get_segment_number_of_candidates (struct context *c, int seg_num)
{
  struct anthy_segment_stat ss;

  if (anthy_get_segment_stat (c->ac, seg_num, &ss) != 0)
    return -1;

  return ss.nr_candidate;
}

static const unsigned char *
get_segment_candidate (struct context *c, int seg_num, int cand_num)
{
  int len;

  while (1) {
    len = anthy_get_segment (c->ac, seg_num, cand_num,
			     (char *)c->buf, c->buflen);

    if (len < 0)
      return NULL;

    if (len < c->buflen)
      return c->buf;

    c->buflen *= 2;
    c->buf = realloc (c->buf, c->buflen);
    if (c->buf == NULL) { /* Fatal */
      c->buflen = -1;
      return NULL;
    }
  }
}

static const unsigned char *
get_segment_yomi (struct context *c, int seg_num)
{
  return get_segment_candidate (c, seg_num, NTH_UNCONVERTED_CANDIDATE);
}

static const unsigned char *
get_segment_converted (struct context *c, int seg_num)
{
  return get_segment_candidate (c, seg_num, 0);
}

static int
resize_segment (struct context *c, int seg_num, int inc_dec)
{
  int i;
  struct anthy_conv_stat cs;

  if (anthy_get_stat(c->ac, &cs) < 0)
    return -1;

  /* Replace all segments after SEG_NUM */
  c->removed = cs.nr_segment - seg_num;
  anthy_resize_segment(c->ac, seg_num, inc_dec?-1:1);
  if (anthy_get_stat(c->ac, &cs) < 0)
    return -1;
  c->inserted = cs.nr_segment - seg_num;

  if (cs.nr_segment >= c->sellen) {
    c->sellen *= 2;
    c->selection = realloc (c->selection, c->sellen);
    if (c->selection == NULL) { /* Fatal */
      c->sellen = -1;
      return -1;
    }
  }
  for (i = seg_num; i < cs.nr_segment; i++)
    c->selection[i] = 0;

  return seg_num;
}

/* Only valid after call of resize_segment or select_candidate */
static int
get_number_of_segments_removed (struct context *c, int seg_num)
{
  (void)seg_num;
  return c->removed;
}

/* Only valid after call of resize_segment or select_candidate */
static int
get_number_of_segments_inserted (struct context *c, int seg_num)
{
  (void)seg_num;
  return c->inserted;
}

static int
select_candidate (struct context *c, int seg_num, int can_num)
{
  /*
   * Anthy does not have capability to affect the result of selection
   * to other segments.
   */
  c->removed = 0;
  c->inserted = 0;

  /*
   * Record, but not call anthy_commit_segment.
   */
  c->selection[seg_num] = can_num;

  return seg_num;
}

static int
say_hello (void)
{
  const char *options = "";

  printf ("Anthy (Version %s) [%s] : Nice to meet you.\r\n", VERSION,
	  options);
  fflush (stdout);
  return 0;
}

#define ERROR_CODE_UNKNOWN	400
#define ERROR_CODE_UNSUPPOTED	401

static int
say_unknown (void)
{
  printf ("-ERR %d Unknown command.\r\n", ERROR_CODE_UNKNOWN);
  fflush (stdout);
  return 0;
}

static int
do_commit (const char *line)
{
  char *p;
  struct context *c;
  int c_desc, cancel, r;

  c_desc = strtol (line+7, &p, 10);
  c = c_desc_to_context (c_desc);
  cancel = strtol (p+1, &p, 10);
  r = end_conversion (c, cancel);
  if (r < 0)
    printf ("-ERR %d commit failed.\r\n", -r);
  else
    printf ("+OK\r\n");
  fflush (stdout);
  return 0;
}

static void
output_segments (struct context *c, int seg_num, int removed, int inserted)
{
  int i;

  printf ("+DATA %d %d %d\r\n", seg_num, removed, inserted);
  for (i = seg_num; i < seg_num + inserted; i++) {
    int nc;

    nc = get_segment_number_of_candidates (c, i);
    printf ("%d " ,nc);
    printf ("%s ", get_segment_converted (c, i));
    printf ("%s\r\n", get_segment_yomi (c, i));
  }
  printf ("\r\n");
}

static int
do_convert (const char *line)
{
  char *p;
  struct context *c;
  int c_desc, r;

  c_desc = strtol (line+8, &p, 10);
  c = c_desc_to_context (c_desc);
  r = begin_conversion (c, p+1);
  if (r < 0)
    printf ("-ERR %d convert failed.\r\n", -r);
  else
    {
      int n = get_number_of_segments (c);
      output_segments (c, 0, 0, n);
    }

  fflush (stdout);
  return 0;
}

static int
do_get_candidates (const char *line)
{
  char *p;
  struct context *c;
  int c_desc, seg_num, cand_offset, max_cands;
  int nc, i, max;

  c_desc = strtol (line+15, &p, 10);
  seg_num = strtol (p+1, &p, 10);
  cand_offset = strtol (p+1, &p, 10);
  max_cands = strtol (p+1, &p, 10);

  c = c_desc_to_context (c_desc);
  nc = get_segment_number_of_candidates (c, seg_num);

  max = cand_offset + max_cands;
  if (nc < cand_offset + max_cands)
    max = nc;

  printf ("+DATA %d %d\r\n", cand_offset, max);
  for (i = cand_offset; i < max; i++)
    printf ("%s\r\n", get_segment_candidate (c, seg_num, i));
  printf ("\r\n");

  fflush (stdout);
  return 0;
}

static int
do_new_context (const char *line)
{
  int r;

  /* XXX: Should check arguments */
  if (strncmp (" INPUT=#18 OUTPUT=#18", line+11, 20) != 0) {
    printf ("-ERR %d unsupported context\r\n", ERROR_CODE_UNSUPPOTED);
    return 1;
  }

  r = new_context ();
  if (r < 0)
    printf ("-ERR %d new context failed.\r\n", -r);
  else
    printf ("+OK %d\r\n", r);

  fflush (stdout);
  return 0;
}

static int
do_release_context (const char *line)
{
  int c_desc;
  int r;
  char *p;

  c_desc = strtol (line+15, &p, 10);
  r = release_context (c_desc);
  if (r < 0)
    printf ("-ERR %d release context failed.\r\n", -r);
  else
    printf ("+OK\r\n");

  fflush (stdout);
  return 0;
}

static int
do_resize_segment (const char *line)
{
  char *p;
  struct context *c;
  int c_desc, seg_num, inc_dec, r;

  c_desc = strtol (line+15, &p, 10);
  seg_num = strtol (p+1, &p, 10);
  inc_dec= strtol (p+1, &p, 10);
  c = c_desc_to_context (c_desc);
  r = resize_segment (c, seg_num, inc_dec);

  if (r < 0)
    printf ("-ERR %d resize failed.\r\n", -r);
  else {
    int removed, inserted;

    seg_num = r;
    removed = get_number_of_segments_removed (c, seg_num);
    inserted = get_number_of_segments_inserted (c, seg_num);

    output_segments (c, seg_num, removed, inserted);
  }

  fflush (stdout);
  return 0;
}

static int
do_select_candidate (const char *line)
{
  char *p;
  struct context *c;
  int c_desc, seg_num, cand_num, r;

  c_desc = strtol (line+17, &p, 10);
  seg_num = strtol (p+1, &p, 10);
  cand_num = strtol (p+1, &p, 10);
  c = c_desc_to_context (c_desc);
  r = select_candidate (c, seg_num, cand_num);

  if (r < 0)
    printf ("-ERR %d select failed.\r\n", -r);
  else {
    int removed;

    seg_num = r;
    removed = get_number_of_segments_removed (c, seg_num);

    if (removed == 0)
      printf ("+OK\r\n");
    else {
      int inserted = get_number_of_segments_inserted (c, seg_num);

      output_segments (c, seg_num, removed, inserted);
    }
  }

  fflush (stdout);
  return 0;
}

static int
do_quit (const char *line)
{
  (void)line;
  return 1;
}

struct dispatch_table {
  const char *command;
  int size;
  int (*func)(const char *line);
};

static struct dispatch_table dt[] = {
  { "COMMIT", 6, do_commit },
  { "CONVERT", 7, do_convert },
  { "GET-CANDIDATES", 14, do_get_candidates },
  { "NEW-CONTEXT", 11, do_new_context },
  { "QUIT", 4, do_quit },
  { "RELEASE-CONTEXT", 15, do_release_context },
  { "RESIZE-SEGMENT", 14, do_resize_segment },
  { "SELECT-CANDIDATE", 16, do_select_candidate },
};

static int
dt_cmp (const char *line, struct dispatch_table *d)
{
  return strncmp (line, d->command, d->size);
}

#define MAX_LINE 512
static char line[MAX_LINE];

void egg_main (void);

void
egg_main (void)
{
  int done = 0;
  char *s, *p;

  say_hello ();

  while (!done) {
    struct dispatch_table *d;

    s = fgets (line, MAX_LINE, stdin);
    if (s == NULL) {
      fprintf (stderr, "null input\n");
      break;
    }
    if ((p = (char *)memchr(s, '\n', MAX_LINE)) == NULL) {
      fprintf (stderr, "no newline\n");
      break;
    }
    if (p > s && *(p-1) == '\r')
      *(p-1) = '\0';
    else
      *p = '\0';
    d = (struct dispatch_table *)
      bsearch (s, dt,
	       sizeof (dt) / sizeof (struct dispatch_table),
	       sizeof (struct dispatch_table),
	       (int (*)(const void *, const void *))dt_cmp);
    if (d != NULL)
      done = d->func (s);
    else
      say_unknown ();
  }
}
