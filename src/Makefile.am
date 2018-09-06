LEX	= flex

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
AM_CFLAGS = -std=c99 -Wall -g -D_GNU_SOURCE
modemu2k_SOURCES = modemu2k.c atcmd.c commx.c sockbuf.c stty.c \
	timeval.c verbose.c cmdarg.c sock.c telopt.c ttybuf.c lex.yy.c

lex.yy.c:
	$(LEX) $(LFLAGS) cmdlex.l
