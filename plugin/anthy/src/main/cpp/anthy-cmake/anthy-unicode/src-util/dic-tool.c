/*
 * 辞書操作用のユーティリティコマンド
 *
 * 辞書のライブラリ内部の形式と外部の形式の相互変換を行う
 * 外部形式は
 * *読み 頻度 単語
 * *品詞の変数1 = 値1
 * *品詞の変数2 = 値2
 * *...
 * *<空行>
 * になる
 */
/*
 * Funded by IPA未踏ソフトウェア創造事業 2001 9/22
 *
 * Copyright (C) 2000-2007 TABATA Yusuke
 * Copyright (C) 2020-2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 */
/*
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <anthy/anthy.h>
#include <anthy/diclib.h>
#include <anthy/dicutil.h>
#include <anthy/logger.h>
/**/
#include <anthy/textdict.h>
#include <anthy/xstr.h>
#include "config.h"

#define UNSPEC 0
#define DUMP_DIC 1
#define LOAD_DIC 2
#define APPEND_DIC 3
#define MIGRATE_DIC 4
#define TEXT_DIC 4

#define TYPETAB "typetab"
#define USAGE_TEXT "dic-tool-usage.txt"

#define USAGE \
 "Anthy-dic-util [options]\n"\
 " --help: Show this usage text\n"\
 " --version: Show version\n"\
 " --dump: Dump dictionary\n"\
 " --load: Load dictionary\n"\
 " --append: Append dictionary\n"\
 " --migrate: Migrate Anthy text dictionary to Anthy-Unicode text dictionary\n"\
 " --eucjp: Use EUC-JP encoding\n"\
 " --personality=NAME: use NAME as a name of personality\n"\
 " --text: Use text dictionary with --append or --load option\n"\
 " --src=FILE: Use FILE as a source file\n"\
 " --dest=FILE: Use FILE as a destination file\n"\
 ""


struct textdict {
  char *fn;
  char *ptr;
  struct filemapping *mapping;
};

extern struct textdict *anthy_private_text_dic;
extern struct textdict *old_anthy_private_text_dic;

static int command = UNSPEC;
static int encoding = ANTHY_UTF8_ENCODING;
static FILE *fp_in;
static const char *src;
static const char *dest;
static const char *personality = "";
static int use_text_dic = FALSE;

/* 変数名と値のペア */
struct var{
  struct var *next;
  char *var_name;
  char *val;
};

/* 品詞のパラメータから品詞名を得るためのテーブル */
struct trans_tab {
  struct trans_tab *next;
  char *type_name; /* 内部での型の名前 T35とか */
  struct var var_list; /* 型を決定するためのパラメータ */
}trans_tab_list;

struct dict_entry {
  char              *yomi;
  char              *word;
  char              *wtype;
  int                freq;
  struct dict_entry *next;
};

static void
print_usage(void)
{
  printf(USAGE);
  exit(0);
}

static FILE *
open_typetab(void)
{
  FILE *fp;
  char *fn, *tmp;
  fp = fopen(TYPETAB, "r");
  if (fp) {
    return fp;
  }
  if (!(fn = strdup(anthy_dic_util_get_anthydir()))) {
    anthy_log(0, "Could not find ANTHYDIR in conf file.\n");
    return NULL;
  }
  if (!(tmp = realloc(fn, strlen(fn) + strlen(TYPETAB) + 4))) {
    anthy_log(0, "Could not realloc TYPETAB.\n");
    /* free(fn) is freed twice. */
    return NULL;
  }
  fn = tmp;
  strcat(fn, "/");
  strcat(fn, TYPETAB);
  fp = fopen(fn, "r");
  free(fn);
  return fp;
}

