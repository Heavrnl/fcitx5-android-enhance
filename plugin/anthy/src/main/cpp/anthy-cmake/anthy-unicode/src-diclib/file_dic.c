/* Copyright 2000 Red Hat, Inc.
 * Copyright 2020 Takao Fujiwara
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <anthy/diclib.h>
#include <anthy/filemap.h>
#include <anthy/alloc.h>
#include <anthy/conf.h>
#include <anthy/logger.h>
#include "diclib_inner.h"

/**
   複数セクションがリンクされた辞書
 */
struct file_dic
{
  struct filemapping *mapping;
};

static struct file_dic fdic;

void*
anthy_file_dic_get_section(const char* section_name)
{
  int i;
  char* head = anthy_mmap_address(fdic.mapping);
  int* p = (int*)head;
  int entry_num = anthy_dic_ntohl(*p++);

  for (i = 0; i < entry_num; ++i) {
    int hash_offset = anthy_dic_ntohl(*p++);
    int key_len =  anthy_dic_ntohl(*p++);
    int contents_offset = anthy_dic_ntohl(*p++);
    if (strncmp(section_name, head + hash_offset, key_len) == 0) {
      return (void*)(head + contents_offset);
    }
  }
  return NULL;
}

int
anthy_init_file_dic(void)
{
  const char *fn;
  fn = anthy_conf_get_str("DIC_FILE");
  if (!fn) {
    anthy_log(0, "dictionary is not specified.\n");
    return -1;
  }

  /* 辞書をメモリ上にmapする */
  fdic.mapping = anthy_mmap(fn, 0);
  if (!fdic.mapping) {
    anthy_log(0, "failed to init file dic.\n");
    return -1;
  }

  return 0;
}

void
anthy_quit_file_dic(void)
{
  anthy_munmap(fdic.mapping);
}


static int
anthy_path_is_absolute (const char *file_name)
{
  if (file_name == NULL) {
    anthy_log (0, "file_name is NULL.\n");
    return FALSE;
  }

  if (ANTHY_IS_DIR_SEPARATOR (file_name[0]))
    return TRUE;

  return FALSE;
}


static const char *
anthy_path_skip_root (const char *file_name)
{
  if (file_name == NULL) {
    anthy_log (0, "file_name is NULL.\n");
    return NULL;
  }

  if (ANTHY_IS_DIR_SEPARATOR (file_name[0]))
    {
      while (ANTHY_IS_DIR_SEPARATOR (file_name[0]))
        file_name++;
      return file_name;
    }
  return NULL;
}


/* anthy_file_test:
 * Copy from g_file_test()
 */
int
anthy_file_test (const char   *filename,
                 AnthyFileTest test)
{
  if ((test & ANTHY_FILE_TEST_EXISTS) && (access (filename, F_OK) == 0))
    return TRUE;

  if ((test & ANTHY_FILE_TEST_IS_EXECUTABLE)
      && (access (filename, X_OK) == 0)) {
    if (getuid () != 0)
      return TRUE;
  } else {
    test &= ~ANTHY_FILE_TEST_IS_EXECUTABLE;
  }

  if (test & ANTHY_FILE_TEST_IS_SYMLINK) {
    struct stat s;
    if ((lstat (filename, &s) == 0) && S_ISLNK (s.st_mode))
      return TRUE;
  }

  if (test & (ANTHY_FILE_TEST_IS_REGULAR |
              ANTHY_FILE_TEST_IS_DIR |
              ANTHY_FILE_TEST_IS_EXECUTABLE)) {
    struct stat s;

    if (stat (filename, &s) == 0) {
      if ((test & ANTHY_FILE_TEST_IS_REGULAR) && S_ISREG (s.st_mode))
        return TRUE;

      if ((test & ANTHY_FILE_TEST_IS_DIR) && S_ISDIR (s.st_mode))
        return TRUE;

      if ((test & ANTHY_FILE_TEST_IS_EXECUTABLE) &&
          ((s.st_mode & S_IXOTH) ||
          (s.st_mode & S_IXUSR) ||
          (s.st_mode & S_IXGRP)))
        return TRUE;
    }
  }
  return FALSE;
}


/* anthy_mkdir_with_parents:
 * Copy from g_mkdir_with_parents()
 */
int
anthy_mkdir_with_parents (const char *pathname,
                          int         mode)
{
  char *fn, *p;

  if (pathname == NULL || *pathname == '\0') {
    errno = EINVAL;
    return -1;
  }

  if (mkdir (pathname, mode) == 0) {
    return 0;
  } else if (errno == EEXIST) {
    if (!anthy_file_test (pathname, ANTHY_FILE_TEST_IS_DIR)) {
      errno = ENOTDIR;
      return -1;
    }
    return 0;
  }

  fn = strdup (pathname);

  if (anthy_path_is_absolute (fn))
    p = (char *) anthy_path_skip_root (fn);
  else
    p = fn;

  do {
    while (*p && !ANTHY_IS_DIR_SEPARATOR (*p))
      p++;

    if (!*p)
      p = NULL;
    else
      *p = '\0';

    if (!anthy_file_test (fn, ANTHY_FILE_TEST_EXISTS)) {
      if (mkdir (fn, mode) == -1 && errno != EEXIST) {
        int errno_save = errno;
        if (errno != ENOENT || !p) {
          free (fn);
          errno = errno_save;
          return -1;
        }
      }
    } else if (!anthy_file_test (fn, ANTHY_FILE_TEST_IS_DIR)) {
      free (fn);
      errno = ENOTDIR;
      return -1;
    }
    if (p) {
      *p++ = ANTHY_DIR_SEPARATOR;
      while (*p && ANTHY_IS_DIR_SEPARATOR (*p))
        p++;
    }
  } while (p);

  free (fn);
  return 0;
}
