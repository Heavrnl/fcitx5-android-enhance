# Files
noinst_SCRIPTS =
CLEANFILES = anthy.dic
INCLUDES = -I$(top_srcdir)/ -DSRCDIR=\"$(srcdir)\"

noinst_PROGRAMS = mkfiledic
mkfiledic_SOURCES = mkfiledic.c
mkfiledic_LDADD = ../src-diclib/libdiclib.la

anthy.dic : mkfiledic ../mkworddic/anthy.wdic ../depgraph/anthy.dep ../calctrans/anthy.cand_info ../calctrans/anthy.trans_info ../calctrans/anthy.corpus_array ../calctrans/anthy.corpus_bucket
	./mkfiledic


# To install 
pkgdata_DATA = anthy.dic
