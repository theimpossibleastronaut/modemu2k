SUBDIRS = src
packextrasdir=@docdir@
EXTRA_DIST = 	COPYING README QuickStart \
		examples/m2k-minicom.sh
		examples/xc-sends-mail
packextras_DATA=$(EXTRA_DIST)

PACKAGE = @PACKAGE@
VERSION = @VERSION@

ACLOCAL_AMFLAGS = --install -I m4

AUTOMAKE_OPTIONS = subdir-objects foreign no-dependencies
