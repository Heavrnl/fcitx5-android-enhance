AUTOMAKE_OPTIONS =
EXTRA_DIST = \
    ChangeLog-old \
    DIARY \
    Doxyfile \
    README.en \
    anthy-unicode.conf.in anthy-unicode-test.conf.in \
    anthy-unicode.pc.in \
    anthy-unicode.spec.in \
    $(NULL)

SRCDIR = src-diclib src-worddic src-splitter src-ordering src-main src-util

SUBDIRS = $(SRCDIR) anthy depgraph mkworddic calctrans mkanthydic doc test alt-cannadic m4

sysconf_DATA = anthy-unicode.conf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = anthy-unicode.pc

MOSTLYCLEANFILES = core

PREV_DATE=20190412

update_params:
	make -C calctrans update_params
	make
update_params0:
	make -C calctrans update_params0
	make
update_params2:
	make -C calctrans update_params2
	make

dist-hook:
	if test -d .git ; then \
	    git log --name-status --date=iso > $(distdir)/ChangeLog ; \
	    PREV_MICRO_VERSION=`expr $(PACKAGE_VERSION_MICRO) - 1`; \
	    PREV_VERSION=\
"$(PACKAGE_VERSION_MAJOR).$(PACKAGE_VERSION_MINOR).$(PACKAGE_VERSION_MICRO).$(PREV_DATE)"; \
	    echo "Changes in $(PACKAGE_NAME) $(PACKAGE_VERSION)" \
	        > $(distdir)/NEWS; \
	    echo "" >> $(distdir)/NEWS; \
	    git shortlog $$PREV_VERSION...$(PACKAGE_VERSION) \
	        >> $(distdir)/NEWS; \
	    echo "" >> $(distdir)/NEWS; \
	    git log $$PREV_VERSION...$(PACKAGE_VERSION) --reverse \
	        --pretty=format:'%s (%an) %h' >> $(distdir)/NEWS; \
	fi

@PACKAGE_NAME@-@PACKAGE_VERSION@.%.sum: @PACKAGE_NAME@-@PACKAGE_VERSION@.%
	f=`basename $@ .sum`; \
	printf "cksum %s %s %s\n"      `cksum $$f`     >  $@; \
	printf "sha1sum %s %s\n"       `sha1sum $$f`   >> $@; \
	printf "sha256sum %s %s\n"     `sha256sum $$f` >> $@; \
	printf "sha512sum %s %s\n"     `sha512sum $$f` >> $@; \
	$(NULL)

