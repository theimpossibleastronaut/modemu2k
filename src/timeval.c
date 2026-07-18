
#include <sys/time.h> /*->ttybuf.h (timeval)*/
#include "m2k_private.h"

/* a = b [10mS] */
void
timevalSet10ms(struct timeval *ap, int b)
{
  ap->tv_sec = b / 100;
  ap->tv_usec = (b % 100) * 10 * 1000;
}

/* a += b */
void
timevalAdd(struct timeval *ap, const struct timeval *bp)
{
  ap->tv_sec += bp->tv_sec;
  ap->tv_usec += bp->tv_usec;
  if (ap->tv_usec >= 1000 * 1000)
  {
    ap->tv_usec -= 1000 * 1000;
    ap->tv_sec += 1;
  }
}

/* a -= b */
void
timevalSub(struct timeval *ap, const struct timeval *bp)
{
  ap->tv_sec -= bp->tv_sec;
  ap->tv_usec -= bp->tv_usec;
  if (ap->tv_usec < 0)
  {
    ap->tv_usec += 1000 * 1000;
    ap->tv_sec -= 1;
  }
}

/* (a < b): -1, (a==b): 0, (a > b): 1 */
int
timevalCmp(const struct timeval *ap, const struct timeval *bp)
{
  if (ap->tv_sec < bp->tv_sec)
    return -1;
  if (ap->tv_sec > bp->tv_sec)
    return 1;
  if (ap->tv_usec < bp->tv_usec)
    return -1;
  if (ap->tv_usec > bp->tv_usec)
    return 1;
  return 0;
}

/* Milliseconds from now until *deadline, as a poll() timeout: 0 if the
   deadline has passed (poll returns immediately), otherwise at least 1 so
   a sub-millisecond remainder never rounds to a busy-wait 0. */
int
timevalRemainingMs(const struct timeval *deadline)
{
  struct timeval now, remaining;
  gettimeofday(&now, NULL);
  if (timevalCmp(&now, deadline) >= 0)
    return 0;
  remaining = *deadline;
  timevalSub(&remaining, &now);
  long ms = remaining.tv_sec * 1000L + remaining.tv_usec / 1000L;
  return ms <= 0 ? 1 : (int) ms;
}