static FILE *
open_usage_file(void)
{
  FILE *fp;
  /* カレントディレクトリにある場合は、それを使用する */
  fp = fopen(USAGE_TEXT, "r");
  if (!fp) {
    /* インストールされたものを使用 */
    char *fn = NULL, *tmp;
    if (!(fn = strdup(anthy_dic_util_get_anthydir()))) {
      anthy_log(0, "Could not find ANTHYDIR in conf file.\n");
      return NULL;
    }
    if (!(tmp = realloc(fn, strlen(fn) + strlen(USAGE_TEXT) + 10))) {
      anthy_log(0, "Could not realloc USAGE_TEXT.\n");
      /* CPPCHECK_WARNING and CLANG_WARNING are conflicted.
       * CPPCHECK_WARNING reports: Common realloc mistake:
       *     'fn' nulled but not freed upon failure
       * also CLANG_WARNING reports: Potential leak of memory ponted to by 'fn'
       * On the other hand,
       * CLANG_WARNING reports: 'fn' is freed twice.
       */
      free(fn);
      return NULL;
    }
    fn = tmp;
    strcat(fn, "/" USAGE_TEXT);
    fp = fopen(fn, "r");
    free(fn);
  }
  return fp;
}

static void
print_usage_text(void)
{
  char buf[256];
  FILE *fp = open_usage_file();
  if (!fp) {
    printf("# Anthy-dic-tool\n#\n");
    return ;
  }
  fprintf(stdout, "#" PACKAGE " " VERSION "\n");
  if (encoding == ANTHY_UTF8_ENCODING) {
  } else {
  }
  /* そのままファイルの内容を出力 */
  while (fgets(buf, 256, fp)) {
    if (encoding == ANTHY_EUC_JP_ENCODING) {
      char *s;
      s = anthy_conv_utf8_to_euc(buf);
      printf("%s", s);
      free(s);
    } else {
      printf("%s", buf);
    }
  }
  fclose(fp);
}

static void
backup_file (const char *path, int do_rename)
{
  FILE *in = NULL;
  FILE *out = NULL;
  time_t t;
  struct tm *tmp;
  size_t nread;
  char *backup = NULL;
  char ext[20];
  char buf[BUFSIZ];

  if (!(in = fopen (path, "r")))
    return;
  t = time (NULL);
  if ((tmp = localtime (&t)) == NULL) {
    perror ("localtime");
    exit (EXIT_FAILURE);
  }
  if (!strftime (ext, sizeof (ext), "%Y%m%d%H%M%S", tmp)) {
    fprintf (stderr, "strftime error");
    exit (EXIT_FAILURE);
  }
  backup = malloc (strlen (path) + strlen (ext) + 2);
  sprintf (backup, "%s.%s", path, ext);
  if (do_rename) {
    fclose (in);
    /* Should not exit with the source file copy */
    if (rename (path, backup))
      fprintf (stderr, "Fail to rename %s: %s\n", path, strerror (errno));
    free (backup);
    return;
  } else if (!(out = fopen (backup, "w")))  {
    fprintf (stderr, "Fail to write %s", backup);
    fclose (in);
    free (backup);
    exit (EXIT_FAILURE);
  }
  while ((nread = fread (buf, 1, sizeof buf, in)) > 0) {
    if (fwrite (buf, 1, nread, out) < nread) {
      fprintf (stderr, "Fail to copy %s: %s\n", path, strerror (errno));
      fclose (out);
      fclose (in);
      free (backup);
      exit (EXIT_FAILURE);
    }
  }
  fclose (out);
  fclose (in);
  free (backup);
}

static char *
read_line(char *buf, int len, FILE *fp)
{
  while (fgets(buf, len, fp)) {
    if (buf[0] != '#') {
      /* 改行を削除する */
      int l = strlen(buf);
      if (l > 0 && buf[l-1] == '\n') {
	buf[l-1] = 0;
      }
      if (l > 1 && buf[l-2] == '\r') {
	buf[l-1] = 0;
      }
      /**/
      return buf;
    }
  }
  return NULL;
}

