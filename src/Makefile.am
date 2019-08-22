
## SunOS 4.1
#CC	= gcc
#CFLAGS_OS = -DUSE_ON_EXIT
#LDFLAGS_OS =
#LIBS_OS =

## A certain SVR4.2 based OS
#CC	= /usr/abiccs/bin/cc
#CFLAGS_OS = -DUSE_FILIO_H
#LDFLAGS_OS =
#LIBS_OS	= -lnsl -lsocket

#}

#{ Proxy sockets

## SOCKS
#CFLAGS_PS = -DSOCKS -Dconnect=Rconnect -Dselect=Rselect
#LIBS_PS = -lsocks
#LDFLAGS_PS = -L$$HOME/lib

## Term
#CFLAGS_PS = -DTERMNET -I$$HOME/include -DNO_DIAL_CANCELING
#LDFLAGS_PS = -L$$HOME/lib
#LIBS_PS = -ltermnet
##CFLAGS_PS = -Dgethostbyname=term_gethostbyname -Dconnect=term_connect \
##	-Dshutdown=term_shutdown -Dclose=term_close \
##	-Drecv=term_recv -Dsend=term_send -Dsocket=term_socket

#

bin_PROGRAMS = modemu2k

modemu2k_SOURCES =	\
	modemu2k.c	\
	atcmd.c	\
	commx.c	\
	sockbuf.c	\
	stty.c	\
	timeval.c	\
	verbose.c	\
	cmdarg.c	\
	sock.c	\
	telopt.c	\
	ttybuf.c	\
	lex.yy.c	\
	atcmd.h	\
	cmdarg.h	\
	cmdlex.h	\
	commx.h	\
	defs.h	\
	sockbuf.h	\
	sock.h	\
	stty.h	\
	telopt.h	\
	timeval.h	\
	ttybuf.h	\
	verbose.h \
	gettext.h

DEFS = -DLOCALEDIR=\"$(localedir)\" -D_GNU_SOURCE

# If LIBINTL (required for gettext) isn't added, the build breaks on
# OpenBSD and probably others
LDADD = @LIBINTL@

# static library used by 'make check'
check_LIBRARIES = libmodemu2k_test.a
libmodemu2k_test_a_SOURCES = $(modemu2k_SOURCES)
libmodemu2k_test_a_CFLAGS = $(AM_CFLAGS) -DUSE_AS_TEST_LIB

lex.yy.c:
	$(LEX) $(LFLAGS) @srcdir@/cmdlex.l

clean-local:
	if test -f lex.yy.c; then \
		rm -f lex.yy.c; \
	fi
