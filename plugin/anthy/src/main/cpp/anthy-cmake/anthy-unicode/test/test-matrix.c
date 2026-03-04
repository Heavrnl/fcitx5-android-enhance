/* 疎行列のテスト用コード */
#include <stdlib.h>
#include <stdio.h>
#include <anthy/dic.h>
#include <anthy/diclib.h>
#include <anthy/matrix.h>

static void
change_endian(struct matrix_image *im)
{
  int i;
  for (i = 0; i < im->size; i++) {
    im->image[i] = anthy_dic_htonl(im->image[i]);
  }
}

static void
zero_matrix(void)
{
  struct sparse_matrix *m;
  struct matrix_image *mi;
  int *im, e;
  m = anthy_sparse_matrix_new();
  anthy_sparse_matrix_make_matrix(m);
  mi = anthy_matrix_image_new(m);
  change_endian(mi);
  im = mi->image;
  e = anthy_matrix_image_peek(im, 0, 0);
  printf("zero matrix: size=%d (0,0)=%d\n", mi->size, e);
  free(mi->image);
  free(mi);
  anthy_sparse_matrix_free(m);
}

static void
dense_matrix(void)
{
  int i, j, fail;
  struct sparse_matrix *m;
  struct matrix_image *mi;
  int *im, e;
  m = anthy_sparse_matrix_new();
  for (i = 0; i < 100; i++) {
    for (j = 0; j < 100; j++) {
      anthy_sparse_matrix_set(m, i, j, i + j, NULL);
    }
  }
  anthy_sparse_matrix_make_matrix(m);
  mi = anthy_matrix_image_new(m);
  change_endian(mi);
  im = mi->image;
  fail = 0;
  for (i = 0; i < 100; i++) {
    for (j = 0; j < 100; j++) {
      e = anthy_matrix_image_peek(im, i, j);
      if (e != i+j) {
	printf("image(%d,%d) == %d != %d\n", i,j,e,i+j);
	fail ++;
      }
      e = anthy_sparse_matrix_get_int(m, i, j);
      if (e != i+j) {
	printf("origin(%d,%d) == %d != %d\n", i,j,e,i+j);
	fail ++;
      }
    }
  }
  printf("%d errors in desnse matrix\n", fail);
  free(mi->image);
  free(mi);
  anthy_sparse_matrix_free(m);
}

int
main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  zero_matrix();
  dense_matrix();
  return 0;
}
