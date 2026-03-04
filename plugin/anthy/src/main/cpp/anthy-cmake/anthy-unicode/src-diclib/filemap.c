/* mmapを抽象化する
 *
 * *Unix系のシステムコールをソース中に散らさないため
 * *将来的には一つのファイルを複数の目的にmapすることも考慮
 *
 * Copyright (C) 2005 TABATA Yusuke
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
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <anthy/filemap.h>
#include <anthy/logger.h>

struct filemapping {
  /* 書き込みモード */
  int wr;
  /* mmapしたアドレス */
  void *ptr;
  /* mmapした領域の長さ */
  size_t size;
};

struct filemapping *
anthy_mmap(const char *fn, int wr)
{
  int fd;
  void *ptr;
  int r;
  struct filemapping *m;
  struct stat st;
  int prot;
  int flags;
  mode_t mode;

  if (wr) {
    prot = PROT_READ | PROT_WRITE;
    flags = O_RDWR;
    mode = S_IRUSR | S_IWUSR;
  } else {
    prot = PROT_READ;
    flags = O_RDONLY;
    mode = S_IRUSR;
  }

  fd = open(fn, flags, mode);

  if (fd == -1) {
    anthy_log(0, "Failed to open (%s).\n", fn);
    return NULL;
  }

  r = fstat(fd, &st);
  if (r == -1) {
    anthy_log(0, "Failed to stat() (%s).\n", fn);
    close(fd);
    return NULL;
  }
  if (st.st_size == 0) {
    anthy_log(0, "Failed to mmap 0size file (%s).\n", fn);
    close(fd);
    return NULL;
  }

  ptr = mmap(NULL, st.st_size, prot, MAP_SHARED, fd, 0);
  close(fd);
  if (ptr == MAP_FAILED) {
    anthy_log(0, "Failed to mmap() (%s).\n", fn);
    return NULL;
  }

  /* mmapに成功したので情報を返す */
  if (!(m = malloc(sizeof(struct filemapping)))) {
    anthy_log(0, "Failed malloc in %s:%d\n", __FILE__, __LINE__);
    munmap(ptr, st.st_size);
    return NULL;
  }
  m->size = st.st_size;
  m->ptr = ptr;
  m->wr = wr;

  return m;
}

void *
anthy_mmap_address(struct filemapping *m)
{
  if (!m) {
    return NULL;
  }
  return m->ptr;
}

int
anthy_mmap_size(struct filemapping *m)
{
  if (!m) {
    return 0;
  }
  return m->size;
}

int
anthy_mmap_is_writable(struct filemapping *m)
{
  if (!m) {
    return 0;
  }
  return m->wr;
}

void
anthy_munmap(struct filemapping *m)
{
  if (!m) {
    return ;
  }
  munmap(m->ptr, m->size);
  free(m);
}