static int
read_typetab_var(struct var *head, FILE *fp, int table)
{
  char buf[256];
  char var[256], eq[256], val[256];
  struct var *v;
  if (!read_line(buf, 256, fp)) {
    return -1;
  }
  if (sscanf(buf, "%s %s %s", var, eq, val) != 3) {
    return -1;
  }

  if (!(v = malloc(sizeof(struct var)))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    return -1;
  }
  if (encoding == ANTHY_EUC_JP_ENCODING && table) {
    /* UTF-8 */
    v->var_name = anthy_conv_utf8_to_euc(var);
    v->val = anthy_conv_utf8_to_euc(val);
  } else {
    /* do not change */
    v->var_name = strdup(var);
    v->val = strdup(val);
  }

  /* リストにつなぐ */
  v->next = head->next;
  head->next = v;

  return 0;
}

static void
free_typetab_var (struct var *head)
{
  struct var *v = head;
  while (v) {
    struct var *prev;
    free (v->var_name);
    free (v->val);
    prev = v;
    v = v->next;
    /* head is not allocated */
    if (prev != head)
      free (prev);
  }
}

static int
read_typetab_entry(FILE *fp)
{
  char buf[256], type_name[257];
  char *res;
  struct trans_tab *t;
  /* 一行目の品詞名を読む */
  do {
    res = read_line(buf, 256, fp);
    if (!res) {
      return -1;
    }
  } while (res[0] == '#' || res[0] == 0);
  t = malloc(sizeof(struct trans_tab));
  sprintf(type_name, "#%s", buf);
  t->type_name = strdup(type_name);
  t->var_list.next = 0;
  /* パラメータを読む */
  while(!read_typetab_var(&t->var_list, fp, 1));
  /* リストにつなぐ */
  t->next = trans_tab_list.next;
  trans_tab_list.next = t;
  return 0;
}

static void
read_typetab(void)
{
  FILE *fp = open_typetab();
  if (!fp) {
    printf("Failed to open type table.\n");
    exit(1);
  }
  while (!read_typetab_entry(fp));
  fclose(fp);
}

static struct trans_tab *
find_trans_tab_by_name(char *name)
{
  struct trans_tab *t;
  for (t = trans_tab_list.next; t; t = t->next) {
    if (!strcmp(t->type_name, name)) {
      return t;
    }
  }
  return NULL;
}

static void
print_word_type(struct trans_tab *t)
{
  struct var *v;
  for (v = t->var_list.next; v; v = v->next) {
    printf("%s\t=\t%s\n", v->var_name, v->val);
  }
}

static void
dump_dic(void)
{
  print_usage_text();
  if (anthy_priv_dic_select_first_entry() == -1) {
    printf("# Failed to read private dictionary\n"
	   "# There are no words or error occurred?\n"
	   "#\n");
    return ;
  }
  do {
    char idx[100], wt[100], w[100];
    int freq;
    if (anthy_priv_dic_get_index(idx, 100) &&
	anthy_priv_dic_get_wtype(wt, 100) &&
	anthy_priv_dic_get_word(w, 100)) {
      struct trans_tab *t;
      freq = anthy_priv_dic_get_freq();
      t = find_trans_tab_by_name(wt);
      if (t) {
	printf("%s %d %s\n", idx, freq, w);
	print_word_type(t);
	printf("\n");
      } else {
	printf("# Failed to determine word type of %s(%s).\n", w, wt);
      }
    }
  } while (anthy_priv_dic_select_next_entry() == 0);
}

static void
open_input_file(void)
{
  if (use_text_dic)
    return;
  if (!src) {
    fp_in = stdin;
  } else {
    fp_in = fopen (src, "r");
    if (!fp_in) {
      exit (EXIT_FAILURE);
    }
  }
}

/* vが sの中にあるか */
static int
match_var(struct var *v, struct var *s)
{
  struct var *i;
  for (i = s->next; i; i = i->next) {
    if (!strcmp(v->var_name, i->var_name) &&
	!strcmp(v->val, i->val)) {
      return 1;
    }
  }
  return 0;
}

