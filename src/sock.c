/*
 *
 * Copyright 2018-2022 Andy Alt <arch_stanton5995@protonmail.com>
 *
 * modemu2k is a fork of modemu
 * Originally developed by Toru Egashira
 * Copyright (c) 1995, 1996
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
// #include <sys/time.h>   /*->ttybuf.h (timeval)*/
#include <stdlib.h>             /*(getenv) */
#include "m2k_private.h"
#include "m2k_ctx.h"

void
sockInit(struct st_sock *sock)
{
  sock->fd = 0;
  sock->alive = 0;
  sock->rp = NULL;
}

int
sockClose(st_sock *sock)
{
  if (sock->fd <= 0)
    return 0;

  int r = close(sock->fd);
  if (r != 0)
    perror("sockClose()");

  sock->fd = sock->alive = 0;
  return r;
}

int
sockShutdown(st_sock *sock)
{
  if (sock->fd <= 0)
    return 0;
  shutdown(sock->fd, 2);
  return sockClose(sock);
}

int
m2k_sockListen(m2k_t *ctx, const char *port)
{
  struct addrinfo hints, *res;
  memset(&hints, 0, sizeof hints);
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags    = AI_PASSIVE | AI_NUMERICSERV;

  int rc = getaddrinfo(NULL, port, &hints, &res);
  if (rc != 0)
  {
    m2k_log(ctx, "getaddrinfo: %s\n", gai_strerror(rc));
    return -1;
  }

  int server_fd = -1;
  static const int families[2] = {AF_INET6, AF_INET};
  for (int fi = 0; fi < 2 && server_fd == -1; fi++)
  {
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next)
    {
      if (ai->ai_family != families[fi])
        continue;
      server_fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
      if (server_fd == -1)
        continue;
      int one = 1;
      setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef IPV6_V6ONLY
      if (ai->ai_family == AF_INET6)
      {
        int zero = 0;
        setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
        int v6only = 1;
        socklen_t vlen = sizeof v6only;
        getsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &vlen);
        if (v6only != 0)
        {
          close(server_fd);
          server_fd = -1;
          continue;
        }
      }
#endif
      if (bind(server_fd, ai->ai_addr, ai->ai_addrlen) != 0 ||
          listen(server_fd, 1) != 0)
      {
        m2k_log(ctx, "bind/listen: %s\n", strerror(errno));
        close(server_fd);
        server_fd = -1;
        continue;
      }
      break;
    }
  }

  freeaddrinfo(res);

  if (server_fd == -1)
  {
    m2k_log(ctx, "Failed to bind/listen on port %s\n", port);
    return -1;
  }

  m2k_log(ctx, "Listening on port %s...\n", port);
  return server_fd;
}

int
m2k_sockAccept(m2k_t *ctx, int server_fd)
{
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;
  int client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
  close(server_fd);

  if (client_fd == -1)
  {
    m2k_log(ctx, "accept: %s\n", strerror(errno));
    return -1;
  }
  return client_fd;
}

