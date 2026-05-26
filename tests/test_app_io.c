#include "test.h"
#include <poll.h>
#include <stddef.h>
#include <string.h>

static int
contains(const char *hay, size_t haylen, const char *needle)
{
  size_t nlen = strlen(needle);
  if (nlen > haylen) return 0;
  for (size_t i = 0; i + nlen <= haylen; i++)
    if (memcmp(hay + i, needle, nlen) == 0)
      return 1;
  return 0;
}

static void
test_at_round_trip(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);

  assert(m2k_write_from_app(ctx, "AT\r", 3) == M2K_OK);

  char drained[512];
  size_t total = 0;
  for (int i = 0; i < 4 && !m2k_run_done(ctx); i++)
  {
    struct pollfd fds[M2K_MAX_POLLFDS];
    size_t nfds = M2K_MAX_POLLFDS;
    int timeout_ms;
    assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
    assert(m2k_step(ctx, fds, nfds) == M2K_OK);

    size_t n = 0;
    assert(m2k_read_to_app(ctx, drained + total, sizeof(drained) - total, &n) == M2K_OK);
    total += n;
  }
  assert(contains(drained, total, "OK"));
  m2k_free(ctx);
}

int
main(void)
{
  test_at_round_trip();
  return 0;
}