/* v1がv2の部分集合かどうか */
static int
var_list_subset_p(struct var *v1, struct var *v2)
{
  struct var *v;
  for (v = v1->next; v; v = v->next) {
    if (!match_var(v, v2)) {
      return 0;
    }
  }
  return 1;
}

static char *
find_wt(void)
{
  struct var v;
  struct trans_tab *t;
  v.next = 0;
  memset(&v, 0, sizeof(struct var));
  while(!read_typetab_var(&v, fp_in, 0));
  for (t = trans_tab_list.next; t; t = t->next) {
    if (var_list_subset_p(&t->var_list, &v) &&
        var_list_subset_p(&v, &t->var_list)) {
      free_typetab_var(&v);
      return t->type_name;
    }
  }
  free_typetab_var(&v);
  return NULL;
}

static int
find_head(char *yomi, char *freq, char *w)
{
  char buf[256];
  do {
    if (!read_line(buf, 256, fp_in)) {
      return -1;
    }
  } while (sscanf(buf, "%s %s %[^\n]",yomi, freq, w) != 3);
  return 0;
}

static void
load_tt_dic (void)
{
  char yomi[256], freq[256], w[256];
  int has_entry = FALSE;
  while (!find_head(yomi, freq, w)) {
    char *wt = find_wt();
    if (wt) {
      int ret;
      ret = anthy_priv_dic_add_entry(yomi, w, wt, atoi(freq));
      if (ret == -1) {
        printf("Failed to register %s\n", yomi);
      }else {
        has_entry = TRUE;
        printf("Word %s is registered as %s\n", yomi, wt);
      }
    } else {
      printf("Failed to find the type of %s.\n", yomi);
    }
  }
  if (has_entry && (command == MIGRATE_DIC)) {
    backup_file (anthy_private_text_dic->fn, FALSE);
    if (src)
      backup_file (src, TRUE);
  }
}

static void
load_text_dic (void)
{
#define LINE_SIZE 1024
  struct textdict *orig_new_dic = anthy_private_text_dic;
  struct textdict *src_dic = NULL;
  struct textdict *dest_dic = NULL;
  static char buf[LINE_SIZE + 1];
  char *yomi = NULL;
  struct dict_entry *dict_head = NULL;
  struct dict_entry *d, *p;

  if ((command != MIGRATE_DIC) && !src) {
    fprintf (stderr, "Do not support the stdin dictionary with text format. " \
                     "Please append \"--src=FILE\" option in the command "    \
                     "arguments.");
    return;
  }
  src_dic = src ? anthy_textdict_open (src)
            : anthy_textdict_open (old_anthy_private_text_dic->fn);
  anthy_private_text_dic = src_dic;
  if (anthy_priv_dic_select_first_entry () == ANTHY_DIC_UTIL_ERROR) {
    anthy_textdict_close (src_dic);
    anthy_private_text_dic = orig_new_dic;
    fprintf (stdout, "No update dict file\n");
    return;
  }
  do {
    if (!(yomi = strdup (anthy_priv_dic_get_index (buf, LINE_SIZE)))) {
      anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
      continue;
    }
    if (*yomi == '#') {
      free (yomi);
      continue;
    }
    if (!dict_head) {
      if (!(d = dict_head = calloc (sizeof (struct dict_entry), 1))) {
        anthy_log(0, "Failed calloc in %s:%d\n", __FILE__, __LINE__);
        free(yomi);
        break;
      }
    } else {
      if (!(d->next = calloc (sizeof (struct dict_entry), 1))) {
        anthy_log(0, "Failed calloc in %s:%d\n", __FILE__, __LINE__);
        free(yomi);
        break;
      }
      d = d->next;
    }
    assert(d);
    d->yomi = yomi;
    d->word = strdup (anthy_priv_dic_get_word (buf, LINE_SIZE));
    d->wtype = strdup (anthy_priv_dic_get_wtype (buf, LINE_SIZE));
    d->freq = anthy_priv_dic_get_freq ();
  } while (anthy_priv_dic_select_next_entry () != ANTHY_DIC_UTIL_ERROR);
  dest_dic = dest ? anthy_textdict_open (dest)
            : anthy_textdict_open (orig_new_dic->fn);
  anthy_private_text_dic = dest_dic;
  if (dict_head) {
    if (command == MIGRATE_DIC) {
      backup_file (dest_dic->fn, FALSE);
      backup_file (src_dic->fn, TRUE);
    }
  } else {
    anthy_textdict_close (src_dic);
    anthy_textdict_close (dest_dic);
    anthy_private_text_dic = orig_new_dic;
    fprintf (stdout, "No update dict file\n");
    return;
  }
  anthy_textdict_close (src_dic);
  for (d = dict_head; d;) {
    int ret = anthy_priv_dic_add_entry(d->yomi, d->word, d->wtype, d->freq);
    if (ret == -1)
      fprintf(stderr, "Failed to register %s %s\n", d->yomi, d->word);
    free (d->yomi);
    free (d->word);
    free (d->wtype);
    p = d->next;
    free (d);
    d = p;
  }
  fprintf (stdout, "Update dict file %s\n", dest_dic->fn);
  anthy_textdict_close (dest_dic);
  anthy_private_text_dic = orig_new_dic;
#undef LINE_SIZE
}

