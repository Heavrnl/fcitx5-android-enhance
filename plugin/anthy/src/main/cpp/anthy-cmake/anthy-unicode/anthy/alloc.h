/*
 * 構造体allocator
 * ソースコード中ではatorと略することがある
 */
#ifndef _alloc_h_included_
#define _alloc_h_included_

/** アロケータのハンドル */
typedef struct allocator_priv * allocator;

/*
 * allocatorを作る
 * s: 構造体のsize(バイト数)
 * dtor: =destructor 確保したオブジェクトが解放されるときに呼ばれる関数
 *  dtorの引数は解放されるオブジェクト
 * 返り値: 作成したallocator 
 */
allocator anthy_create_allocator(int s, void (*dtor)(void *));

/*
 * allocatorを解放する
 *  この際に、このallocatorから確保されたオブジェクトは全て解放される
 * a: 解放するallocator
 */
void anthy_free_allocator(allocator a);

/*
 * オブジェクトを確保する
 * a: allocator
 * 返り値: 確保したオブジェクトのアドレス
 */
void *anthy_smalloc(allocator a);
/*
 * オブジェクトを解放する
 * a: allocator
 * p: 解放するオブジェクトのアドレス
 */
void anthy_sfree(allocator a, void *p);

/* 全てのallocatorを破棄する */
void anthy_quit_allocator(void);

#endif
