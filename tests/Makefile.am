AM_CFLAGS = -I$(top_srcdir)/src

TESTS =	\
	getNumArg	\
	connect

check_PROGRAMS = $(TESTS:.c=)

# If LIBINTL (used by gettext) isn't added, the build breaks on
# OpenBSD and probably others
LDADD = @LIBINTL@ $(top_builddir)/src/libmodemu2k_test.a
