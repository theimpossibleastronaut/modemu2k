#include "test.h"
#include <stdio.h>
#include <string.h>

static int
contains(const char *hay, const char *needle)
{
  return strstr(hay, needle) != NULL;
}

static void
test_atcmd_rejected(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  assert(m2k_atcmd(ctx, "AT~not~a~real~cmd") == M2K_ERR_AT);
  assert(contains(errbuf, "rejected"));
  assert(contains(errbuf, "AT~not~a~real~cmd"));

  m2k_free(ctx);
}

static void
test_atcmd_action_verbs_route_to_at_error(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  /* ATD and ATO are lexable but not actionable from m2k_atcmd —
     should return M2K_ERR_AT (not M2K_ERR_BUG) with a helpful nudge
     toward the right entry point. */
  assert(m2k_atcmd(ctx, "ATD\"bbs 23\"") == M2K_ERR_AT);
  assert(contains(errbuf, "m2k_dial"));

  assert(m2k_atcmd(ctx, "ATO") == M2K_ERR_AT);
  assert(contains(errbuf, "m2k_online"));

  m2k_free(ctx);
}

static void
test_write_from_app_wrong_mode(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  /* No m2k_setup_app_io() call — the function should refuse and explain. */
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, "x", 1, &consumed) == M2K_ERR_PTY);
  assert(consumed == 0);
  assert(contains(errbuf, "app-I/O"));

  m2k_free(ctx);
}

static void
test_listen_accept_without_listener(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  assert(m2k_listen_accept(ctx) == M2K_ERR_SOCKET);
  assert(contains(errbuf, "no listener"));

  m2k_free(ctx);
}

static void
test_dial_bad_host(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  /* RFC 6761: ".invalid" is guaranteed never to resolve. */
  assert(m2k_dial(ctx, "nonexistent.invalid", "12345") == M2K_ERR_SOCKET);
  assert(contains(errbuf, "nonexistent.invalid"));

  m2k_free(ctx);
}

static void
test_buffer_unchanged_on_success(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[M2K_ERROR_BUFFER_SIZE];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  /* Trigger an error first so the buffer has known content. */
  assert(m2k_atcmd(ctx, "AT~bad") == M2K_ERR_AT);
  assert(strlen(errbuf) > 0);
  char snapshot[M2K_ERROR_BUFFER_SIZE];
  strcpy(snapshot, errbuf);

  /* A successful call must not touch the buffer. */
  assert(m2k_atcmd(ctx, "ATS7=20") == M2K_OK);
  assert(strcmp(errbuf, snapshot) == 0);

  m2k_free(ctx);
}

static void
test_truncates_to_buffer(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char errbuf[16];
  m2k_set_error_buffer(ctx, errbuf, sizeof(errbuf));

  assert(m2k_atcmd(ctx, "AT~longlonglonglongcommand") == M2K_ERR_AT);
  /* Buffer must be NUL-terminated within capacity. */
  size_t len = strnlen(errbuf, sizeof(errbuf));
  assert(len < sizeof(errbuf));

  m2k_free(ctx);
}

/* Verify m2k_strerror's table stays aligned with the m2k_err_t enum.
   A previous reorder of the enum without an accompanying update to the
   string table caused M2K_ERR_BUG to render as "Buffer full" and vice
   versa — this test pins the mapping. */
static void
test_strerror_mapping(void)
{
  assert(strcmp(m2k_strerror(M2K_OK), "Success") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_NOMEM), "Out of memory") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_PTY), "PTY error") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_SOCKET), "Socket error") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_TIMEOUT), "Connection timed out") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_CANCELED), "Operation canceled") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_BUG), "Internal bug") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_WOULDBLOCK),
                "Operation would block; retry later") == 0);
  assert(strcmp(m2k_strerror(M2K_ERR_AT), "AT command rejected") == 0);
}

static int log_call_count;
static char log_last_msg[256];

static void
test_log_callback(const char *msg, void *userdata)
{
  log_call_count++;
  if (userdata)
    *(int *)userdata = 1;
  snprintf(log_last_msg, sizeof(log_last_msg), "%s", msg);
}

static void
test_set_log_fn(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  int flag = 0;
  log_call_count = 0;
  log_last_msg[0] = '\0';

  m2k_set_log_fn(ctx, test_log_callback, &flag);

  /* Trigger an error path that emits a log message. */
  assert(m2k_atcmd(ctx, "AT~rejected~thing") == M2K_ERR_AT);
  assert(log_call_count > 0);
  assert(flag == 1);
  assert(strstr(log_last_msg, "rejected") != NULL);

  m2k_free(ctx);
}

static void
test_initial_state_predicates(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  /* Fresh ctx: not done, not online, no carrier. */
  assert(!m2k_run_done(ctx));
  assert(!m2k_is_online(ctx));
  assert(!m2k_has_carrier(ctx));
  /* No listener until m2k_setup_listen. */
  assert(m2k_get_listen_fd(ctx) == -1);
  /* DTR/RTS default asserted. */
  assert(m2k_get_dtr(ctx) == 1);
  assert(m2k_get_rts(ctx) == 1);
  m2k_free(ctx);
}

static void
test_dtr_rts_tracking(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);

  m2k_set_dtr(ctx, 0);
  assert(m2k_get_dtr(ctx) == 0);
  m2k_set_dtr(ctx, 1);
  assert(m2k_get_dtr(ctx) == 1);
  /* Any non-zero asserts. */
  m2k_set_dtr(ctx, 42);
  assert(m2k_get_dtr(ctx) == 1);

  m2k_set_rts(ctx, 0);
  assert(m2k_get_rts(ctx) == 0);
  m2k_set_rts(ctx, 1);
  assert(m2k_get_rts(ctx) == 1);

  /* DTR drop with no carrier is a no-op (no crash, state machine
     untouched). */
  assert(!m2k_has_carrier(ctx));
  m2k_set_dtr(ctx, 0);
  assert(!m2k_has_carrier(ctx));
  assert(!m2k_run_done(ctx));

  m2k_free(ctx);
}

static void
test_version(void)
{
  /* Runtime string must match the compile-time macro, byte-for-byte. */
  const char *v = m2k_version();
  assert(v);
  assert(strcmp(v, M2K_VERSION) == 0);
  /* Sanity: parses as MAJOR.MINOR.PATCH integers. */
  int maj = -1, min = -1, pat = -1;
  assert(sscanf(v, "%d.%d.%d", &maj, &min, &pat) == 3);
  assert(maj == M2K_VERSION_MAJOR);
  assert(min == M2K_VERSION_MINOR);
  assert(pat == M2K_VERSION_PATCH);
}

int
main(void)
{
  test_atcmd_rejected();
  test_atcmd_action_verbs_route_to_at_error();
  test_write_from_app_wrong_mode();
  test_listen_accept_without_listener();
  test_dial_bad_host();
  test_buffer_unchanged_on_success();
  test_truncates_to_buffer();
  test_strerror_mapping();
  test_set_log_fn();
  test_initial_state_predicates();
  test_dtr_rts_tracking();
  test_version();
  return 0;
}
