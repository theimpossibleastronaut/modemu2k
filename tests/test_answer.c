/* Answer-side (ATA/RING/S0) tests — line-side listener bound by
   m2k_setup_answer; incoming TCP connections are "calls". */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

static char outbuf[8192];
static size_t outlen;

static void
out_reset(void)
{
  outlen = 0;
  outbuf[0] = '\0';
}

static void
step_once(m2k_t *ctx)
{
  struct pollfd fds[M2K_MAX_POLLFDS];
  size_t nfds = M2K_MAX_POLLFDS;
  int timeout_ms;
  assert(m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms) == M2K_OK);
  if (timeout_ms < 0 || timeout_ms > 100) timeout_ms = 100;
  if (nfds > 0)
    poll(fds, nfds, timeout_ms);
  assert(m2k_step(ctx, fds, nfds) == M2K_OK);
  size_t len = 0;
  m2k_read_to_app(ctx, outbuf + outlen, sizeof outbuf - outlen - 1, &len);
  outlen += len;
  outbuf[outlen] = '\0';
}

static void
push_line(m2k_t *ctx, const char *line)
{
  char buf[128];
  int n = snprintf(buf, sizeof buf, "%s\r", line);
  assert(n > 0 && (size_t) n < sizeof buf);
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, buf, (size_t) n, &consumed) == M2K_OK);
  assert(consumed == (size_t) n);
}

static m2k_t *
new_answer_ctx(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  assert(m2k_atcmd(ctx, "AT%R1") == M2K_OK); /* raw: no IAC noise in bridge checks */
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);
  out_reset();
  return ctx;
}

static int
connect_client(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) port);
  assert(connect(fd, (struct sockaddr *) &addr, sizeof addr) == 0);
  return fd;
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

static void
test_ata_lexes(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[256] = "";
  m2k_set_error_buffer(ctx, errbuf, sizeof errbuf);
  /* ATA is a stepping-loop command, not actionable synchronously —
     same contract as ATD/ATO in m2k_atcmd(). */
  assert(m2k_atcmd(ctx, "ATA") == M2K_ERR_AT);
  assert(strstr(errbuf, "ATA is not actionable") != NULL);
  /* Garbage after AT still errors (lexer catch-all intact). */
  assert(m2k_atcmd(ctx, "ATY") == M2K_ERR_AT);
  m2k_free(ctx);
}

static void
test_ata_no_listener_errors(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 10 && strstr(outbuf, "ERROR") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "ERROR") != NULL);
  assert(!m2k_is_online(ctx));
  m2k_free(ctx);
}

static void
test_ata_accepts_pending_caller(void)
{
  m2k_t *ctx = new_answer_ctx();
  int client = connect_client(answer_port(ctx));

  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(m2k_has_carrier(ctx));
  assert(strstr(outbuf, "CONNECT") != NULL);

  /* Bytes bridge both ways (raw mode, no telnet negotiation). */
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "hello", 5, &consumed) == M2K_OK);
  for (int i = 0; i < 20; i++)
    step_once(ctx);
  char rbuf[16] = "";
  assert(recv(client, rbuf, sizeof rbuf - 1, 0) == 5);
  assert(memcmp(rbuf, "hello", 5) == 0);

  assert(send(client, "world", 5, 0) == 5);
  out_reset();
  for (int i = 0; i < 20 && strstr(outbuf, "world") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "world") != NULL);

  /* Answer listener survived the accept. */
  assert(m2k_get_answer_fd(ctx) >= 0);

  close(client);
  m2k_free(ctx);
}

static void
test_ata_no_caller_s7_zero_nocarrier(void)
{
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS7=0") == M2K_OK);
  push_line(ctx, "ATA");
  for (int i = 0; i < 10 && strstr(outbuf, "NO CARRIER") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "NO CARRIER") != NULL);
  assert(!m2k_is_online(ctx));
  assert(m2k_get_answer_fd(ctx) >= 0);
  m2k_free(ctx);
}

static void
test_ata_s7_timeout_nocarrier(void)
{
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS7=1") == M2K_OK);
  push_line(ctx, "ATA");
  /* ~2 s of stepping at <=100 ms per step covers the 1 s window. */
  for (int i = 0; i < 40 && strstr(outbuf, "NO CARRIER") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "NO CARRIER") != NULL);
  assert(!m2k_is_online(ctx));
  /* Back in CMD: a caller arriving now can still be answered. */
  int client = connect_client(answer_port(ctx));
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  close(client);
  m2k_free(ctx);
}

