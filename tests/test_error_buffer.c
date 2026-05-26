#include "test.h"
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

int
main(void)
{
  test_atcmd_rejected();
  test_write_from_app_wrong_mode();
  test_listen_accept_without_listener();
  test_dial_bad_host();
  test_buffer_unchanged_on_success();
  test_truncates_to_buffer();
  return 0;
}
