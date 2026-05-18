#include "test.h"

#include <ifaddrs.h>
#include <netinet/in.h>
#include <unistd.h>
#include "modemu2k.h"

static int
has_ipv6_interface(void)
{
  struct ifaddrs *ifap, *ifa;
  if (getifaddrs(&ifap) != 0)
    return 0;
  for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next)
  {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET6)
      continue;
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *) ifa->ifa_addr;
    if (IN6_IS_ADDR_LOOPBACK(&sa6->sin6_addr))
      continue;
    freeifaddrs(ifap);
    return 1;
  }
  freeifaddrs(ifap);
  return 0;
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
  if (!has_ipv6_interface())
    return 77;

  setup();
  test_ipv4_numeric();
  test_ipv6_hostname();
  return 0;
}
