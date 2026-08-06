#include "test.h"
#include "test_helpers.h"

/* The parent binds first (kernel-assigned port 0, discovered via
   getsockname on m2k_get_listen_fd), so a single connect from the forked
   child suffices — no retry loop and no fixed-port collision with other
   tests or build dirs. */
static void
test_setup_listen(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  assert(m2k_setup_listen(ctx, "0") == M2K_OK);
  if (start_connector(AF_INET, test_local_port(m2k_get_listen_fd(ctx))) < 0)
    exit(77);
  assert(m2k_listen_accept(ctx) == M2K_OK);
  m2k_free(ctx);
}

/* The port matters to a host that binds 0 and has to tell a user where
   to connect. Unlike the answer listener, this one is consumed by
   m2k_listen_accept(), so the port stops being reportable there. */
static void
test_get_listen_port(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx != NULL);
  assert(m2k_get_listen_port(ctx) == -1);

  assert(m2k_setup_listen(ctx, "0") == M2K_OK);
  int port = m2k_get_listen_port(ctx);
  assert(port > 0);
  assert(port == test_local_port(m2k_get_listen_fd(ctx)));

  if (start_connector(AF_INET, port) < 0)
    exit(77);
  assert(m2k_listen_accept(ctx) == M2K_OK);
  assert(m2k_get_listen_port(ctx) == -1);

  m2k_free(ctx);
}

int
main(void)
{
  test_setup_listen();
  test_get_listen_port();
  return 0;
}
