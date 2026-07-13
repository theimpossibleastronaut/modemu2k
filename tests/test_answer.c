/* Answer-side (ATA/RING/S0) tests — line-side listener bound by
   m2k_setup_answer; incoming TCP connections are "calls". */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>

/* Discover the ephemeral port of the bound answer listener. */
static int
answer_port(m2k_t *ctx)
{
  struct sockaddr_storage ss;
  socklen_t slen = sizeof ss;
  assert(getsockname(m2k_get_answer_fd(ctx), (struct sockaddr *) &ss, &slen) == 0);
  if (ss.ss_family == AF_INET6)
    return ntohs(((struct sockaddr_in6 *) &ss)->sin6_port);
  return ntohs(((struct sockaddr_in *) &ss)->sin_port);
}

static void
test_setup_answer_binds(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_get_answer_fd(ctx) == -1);
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);
  assert(m2k_get_answer_fd(ctx) >= 0);
  assert(answer_port(ctx) > 0);
  /* Second bind while one is active is an error; the first survives. */
  int fd = m2k_get_answer_fd(ctx);
  assert(m2k_setup_answer(ctx, "0") != M2K_OK);
  assert(m2k_get_answer_fd(ctx) == fd);
  m2k_free(ctx);
}

int
main(void)
{
  test_setup_answer_binds();
  return 0;
}
