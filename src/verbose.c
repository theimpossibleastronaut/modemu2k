
#include <stdarg.h>             /*va_list */
#include "modemu2k.h"       /*->atcmd.h (uchar)*/
#include "m2k_ctx.h"

void
verboseOut(m2k_t *ctx, int mask, const char *format, ...)
{
  va_list vl;

  if (!(ctx->atcmd.pv & mask))
    return;
  va_start(vl, format);
  vfprintf(stderr, format, vl);
  va_end(vl);
}

void
verbosePerror(m2k_t *ctx, int mask, const char *s)
{
  if (!(ctx->atcmd.pv & mask))
    return;
  perror(s);
}
