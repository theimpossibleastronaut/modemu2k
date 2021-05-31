AM_CFLAGS = -I$(top_srcdir)/src @EXTRA_CFLAGS@

check_PROGRAMS = test_atcmd
test_atcmd_SOURCES = $(top_srcdir)/src/atcmd.c

TESTS=$(check_PROGRAMS)

@VALGRIND_CHECK_RULES@
AUTOMAKE_OPTIONS = parallel-tests
# TEST_EXTENSIONS = .sh
LOG_COMPILER = $(VALGRIND)

DEFS = -DUSE_AS_TEST_LIB

# If LIBINTL (used by gettext) isn't added, the build breaks on
# OpenBSD and probably others
LDADD = @LIBINTL@ $(top_builddir)/src/libmodemu2k.a
