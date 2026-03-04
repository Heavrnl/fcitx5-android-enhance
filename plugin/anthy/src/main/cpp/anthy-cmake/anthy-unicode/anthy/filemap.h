/* mmapを抽象化する */
#ifndef _filemap_h_included_
#define _filemap_h_included_

/* メモリ上にmapされたファイルのハンドル */
struct filemapping;

struct filemapping *anthy_mmap(const char *fn, int wr);
void *anthy_mmap_address(struct filemapping *m);
int anthy_mmap_size(struct filemapping *m);
int anthy_mmap_is_writable(struct filemapping *m);
void anthy_munmap(struct filemapping *m);

#endif
