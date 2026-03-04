EXTRA_DIST =\
 corpus.0.txt corpus.1.txt corpus.2.txt\
 corpus.3.txt corpus.4.txt corpus.5.txt\
 corpus_info weak_words

noinst_PROGRAMS = calctrans proccorpus
INCLUDES = -I$(top_srcdir)/

calctrans_SOURCES = calctrans.c input_set.c input_set.h corpus.c
calctrans_LDADD = ../src-main/libanthy-unicode.la ../src-worddic/libanthydic-unicode.la -lm
proccorpus_SOURCES = proccorpus.c
proccorpus_LDADD = ../src-util/libconvdb.la ../src-main/libanthy-unicode.la ../src-worddic/libanthydic-unicode.la

dict_source_files = anthy.cand_info anthy.trans_info anthy.corpus_array anthy.corpus_bucket anthy.weak_words

update_params:
	rm -f parsed_data2
	./proccorpus $(srcdir)/corpus.?.txt > parsed_data
	./calctrans parsed_data -o $(srcdir)/corpus_info
	./calctrans parsed_data -e -o $(srcdir)/weak_words
	make do_update_params

update_params0:
	rm -f parsed_data2
	./proccorpus $(srcdir)/corpus.0.txt > parsed_data
	./calctrans parsed_data -o $(srcdir)/corpus_info
	./calctrans parsed_data -e -o $(srcdir)/weak_words
	make do_update_params

update_params2:
	./proccorpus $(srcdir)/corpus.?.txt >> parsed_data2
	./calctrans parsed_data parsed_data2 -o $(srcdir)/corpus_info
	./calctrans parsed_data parsed_data2 -e -o $(srcdir)/weak_words
	make do_update_params

do_update_params:
	./calctrans -c $(srcdir)/corpus_info
	rm -f $(dict_source_files)
	make $(dict_source_files)

$(dict_source_files): $(srcdir)/corpus_info $(srcdir)/weak_words calctrans
	./calctrans -c $(srcdir)/corpus_info $(srcdir)/weak_words

noinst_DATA = $(dict_source_files)
CLEANFILES = $(dict_source_files) parsed_data parsed_data2 anthy.feature_info
