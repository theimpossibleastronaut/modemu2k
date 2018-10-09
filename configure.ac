AC_INIT([modemu2k],
        [0.0.3-dev],
        [andy400-dev@yahoo.com],
        ,
        [https://github.com/theimpossibleastronaut/modemu2k])
AM_INIT_AUTOMAKE
AC_PREREQ(2.69)
AC_CONFIG_HEADERS([src/config.h:config.in])
AC_PROG_CC
AC_PROG_INSTALL

# This prevents the error shown in
# https://github.com/theimpossibleastronaut/rmw/commit/90200c2df06b16f16b5d21d25c51966c0ee65b23#commitcomment-30291627
# and https://github.com/theimpossibleastronaut/rmw/issues/21
AM_MAINTAINER_MODE

AC_CONFIG_FILES([	Makefile
			src/Makefile])
AC_OUTPUT
