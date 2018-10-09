SUBDIRS = src
packextrasdir=@docdir@
EXTRA_DIST = 	COPYING README QuickStart INSTALL AUTHORS ChangeLog \
		NEWS CONTRIBUTING.md CODE_OF_CONDUCT.md	\
		examples/m2k-minicom.sh
		examples/xc-sends-mail
packextras_DATA=$(EXTRA_DIST)

PACKAGE = @PACKAGE@
VERSION = @VERSION@

ACLOCAL_AMFLAGS = --install -I m4

AUTOMAKE_OPTIONS = subdir-objects no-dependencies

dist_man_MANS = man/modemu2k.1
