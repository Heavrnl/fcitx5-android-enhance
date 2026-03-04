#ifndef __diclib_inner_h_included__
#define __diclib_inner_h_included__


#define ANTHY_DIR_SEPARATOR '/'
#define ANTHY_IS_DIR_SEPARATOR(c) ((c) == ANTHY_DIR_SEPARATOR)

typedef enum
{
  ANTHY_FILE_TEST_IS_REGULAR    = 1 << 0,
  ANTHY_FILE_TEST_IS_SYMLINK    = 1 << 1,
  ANTHY_FILE_TEST_IS_DIR        = 1 << 2,
  ANTHY_FILE_TEST_IS_EXECUTABLE = 1 << 3,
  ANTHY_FILE_TEST_EXISTS        = 1 << 4
} AnthyFileTest;

/* file_dic.h */
int anthy_init_file_dic(void);
void anthy_quit_file_dic(void);
int anthy_file_test(const char   *filename, AnthyFileTest test);

/* xchar.c */
void anthy_init_xchar_tab(void);

#endif
