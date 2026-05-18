#include "test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include "modemu2k.h"

/* Probe IPv6 routing by connect()ing a SOCK_DGRAM socket — no packets sent,
   but ENETUNREACH is returned immediately if no route exists. */
static int
has_ipv6_connectivity(void)
{
  int fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (fd < 0)
    return 0;
  struct sockaddr_in6 addr = {0};
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(80);
  inet_pton(AF_INET6, "2001:4860:4860::8888", &addr.sin6_addr);
  int ok = (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);
  close(fd);
  return ok;
}

static void
setup(void)
{
  atcmd.s[7] = 20;
  atcmd.pd = 1;
}

static void
test_ipv4_numeric(void)
{
  st_sock sock;
  telOptReset();
  m2k_atcmdD("140.82.113.3 80", ATDA_NUM, ATDP_NUM);
  assert(m2k_sockDial(&sock) == 0);
  sleep(1);
  assert(sockShutdown(&sock) == 0);
}

static void
test_ipv6_hostname(void)
{
  /* ipv6.google.com has only AAAA records, forcing an IPv6 connection */
  st_sock sock;
  telOptReset();
  m2k_atcmdD("ipv6.google.com 80", ATDA_STR, ATDP_NUM);
  assert(m2k_sockDial(&sock) == 0);
  sleep(1);
  assert(sockShutdown(&sock) == 0);
}

int
main(void)
{
  if (!has_ipv6_connectivity())
    return 77;

  setup();
  test_ipv4_numeric();
  test_ipv6_hostname();
  return 0;
}
