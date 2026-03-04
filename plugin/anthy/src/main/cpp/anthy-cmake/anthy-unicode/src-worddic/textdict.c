/*
 * ソートされたテキストから検索を行う
 *
 * Copyright (C) 2021 Takao Fujiwara <takao.fujiwara1@gmail.com>
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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <anthy/filemap.h>
#include <anthy/logger.h>
#include <anthy/textdict.h>
#include "dic_main.h"

struct textdict {
  char *fn;
  char *ptr;
  struct filemapping *mapping;
};

struct textdict *
anthy_textdict_open(const char *fn)
{
  struct textdict *td = malloc(sizeof(struct textdict));
  if (!td) {
    return NULL;
  }
  td->fn = strdup(fn);
  if (!td->fn) {
    free(td);
    return NULL;
  }
  td->mapping = NULL;
  return td;
}


static void
unmap(struct textdict *td)
{
  if (td->mapping) {
    anthy_munmap(td->mapping);
    td->mapping = NULL;
  }
}

void
anthy_textdict_close(struct textdict *td)
{
  if (!td) {
    return ;
  }
  unmap(td);
  free(td->fn);
  free(td);
}

static int
update_mapping(struct textdict *td)
{
  if (td->mapping) {
    anthy_munmap(td->mapping);
  }
  td->mapping = anthy_mmap(td->fn, 1);
  if (!td->mapping) {
    td->ptr = NULL;
    return 1;
  }
  td->ptr = anthy_mmap_address(td->mapping);
  return 0;
}

static int
expand_file(struct textdict *td, int len)
{
  FILE *fp;
  char buf[256];
  int c;
  fp = fopen(td->fn, "a+");
  if (!fp) {
    return -1;
  }
  memset(buf, '\n', 256);
  c = 1;
  if (len > 256) {
    c *= fwrite(buf, 256, len / 256, fp);
  }
  if (len % 256) {
    c *= fwrite(buf, len % 256, 1, fp);
  }
  fclose(fp);
  if (c == 0) {
    return -1;
  }
  return 0;
}

void
anthy_textdict_scan(struct textdict *td, int offset, void *ptr,
		    int (*fun)(void *, int, const char *, const char *))
{
  FILE *fp;
  const char *old_path;
  char buf[1024];
  if (!td) {
    return ;
  }
  fp = fopen(td->fn, "r");
  if (!fp) {
    return ;
  }
  old_path = anthy_get_user_dir (1);
  if (!strncmp (td->fn, old_path, strlen (old_path))) {
    anthy_log (
      0,
      "WARNING: Please move the old dict file %s to the new directory %s\n",
      td->fn,
      anthy_get_user_dir (0));
  }
  if (fseek(fp, offset, SEEK_SET)) {
    fclose(fp);
    return ;
  }
  while (fgets(buf, 1024, fp)) {
    char *p = strchr(buf, ' ');
    int len, r;
    len = strlen(buf);
    offset += len;
    buf[len - 1] = 0;
    if (!p) {
      continue;
    }
    *p = 0;
    p++;
    while (*p == ' ') {
      p++;
    }
    /* call it */
    r = fun(ptr, offset, buf, p);
    if (r) {
      break;
    }
  }
  fclose(fp);
}

int
anthy_textdict_delete_line(struct textdict *td, int offset)
{
  FILE *fp;
  char buf[1024];
  int len, size;
  fp = fopen(td->fn, "r");
  if (!fp) {
    return -1;
  }
  if (fseek(fp, offset, SEEK_SET)) {
    fclose(fp);
    return -1;
  }
  if (!fgets(buf, 1024, fp)) {
    fclose(fp);
    return -1;
  }
  len = strlen(buf);
  fclose(fp);
  if (update_mapping(td))
    return -1;
  /* anthy_mmap() should make td->ptr if td->mapping is not null
   * in update_mapping().
   */
  assert(td->ptr);
  size = anthy_mmap_size(td->mapping);
  memmove(&td->ptr[offset], &td->ptr[offset+len], size - offset - len);
  unmap(td);
  if (size - len == 0) {
    unlink(td->fn);
    return 0;
  }
  errno = 0;
  if (truncate(td->fn, size - len)) {
    anthy_log(0, "Failed truncate in %s:%d: %s\n",
              __FILE__, __LINE__, strerror(errno));
  }
  return 0;
}

int
anthy_textdict_insert_line(struct textdict *td, int offset,
			   const char *line)
{
  int len = strlen(line);
  int size;
  if (!td)
    return -1;
  if (expand_file(td, len))
    return -1;
  if (update_mapping(td))
    return -1;
  /* anthy_mmap() should make td->ptr if td->mapping is not null
   * in update_mapping().
   */
  assert(td->ptr);
  size = anthy_mmap_size(td->mapping);
  memmove(&td->ptr[offset+len], &td->ptr[offset], size - offset - len);
  memcpy(&td->ptr[offset], line, len);
  return 0;
}
