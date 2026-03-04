/* リリース前のチェックを行う */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <anthy/anthy.h>
#include <anthy/xstr.h>

static int
init(void)
{
  int res;

  anthy_conf_override("CONFFILE", "../anthy-unicode.conf");
  anthy_conf_override("HOME", TEST_HOME);
  anthy_conf_override("DIC_FILE", "../mkanthydic/anthy.dic");
  res = anthy_init();
  if (res) {
    printf("failed to init\n");
    return 1;
  }
  anthy_quit();
  /* init again */
  anthy_conf_override("CONFFILE", "../anthy-unicode.conf");
  anthy_conf_override("HOME", TEST_HOME);
  anthy_conf_override("DIC_FILE", "../mkanthydic/anthy.dic");
  res = anthy_init();
  if (res) {
    printf("failed to init\n");
    return 1;
  }
  return 0;
}

static int
test0(void)
{
  anthy_context_t ac;
  ac = anthy_create_context();
  if (!ac) {
    printf("failed to create context\n");
    return 1;
  }
  anthy_release_context(ac);
  return 0;
}

static int
test1(void)
{
  anthy_context_t ac;
  char buf[100];
  xstr *xs, *xs2;
  ac = anthy_create_context();
  if (!ac) {
    printf("failed to create context\n");
    return 1;
  }
  anthy_context_set_encoding (ac, ANTHY_UTF8_ENCODING);
  anthy_xstr_set_print_encoding (ANTHY_UTF8_ENCODING);
  anthy_set_string(ac, "あいうえお、かきくけこ。");
  if (anthy_get_segment(ac, 0, NTH_UNCONVERTED_CANDIDATE, buf, 100) > 0) {
    printf("(%s)\n", buf);
  }
  if (anthy_get_segment(ac, 0, NTH_KATAKANA_CANDIDATE, buf, 100) > 0) {
    printf("(%s)\n", buf);
  }
  if (anthy_get_segment(ac, 0, NTH_HIRAGANA_CANDIDATE, buf, 100) > 0) {
    printf("(%s)\n", buf);
  }
  if (anthy_get_segment(ac, 0, NTH_HALFKANA_CANDIDATE, buf, 100) > 0) {
    printf("(%s)\n", buf);
  }
  anthy_release_context(ac);
  xs = anthy_cstr_to_xstr("あいうえおがぎぐげご", ANTHY_UTF8_ENCODING);
  xs2 = anthy_xstr_hira_to_half_kata(xs);
  anthy_putxstrln(xs2);
  anthy_free_xstr(xs);
  anthy_free_xstr(xs2);
  return 0;
}

/* compliant_rand:
 * dont_call: "rand" should not be used for security-related applications,
 * because linear congruential algorithms are too easy to break
 * but we don't need the strict randoms here.
 */
static long int
compliant_rand(void)
{
  struct timespec ts = { 0, };
  if (!timespec_get (&ts, TIME_UTC)) {
    printf("Failed timespec_get\n");
    assert(0);
  }
  return ts.tv_nsec;
}

static int
shake_test(const char *str)
{
  int i;
  anthy_context_t ac;
  ac = anthy_create_context();
  if (!ac) {
    printf("failed to create context\n");
    return 1;
  }
  anthy_context_set_encoding(ac, ANTHY_UTF8_ENCODING);
  anthy_set_string(ac, str);
  for (i = 0; i < 50; i++) {
    int nth, rsz;
    struct anthy_conv_stat cs;
    anthy_get_stat(ac, &cs);
    nth = compliant_rand() % cs.nr_segment;
    rsz = (compliant_rand() % 3) - 1;
    anthy_resize_segment(ac, nth, rsz);
  }
  anthy_release_context(ac);
  return 0;
}

int
main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  printf("checking\n");
  if (init()) {
    printf("fail (init)\n");
    return 0;
  }
  if (test0()) {
    printf("fail (test0)\n");
  }
  if (test1()) {
    printf("fail (test1)\n");
  }
  if (shake_test("あいうえおかきくけこ")) {
    printf("fail (shake_test)\n");
  }
  printf("done\n");
  return 0;
}
