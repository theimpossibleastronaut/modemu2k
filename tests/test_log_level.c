/* m2k_set_log_level() severity filter — delivery matrix across levels,
   the AT%V category mask, and the force_verbose bypass. */

#include "test.h"
#include "m2k_private.h"
#include "m2k_ctx.h"
#include <string.h>

static char logbuf[1024];

static void
log_capture(const char *msg, void *ud)
{
  (void) ud;
  strncat(logbuf, msg, sizeof logbuf - strlen(logbuf) - 1);
}

static m2k_t *
capture_ctx(void)
{
  m2k_t *ctx = m2k_new();
  assert(ctx);
  m2k_set_log_fn(ctx, log_capture, NULL);
  logbuf[0] = '\0';
  return ctx;
}

static int
logged(const char *needle)
{
  int hit = strstr(logbuf, needle) != NULL;
  logbuf[0] = '\0';
  return hit;
}

static void
test_default_threshold_is_info(void)
{
  m2k_t *ctx = capture_ctx();
  assert(m2k_get_log_level(ctx) == M2K_LOG_INFO);

  m2k_log(ctx, M2K_LOG_ERROR, "e1\n");
  assert(logged("e1"));
  m2k_log(ctx, M2K_LOG_WARN, "w1\n");
  assert(logged("w1"));
  m2k_log(ctx, M2K_LOG_INFO, "i1\n");
  assert(logged("i1"));
  /* DEBUG/TRACE categories stay quiet by default. */
  verboseOut(ctx, VERB_MISC, "d1\n");
  assert(!logged("d1"));
  verboseOut(ctx, VERB_BYTES, "t1\n");
  assert(!logged("t1"));
  m2k_free(ctx);
}

static void
test_threshold_raises_and_lowers(void)
{
  m2k_t *ctx = capture_ctx();

  /* ERROR threshold hides INFO but not errors. */
  m2k_set_log_level(ctx, M2K_LOG_ERROR);
  m2k_log(ctx, M2K_LOG_INFO, "i2\n");
  assert(!logged("i2"));
  m2k_log(ctx, M2K_LOG_ERROR, "e2\n");
  assert(logged("e2"));
  /* m2k_err_set is error-path by construction: never hidden. */
  m2k_err_set(ctx, "err-set\n");
  assert(logged("err-set"));

  /* DEBUG opens MISC/TELOPT narration but not BYTES. */
  m2k_set_log_level(ctx, M2K_LOG_DEBUG);
  verboseOut(ctx, VERB_MISC, "d2\n");
  assert(logged("d2"));
  verboseOut(ctx, VERB_TELOPT, "d3\n");
  assert(logged("d3"));
  verboseOut(ctx, VERB_BYTES, "t2\n");
  assert(!logged("t2"));

  /* TRACE opens everything. */
  m2k_set_log_level(ctx, M2K_LOG_TRACE);
  verboseOut(ctx, VERB_BYTES, "t3\n");
  assert(logged("t3"));
  m2k_free(ctx);
}

static void
test_atv_mask_opens_categories_below_threshold(void)
{
  m2k_t *ctx = capture_ctx();
  /* Level INFO (default) hides DEBUG — but the Hayes-side AT%V mask
     opens its category anyway, exactly as before levels existed. */
  assert(m2k_atcmd(ctx, "AT%V2") == M2K_OK); /* TELOPT only */
  verboseOut(ctx, VERB_TELOPT, "d4\n");
  assert(logged("d4"));
  verboseOut(ctx, VERB_MISC, "d5\n");
  assert(!logged("d5"));
  m2k_free(ctx);
}

static void
test_force_verbose_bypasses_mask(void)
{
  m2k_t *ctx = capture_ctx();
  m2k_set_force_verbose(ctx, 1);
  verboseOut(ctx, VERB_MISC, "d6\n");
  assert(logged("d6"));
  /* Level threshold for tagged messages is independent of the flag. */
  m2k_set_log_level(ctx, M2K_LOG_ERROR);
  m2k_log(ctx, M2K_LOG_INFO, "i3\n");
  assert(!logged("i3"));
  m2k_free(ctx);
}

int
main(void)
{
  test_default_threshold_is_info();
  test_threshold_raises_and_lowers();
  test_atv_mask_opens_categories_below_threshold();
  test_force_verbose_bypasses_mask();
  return 0;
}
