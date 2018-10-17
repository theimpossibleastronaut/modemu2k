SUBDIRS = src man po
packextrasdir=@docdir@
EXTRA_DIST = 	COPYING README QuickStart INSTALL AUTHORS ChangeLog \
		NEWS CONTRIBUTING.md CODE_OF_CONDUCT.md

packextras_DATA=$(EXTRA_DIST)

ACLOCAL_AMFLAGS = --install -I m4

AUTOMAKE_OPTIONS = subdir-objects no-dependencies
