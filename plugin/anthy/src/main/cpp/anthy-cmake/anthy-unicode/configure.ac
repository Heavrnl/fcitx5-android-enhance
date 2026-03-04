m4_define([anthy_released], [0])
m4_define([anthy_major_version], [1])
m4_define([anthy_minor_version], [0])
m4_define([anthy_micro_version], [0])
m4_define(anthy_maybe_datestamp,
    m4_esyscmd([if test x]anthy_released[ != x1; then date +.%Y%m%d | tr -d '\n\r'; fi]))

m4_define([anthy_version],
    anthy_major_version.anthy_minor_version.anthy_micro_version[]anthy_maybe_datestamp)

AC_INIT([anthy-unicode], [anthy_version],
        [https://github.com/fujiwarat/anthy-unicode/issues],
        [anthy-unicode])

AM_INIT_AUTOMAKE([1.10])
AM_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIR(m4)

dnl Checks for programs.
AS_VERSION
AC_PROG_CC
AC_PROG_CPP
AM_PROG_LIBTOOL
AM_PATH_LISPDIR

AC_CHECK_FUNCS([strerror_r])

AM_CONDITIONAL(ELISP, test x$lispdir != x)

dnl without emacs. install-lispLISP does mkdir /anthy
dnl dirty hack to avoid it.
test -n "$lispdir" || lispdir="/tmp/"

lispdir="$lispdir/anthy-unicode"

test -z "$GCC" || CFLAGS="$CFLAGS -W -Werror=format-security -Wall -Wextra -Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes -pedantic -Wno-long-long"

AC_OUTPUT(Makefile
 src-diclib/Makefile src-worddic/Makefile 
 src-splitter/Makefile src-ordering/Makefile
 src-main/Makefile src-util/Makefile
 anthy/Makefile
 depgraph/Makefile
 m4/Makefile
 mkanthydic/Makefile mkworddic/Makefile
 mkworddic/dict.args test/Makefile
 alt-cannadic/Makefile
 doc/Makefile calctrans/Makefile
 anthy-unicode.conf anthy-unicode-test.conf anthy-unicode.spec anthy-unicode.pc)
