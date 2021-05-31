AC_INIT([modemu2k],
        [0.1.0-dev],
        [andy400-dev@yahoo.com],
        ,
        [https://github.com/theimpossibleastronaut/modemu2k])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([build-aux/m4])
AM_INIT_AUTOMAKE([-Wall subdir-objects foreign no-dependencies])
AM_SILENT_RULES([yes])
AC_PREREQ(2.69)
AC_CONFIG_HEADERS([src/config.h:config.in])

AX_IS_RELEASE([git-directory])
# Define the variables listed in the second argument if debug is
# enabled, defaulting to no variables. Defines the variables listed in
# the third argument if debug is disabled, defaulting to NDEBUG. All
# lists of variables should be space-separated.
#
# A "yes" value will be overridden by the value returned by AX_IS_RELEASE()
AX_CHECK_ENABLE_DEBUG([yes])

AC_PROG_CC
AX_CFLAGS_WARN_ALL
AC_PROG_INSTALL

EXTRA_CFLAGS="-fno-common"

AX_CHECK_COMPILE_FLAG(-fanalyzer, [ax_gcc_flag_option=yes], [ax_gcc_flag_option=no], [-Werror])
if test "x$ax_gcc_flag_option" = "xyes"; then
        EXTRA_CFLAGS+=" -fanalyzer"
fi

# For creating the static test library
AC_PROG_RANLIB
AM_PROG_AR

AM_PROG_LEX

AM_GNU_GETTEXT([external], [need-formatstring-macros])
AM_GNU_GETTEXT_VERSION([0.19.8])

# This prevents the error shown in
# https://github.com/theimpossibleastronaut/rmw/commit/90200c2df06b16f16b5d21d25c51966c0ee65b23#commitcomment-30291627
# and https://github.com/theimpossibleastronaut/rmw/issues/21
AM_MAINTAINER_MODE

AX_VALGRIND_DFLT([memcheck], [on])
AX_VALGRIND_CHECK([off])

AC_SUBST([EXTRA_CFLAGS])

AC_CONFIG_FILES([	Makefile
			src/Makefile
			man/Makefile
			tests/Makefile
			scripts/Makefile
			po/Makefile.in])

AC_CONFIG_FILES([scripts/m2k-minicom], [chmod +x scripts/m2k-minicom])

AC_OUTPUT
