#include "test.h"
#include "test_helpers.h"

/* Check that dual-stack actually works: m2k_sockListen tries AF_INET6 with
   IPV6_V6ONLY=0 and falls back to IPv4 if the OS refuses (e.g. OpenBSD).
   Replicate that probe here so we skip rather than time out.

   Binding ::1 (not just creating the socket) is the load-bearing step: with
   IPv6 administratively disabled (net.ipv6.conf.all.disable_ipv6=1), the
   socket() and IPV6_V6ONLY calls all still succeed, but bind(::1) fails with
   EADDRNOTAVAIL — which is exactly the condition that would otherwise make
   m2k_listen_accept() below block until the meson timeout. */
static int
has_dualstack(void)
{
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0)
    return 0;
#ifdef IPV6_V6ONLY
  int zero = 0;
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);
  int v6only = 1;
  socklen_t vlen = sizeof v6only;
  getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, &vlen);
  if (v6only != 0)
  {
    close(fd);
    return 0;
  }
#endif
  struct sockaddr_in6 loop = {0};
  loop.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &loop.sin6_addr);
  int ok = bind(fd, (struct sockaddr *) &loop, sizeof loop) == 0;
  close(fd);
  return ok;
}

static void
test_setup_listen_ipv6(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  assert(m2k_setup_listen(ctx, "0") == M2K_OK);
  if (start_connector(AF_INET6, test_local_port(m2k_get_listen_fd(ctx))) < 0)
    exit(77);
  assert(m2k_listen_accept(ctx) == M2K_OK);
  m2k_free(ctx);
}

int
main(void)
{
  if (!has_dualstack())
    return 77;

  test_setup_listen_ipv6();
  return 0;
}
