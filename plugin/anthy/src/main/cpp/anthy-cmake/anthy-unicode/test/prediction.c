#include <stdlib.h>
#include <stdio.h>
#include <anthy/anthy.h>


/* Makefile の $(srcdir) (静的データファイルの基準ディレクトリ) */
#ifndef SRCDIR
# define SRCDIR "."
#endif

static void
init_lib(void)
{
  /* 既にインストールされているファイルの影響を受けないようにする */
  anthy_conf_override("CONFFILE", "../anthy-unicode.conf");
  anthy_conf_override("DEPWORD", "master.depword");
  anthy_conf_override("DEPGRAPH", "../depgraph/anthy.dep");
  anthy_conf_override("DIC_FILE", "../mkanthydic/anthy.dic");
  anthy_conf_override("ANTHYDIR", SRCDIR "/../depgraph");
  if (anthy_init()) {
    printf("failed to init anthy\n");
    exit(0);
  }
}


int main(int argc, char** argv)
{
  struct anthy_prediction_stat ps;
  anthy_context_t ac;
  int i;

  if (argc == 1) {
    return 0;
  }
  init_lib();
  ac = anthy_create_context();

  anthy_set_prediction_string(ac, argv[1]);
  anthy_get_prediction_stat(ac, &ps);
  for (i = 0; i < ps.nr_prediction; ++i) {
    char* buf;
    int len;
    len = anthy_get_prediction(ac, i, NULL, 0);
    buf = malloc(sizeof(char) * (len + 1));
    len = anthy_get_prediction(ac, i, buf, len + 1);
    printf("%s, %d\n", buf, len);
    free(buf);
  }
  anthy_commit_prediction(ac, 0);

  anthy_release_context(ac);

  anthy_quit();

  return 0;
}
