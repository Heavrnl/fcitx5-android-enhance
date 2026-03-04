## $Id: Makefile.am,v 1.9 2002/03/04 09:43:31 yusuke Exp $

INCLUDES = -I$(top_srcdir)/

lib_LTLIBRARIES = libanthy-unicode.la

libanthy_unicode_la_LIBADD = ../src-splitter/libsplit.la ../src-ordering/libordering.la -lm ../src-worddic/libanthydic-unicode.la

libanthy_unicode_la_LDFLAGS = -version-info 1:0:1

libanthy_unicode_la_SOURCES = \
 main.c context.c main.h
