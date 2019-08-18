AM_CFLAGS = -I$(top_srcdir)/src

check_PROGRAMS =	\
	getNumArg

getNumArg_SOURCES = getNumArg.c

TESTS = $(check_PROGRAMS)

# If LIBINTL (used by gettext) isn't added, the build breaks on
# OpenBSD and probably others
LDADD = @LIBINTL@ $(top_builddir)/src/libmodemu2k_test.a