static void
load_dic(void)
{
  if (use_text_dic)
    load_text_dic ();
  else
    load_tt_dic ();
}

static void
print_version(void)
{
  printf("Anthy-dic-util "VERSION".\n");
  exit(0);
}

static void
parse_args(int argc, char **argv)
{
  int i;
  for (i = 1 ; i < argc ; i++) {
    if (!strncmp(argv[i], "--", 2)) {
      char *opt = &argv[i][2];
      if (!strcmp(opt, "help")) {
        print_usage ();
      } else if (!strcmp(opt, "version")){
        print_version ();
      } else if (!strcmp(opt, "dump")) {
        command = DUMP_DIC;
      } else if (!strcmp(opt,"append") ){
        command = APPEND_DIC;
      } else if (!strncmp(opt, "personality=", 12)) {
        personality = &opt[12];
      } else if (!strcmp(opt, "utf8")) {
        encoding = ANTHY_UTF8_ENCODING;
      } else if (!strcmp(opt, "eucjp")) {
        encoding = ANTHY_EUC_JP_ENCODING;
      } else if (!strcmp(opt, "load")) {
        command = LOAD_DIC;
      } else if (!strcmp(opt,"migrate") ){
        command = MIGRATE_DIC;
        /* tt_dic migration is not supported. */
        use_text_dic = TRUE;
      } else if (!strcmp(opt,"text") ){
        use_text_dic = TRUE;
      } else if (!strncmp(opt, "src=", 4)) {
        src = &opt[4];
      } else if (!strncmp(opt, "dest=", 5)) {
        dest = &opt[5];
      }
    } else {
      src = argv[i];
    }
  }
}

static void
init_lib(void)
{
  anthy_dic_util_init();
  anthy_dic_util_set_encoding(encoding);
  read_typetab();
}

int
main(int argc,char **argv)
{
  fp_in = stdin;
  parse_args (argc, argv);

  switch (command) {
  case DUMP_DIC:
    init_lib ();
    dump_dic ();
    break;
  case LOAD_DIC:
    init_lib ();
    anthy_priv_dic_delete ();
    open_input_file ();
    load_dic ();
    break;
  case APPEND_DIC:
    init_lib ();
    open_input_file ();
    load_dic ();
    break;
  case MIGRATE_DIC:
    init_lib ();
    load_dic ();
    break;
  case UNSPEC:
  default:
    print_usage ();
  }
  return 0;
}
