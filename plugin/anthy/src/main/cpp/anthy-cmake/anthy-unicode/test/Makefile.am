SUBDIRS =

EXTRA_DIST = test.txt test.exp prediction.c test-matrix.c

INCLUDES = -I$(top_srcdir)/ -DSRCDIR=\"$(srcdir)\" \
	  -DTEST_HOME=\""`pwd`"\"

noinst_PROGRAMS = anthy checklib prediction test-matrix
anthy_SOURCES = main.c
checklib_SOURCES = check.c
prediction_SOURCES = prediction.c
test_matrix_SOURCES = test-matrix.c

anthy_LDADD = ../src-util/libconvdb.la ../src-main/libanthy-unicode.la ../src-worddic/libanthydic-unicode.la
checklib_LDADD = ../src-main/libanthy-unicode.la ../src-worddic/libanthydic-unicode.la
prediction_LDADD = ../src-main/libanthy-unicode.la ../src-worddic/libanthydic-unicode.la
test_matrix_LDADD = ../src-main/libanthy-unicode.la ../src-worddic/libanthydic-unicode.la

mostlyclean-local:
	-rm -rf .anthy*
