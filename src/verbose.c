
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "m2k_private.h"
#include "m2k_ctx.h"

static void
m2k_vlog(m2k_t *ctx, const char *fmt, va_list ap)
{
  char buf[512];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  if (ctx && ctx->log.fn)
    ctx->log.fn(buf, ctx->log.userdata);
  else
    fputs(buf, stderr);
}

void
m2k_log(m2k_t *ctx, m2k_log_level_t level, const char *fmt, ...)
{
  if (ctx && level > ctx->log.level)
    return;
  va_list ap;
  va_start(ap, fmt);
  m2k_vlog(ctx, fmt, ap);
  va_end(ap);
}

/* m2k_err_set: record a detailed message for the most recent error.
   Writes to ctx->log.err_buf (if installed via m2k_set_error_buffer) and
   ALSO emits through the log callback so existing log consumers still
   see it. Used at error-return sites instead of plain m2k_log; always
   ERROR level, which no threshold hides. */
void
m2k_err_set(m2k_t *ctx, const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (ctx && ctx->log.err_buf && ctx->log.err_buf_size)
  {
    size_t n = ctx->log.err_buf_size - 1;
    strncpy(ctx->log.err_buf, buf, n);
    ctx->log.err_buf[n] = '\0';
    /* Strip a trailing newline — error buffer is single-line; logs keep it. */
    size_t len = strlen(ctx->log.err_buf);
    if (len && ctx->log.err_buf[len - 1] == '\n')
      ctx->log.err_buf[len - 1] = '\0';
  }
  if (ctx && ctx->log.fn)
    ctx->log.fn(buf, ctx->log.userdata);
  else
    fputs(buf, stderr);
}

/* Minimum level threshold that opens a verbose category wholesale:
   BYTES traffic is TRACE; everything else (MISC, TELOPT) is DEBUG. */
static m2k_log_level_t
verbLevel(int mask)
{
  return (mask & VERB_BYTES) ? M2K_LOG_TRACE : M2K_LOG_DEBUG;
}

/* Deliver when either knob opens the gate: the Hayes-side AT%V category
   mask (or its host-side force_verbose bypass), or the host-side
   severity threshold from m2k_set_log_level(). */
static int
verbOpen(m2k_t *ctx, int mask)
{
  if (ctx->log.force_verbose || (ctx->atcmd.pv & mask))
    return 1;
  return ctx->log.level >= verbLevel(mask);
}

void
verboseOut(m2k_t *ctx, int mask, const char *format, ...)
{
  if (!verbOpen(ctx, mask))
    return;
  va_list ap;
  va_start(ap, format);
  m2k_vlog(ctx, format, ap);
  va_end(ap);
}

void
verbosePerror(m2k_t *ctx, int mask, const char *s)
{
  if (!verbOpen(ctx, mask))
    return;
  char buf[256];
  snprintf(buf, sizeof buf, "%s: %s\n", s, strerror(errno));
  if (ctx->log.fn)
    ctx->log.fn(buf, ctx->log.userdata);
  else
    fputs(buf, stderr);
}
