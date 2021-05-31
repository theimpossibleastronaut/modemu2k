
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

AM_CFLAGS = @EXTRA_CFLAGS@

modemu2k_SOURCES = modemu2k.c

libmodemu2k_a_SOURCES =	\
	cmdlex.l	\
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
	verbose.h	\
	gettext.h	\
	utils.c	\
	utils.h

DEFS = -DLOCALEDIR=\"$(localedir)\" -D_GNU_SOURCE

# static library used by 'make check'
noinst_LIBRARIES = libmodemu2k.a
libmodemu2k_a_CFLAGS = $(AM_CFLAGS)

bin_PROGRAMS = modemu2k
# If LIBINTL (required for gettext) isn't added, the build breaks on
# OpenBSD and probably others
modemu2k_LDADD = @LIBINTL@ libmodemu2k.a

clean-local:
	if test -f lex.yy.c; then \
		rm -f lex.yy.c; \
	fi
