## $Id: Makefile.am,v 1.10 2002/11/05 15:38:58 yusuke Exp $

# Files
noinst_SCRIPTS =
EXTRA_DICS = base.t extra.t compound.t name.t adjust.t utf8.t tankanji.t
ZIPCODE_DICT = zipcode.t
CANNADIC_DIST = gcanna.ctd gcannaf.ctd
INCLUDES = -I$(top_srcdir)/
# HOKUTODIC_DIST = hokuto.t
# You can get it from
# http://winnie.kuis.kyoto-u.ac.jp/members/ri/hokuto/dic/index.html
HOKUTODIC_DIST =
CLEANFILES = anthy.wdic
EXTRA_DIST = \
 $(EXTRA_DICS) $(ZIPCODE_DICT) $(HOKUTODIC_DIST) \
 udict dict.args.in

# Files, to build dictionay, not to pack sources.
DIC_FILES = @top_srcdir@/alt-cannadic/gcanna.ctd \
	    @top_srcdir@/alt-cannadic/gcannaf.ctd \
	    @top_srcdir@/alt-cannadic/gtankan.ctd \
	    @top_srcdir@/mkworddic/adjust.t \
	    @top_srcdir@/mkworddic/compound.t \
	    @top_srcdir@/mkworddic/extra.t \
	    @top_srcdir@/alt-cannadic/g_fname.t \
	    @top_srcdir@/mkworddic/udict

# Generate the dictionary
noinst_PROGRAMS = mkworddic
mkworddic_SOURCES = mkdic.c writewords.c mkudic.c calcfreq.c mkdic.h
mkworddic_LDADD = ../src-worddic/libanthydic-unicode.la

noinst_DATA = anthy.wdic

anthy.wdic : mkworddic $(DIC_FILES)
	   ./mkworddic -f ./dict.args


# To install
pkgdata_DATA = zipcode.t
