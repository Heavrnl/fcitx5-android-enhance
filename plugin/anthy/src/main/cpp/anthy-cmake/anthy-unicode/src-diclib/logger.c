/*
 * ログの出力
 * $Id: logger.c,v 1.8 2002/05/14 13:24:47 yusuke Exp $
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <anthy/anthy.h>
#include <anthy/logger.h>

static void (*logger)(int lv, const char *str);
static int current_level = 1;
static int buffer_size = 256;
char *log_buffer = NULL;

void
anthy_do_set_logger(void (*fn)(int lv, const char *str), int lv)
{
  current_level = lv;
  logger = fn;
  if (logger != NULL && log_buffer == NULL) {
    log_buffer = malloc(sizeof(char) * buffer_size);
  } else if (logger == NULL && log_buffer != NULL) {
    free(log_buffer);
    log_buffer = NULL;
  }
}

static void
do_log(int lv, const char *str, va_list arg)
{
  if (lv < current_level) {
    return ;
  }
  if (logger) {
    vsnprintf(log_buffer, 256, str, arg);
    logger(lv, log_buffer);
  } else {
    fprintf(stderr, "Anthy: ");
    vfprintf(stderr, str, arg);
  }
}

void
anthy_log(int lv, const char *str, ...)
{
  va_list arg;
  if (lv > current_level) {
    return ;
  }
  va_start(arg, str);
  do_log(lv, str, arg);
  va_end(arg);
}

void
anthy_set_logger(anthy_logger lg, int level)
{
  anthy_do_set_logger(lg, level);
}

const char *
anthy_strerror (int errnum)
{
  const char *msg;
  static char buf[1024];

#if defined(HAVE_STRERROR_R)
#  if defined(__GLIBC__) && !((_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && ! _GNU_SOURCE)
      msg = strerror_r (errnum, buf, sizeof (buf));
#  else
      strerror_r (errnum, buf, sizeof (buf));
      msg = buf;
#  endif /* HAVE_STRERROR_R */
#else
      strncpy (buf, strerror (errnum), sizeof (buf));
      buf[sizeof (buf) - 1] = '\0';
      msg = buf;
#endif
  return msg;
}
