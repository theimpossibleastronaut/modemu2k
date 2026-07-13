/* m2k_describe_state() — content in known states + snprintf contract. */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include "test_helpers.h"
#include <poll.h>
#include <stdio.h>
#include <string.h>

static int
answer_port(m2k_t *ctx)
{
  return test_local_port(m2k_get_answer_fd(ctx));
}

static void
test_cmd_state_content(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char buf[512];
  int n = m2k_describe_state(ctx, buf, sizeof buf);
  assert(n > 0 && (size_t) n < sizeof buf);
  assert((size_t) n == strlen(buf));
  assert(strstr(buf, "state=CMD") != NULL);
  assert(strstr(buf, "carrier=no") != NULL);
  assert(strstr(buf, "sock: fd=-1") != NULL); /* sockInit's "none" is 0 */
  assert(strstr(buf, "answer_fd=-1") != NULL);
  assert(strstr(buf, "dial=idle") != NULL);
  /* four lines: exactly three newlines, none trailing */
  int nl = 0;
  for (char *p = buf; *p; p++) nl += (*p == '\n');
  assert(nl == 3);
  assert(buf[n - 1] != '\n');
  m2k_free(ctx);
}

static void
test_online_state_content(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  assert(m2k_setup_app_io(ctx) == M2K_OK);
  assert(m2k_setup_answer(ctx, "0") == M2K_OK);
  int client = socket(AF_INET, SOCK_STREAM, 0);
  assert(client >= 0);
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  addr.sin_port = htons((unsigned short) answer_port(ctx));
  assert(connect(client, (struct sockaddr *) &addr, sizeof addr) == 0);

  char line[] = "ATA\r";
  size_t consumed = 0;
  assert(m2k_write_from_app(ctx, line, sizeof line - 1, &consumed) == M2K_OK);
  for (int i = 0; i < 50 && !m2k_is_online(ctx); i++)
    test_step(ctx);
  assert(m2k_is_online(ctx));

  char buf[512];
  int n = m2k_describe_state(ctx, buf, sizeof buf);
  assert(n > 0);
  assert(strstr(buf, "state=ONLINE") != NULL);
  assert(strstr(buf, "carrier=yes") != NULL);
  assert(strstr(buf, "alive=yes") != NULL);
  assert(strstr(buf, "app_io=yes") != NULL);

  close(client);
  m2k_free(ctx);
}

static void
test_snprintf_contract(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  char full[512];
  int n = m2k_describe_state(ctx, full, sizeof full);
  assert(n > 0);
  /* size probe: cap == 0, buf NULL */
  assert(m2k_describe_state(ctx, NULL, 0) == n);
  /* truncation: NUL-terminated, same return */
  char tiny[8];
  memset(tiny, 'x', sizeof tiny);
  assert(m2k_describe_state(ctx, tiny, sizeof tiny) == n);
  assert(tiny[7] == '\0');
  assert(strncmp(tiny, full, 7) == 0);
  m2k_free(ctx);
}

int
main(void)
{
  test_cmd_state_content();
  test_online_state_content();
  test_snprintf_contract();
  return 0;
}