int
m2k_sockDial(m2k_t *ctx, st_sock *sock)
{
  struct addrinfo hints, *result;
  memset(&hints, 0, sizeof(struct addrinfo));

  hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;
  hints.ai_protocol = 0;        /* Any protocol */

  char out_port[PORT_MAX + 1];
  if ((size_t) snprintf(out_port, sizeof out_port, "%d",
                        ctx->atcmd.d.port.type == ATDP_NUL
                          ? DEFAULT_PORT
                          : atoi(ctx->atcmd.d.port.str)) >= sizeof out_port)
    return 1;

  if (ctx->atcmd.d.port.type != ATDP_NUL)
    ctx->telOpt.sentReqs = 1;        /* skip sending option requests */

  int s = getaddrinfo(ctx->atcmd.d.addr.str, out_port, &hints, &result);
  if (s != 0)
  {
    m2k_log(ctx, "Host address lookup failed: %s\n", gai_strerror(s));
    return 1;
  }

  sockInit(sock);
  for (sock->rp = result; sock->rp != NULL; sock->rp = sock->rp->ai_next)
  {
    sock->fd =
      socket(sock->rp->ai_family, sock->rp->ai_socktype,
             sock->rp->ai_protocol);
    if (sock->fd == -1)
      continue;

    int tmp = 1;
    if (setsockopt(sock->fd, SOL_SOCKET, SO_OOBINLINE, &tmp, sizeof(tmp)) < 0)
    {
      m2k_log(ctx, "setsockopt(): %s\n", strerror(errno));
      sockClose(sock);
      continue;
    }

#ifdef NO_DIAL_CANCELING
    /* blocking connect. */
    if (connect(sock->fd, sock->rp->ai_addr, sock->rp->ai_addrlen) == 0)
    {
      sock->alive = 1;
      freeaddrinfo(result);
      return 0;
    }
    m2k_log(ctx, "connect(): %s\n", strerror(errno));
    sockClose(sock);
    /* try next address */
#else /*!ifdef NO_DIAL_CANCELING */
    {
      /* nonblocking connect. */
      /* SOCKS version 4.2 or higher is required for SOCKS support */
      fd_set rfds, wfds;
      struct timeval tv;
      struct timeval to, t;

      tmp = 1;
      ioctl(sock->fd, FIONBIO, &tmp);   /* non-blocking i/o */

      /* but Term's connect() blocks here... */
      if (connect(sock->fd, sock->rp->ai_addr, sock->rp->ai_addrlen) < 0
          && errno != EINPROGRESS)
      {
        m2k_log(ctx, "connect(): %s\n", strerror(errno));
        sockClose(sock);
        continue;               /* try next address */
      }

      FD_ZERO(&rfds);
      FD_ZERO(&wfds);
      tv.tv_sec = 0;

      timevalSet10ms(&t, ctx->atcmd.s[7] * 100);     /* S7 sec */
      gettimeofday(&to, NULL);
      timevalAdd(&to, &t);      /* S7 sec after */

      /* SOCKS Rselect() first checks if connected, then select(). */
      /* so, select() with large timeval is inappropriate */
      do
      {
        if (!ctx->atcmd.pd)
          FD_SET(ctx->tty.rfd, &rfds);
        FD_SET(sock->fd, &wfds);
        tv.tv_usec = 200 * 1000;        /* 0.2sec period */

      RETRY:
        if (select(sock->fd + 1, &rfds, &wfds, NULL, &tv) < 0)
        {
          if (errno == EINTR)
            goto RETRY;
          m2k_log(ctx, "select(): %s\n", strerror(errno));
          freeaddrinfo(result);
          sockShutdown(sock);
          return 1;
        }
#if 0
        verboseOut(ctx, VERB_MISC, "tty=%d, sock=%d\r\n",
                   FD_ISSET(ctx->tty.rfd, &rfds), FD_ISSET(sock->fd, &wfds));
#endif
        if (FD_ISSET(ctx->tty.rfd, &rfds))
        {
          freeaddrinfo(result);
          sockShutdown(sock);
          verboseOut(ctx, VERB_MISC,
                     "Connecting attempt canceled by user input.\r\n");
          return 1;
        }
        /* check if really connected or not */

        /*if (FD_ISSET(sock->fd, &wfds)
           && getpeername(sock->fd, (struct sockaddr *)&sa, &tmp) == 0) */

        /* SOCKS requires this check method (ref: What_SOCKS_expects) */
        if (FD_ISSET(sock->fd, &wfds))
        {
          if (connect(sock->fd, sock->rp->ai_addr, sock->rp->ai_addrlen) < 0
              && errno != EISCONN)
          {
            m2k_log(ctx, "connect()-2: %s\n", strerror(errno));
            sockClose(sock);
            goto next_addr;     /* try next address */
          }

          tmp = 0;
          ioctl(sock->fd, FIONBIO, &tmp);       /* blocking i/o */
          sock->alive = 1;
          freeaddrinfo(result);
          return 0;
        }

        gettimeofday(&t, NULL);
      }
      while (timevalCmp(&t, &to) < 0);

      freeaddrinfo(result);
      sockShutdown(sock);
      verboseOut(ctx, VERB_MISC, "Connection attempt timed out.\r\n");
      return 1;                 /* timeout */
    }
  next_addr:
    ;                           /* try next address in list */
#endif /*ifdef NO_DIAL_CANCELING */
  }

  /* No address succeeded */
  if (result)
    freeaddrinfo(result);
  return 1;
}