static void
test_second_call_after_hangup(void)
{
  m2k_t *ctx = new_answer_ctx();
  int c1 = connect_client(answer_port(ctx));
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));

  /* Peer disconnect -> NO CARRIER -> CMD. */
  close(c1);
  out_reset();
  for (int i = 0; i < 50 && m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(!m2k_is_online(ctx));
  assert(strstr(outbuf, "NO CARRIER") != NULL);

  /* Second caller, second ATA, no re-bind. */
  int c2 = connect_client(answer_port(ctx));
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(strstr(outbuf, "CONNECT") != NULL);
  close(c2);
  m2k_free(ctx);
}

static void
test_ring_emitted_for_pending_caller(void)
{
  m2k_t *ctx = new_answer_ctx();
  int client = connect_client(answer_port(ctx));

  for (int i = 0; i < 50 && strstr(outbuf, "RING") == NULL; i++)
    step_once(ctx);
  assert(strstr(outbuf, "RING") != NULL);
  assert(!m2k_is_online(ctx)); /* S0=0: never auto-answers */
  assert(ctx->atcmd.s[1] == 1); /* one ring so far (cadence is 6 s) */

  /* Explicit ATA answers the ringing call. */
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(ctx->atcmd.s[1] == 0); /* counter rests once answered */

  close(client);
  m2k_free(ctx);
}

static void
test_s0_auto_answer(void)
{
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS0=1") == M2K_OK);
  int client = connect_client(answer_port(ctx));

  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  assert(strstr(outbuf, "RING") != NULL);
  assert(strstr(outbuf, "CONNECT") != NULL);

  close(client);
  m2k_free(ctx);
}

static void
test_ath_keeps_listener(void)
{
  m2k_t *ctx = new_answer_ctx();
  int client = connect_client(answer_port(ctx));
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));

  /* +++ escape back to CMD, then ATH hangs up the call only. */
  assert(m2k_escape(ctx) == M2K_OK);
  for (int i = 0; i < 10 && m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(!m2k_is_online(ctx));
  push_line(ctx, "ATH");
  for (int i = 0; i < 10 && m2k_has_carrier(ctx); i++)
    step_once(ctx);
  assert(!m2k_has_carrier(ctx));
  assert(m2k_get_answer_fd(ctx) >= 0);

  close(client);
  m2k_free(ctx);
}

static void
test_aborted_caller_does_not_hang(void)
{
  /* A caller that RSTs before ATA is silently removed from the accept
     queue on Linux; a blocking accept() would then hang m2k_step. The
     answer listener is non-blocking so the step loop must stay prompt
     and resolve to NO CARRIER (or answer a real caller) either way. */
  m2k_t *ctx = new_answer_ctx();
  assert(m2k_atcmd(ctx, "ATS7=1") == M2K_OK);

  int client = connect_client(answer_port(ctx));
  struct linger lg = {1, 0};
  assert(setsockopt(client, SOL_SOCKET, SO_LINGER, &lg, sizeof lg) == 0);
  close(client); /* RST */
  usleep(100 * 1000);

  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  push_line(ctx, "ATA");
  for (int i = 0; i < 40 && strstr(outbuf, "NO CARRIER") == NULL; i++)
    step_once(ctx);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
  assert(elapsed < 3.0); /* never wedged in a blocking accept() */
  assert(strstr(outbuf, "NO CARRIER") != NULL);
  assert(!m2k_is_online(ctx));

  /* Listener still healthy for a well-behaved caller. */
  int c2 = connect_client(answer_port(ctx));
  out_reset();
  push_line(ctx, "ATA");
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    step_once(ctx);
  assert(m2k_is_online(ctx));
  close(c2);
  m2k_free(ctx);
}

int
main(void)
{
  test_setup_answer_binds();
  test_ata_lexes();
  test_ata_no_listener_errors();
  test_ata_accepts_pending_caller();
  test_ata_no_caller_s7_zero_nocarrier();
  test_ata_s7_timeout_nocarrier();
  test_second_call_after_hangup();
  test_ring_emitted_for_pending_caller();
  test_s0_auto_answer();
  test_ath_keeps_listener();
  test_aborted_caller_does_not_hang();
  return 0;
}
