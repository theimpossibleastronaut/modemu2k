
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "modemu2k.h"
#include "m2k_ctx.h"

static void
m2k_vlog(m2k_t *ctx, const char *fmt, va_list ap)
{
  char buf[512];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  if (ctx && ctx->log_fn)
    ctx->log_fn(buf, ctx->log_userdata);
  else
    fputs(buf, stderr);
}

void
m2k_log(m2k_t *ctx, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  m2k_vlog(ctx, fmt, ap);
  va_end(ap);
}

void
verboseOut(m2k_t *ctx, int mask, const char *format, ...)
{
  if (!(ctx->atcmd.pv & mask))
    return;
  va_list ap;
  va_start(ap, format);
  m2k_vlog(ctx, format, ap);
  va_end(ap);
}

void
verbosePerror(m2k_t *ctx, int mask, const char *s)
{
  if (!(ctx->atcmd.pv & mask))
    return;
  m2k_log(ctx, "%s: %s\n", s, strerror(errno));
}
