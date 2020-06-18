#if 0
#include <sys/time.h>   /*->ttybuf.h (timeval)*/
#include "defs.h"       /*->ttybuf.h (uchar,SOCKBUFR_SIZE,TTYBUFR_SIZE)*/
#endif

struct st_tty
{
  int rfd;
  int wfd;
};

extern struct st_tty tty;


/* reading tty */

struct st_ttyBufR
{
  uchar buf[TTYBUFR_SIZE];
  uchar *ptr;
  uchar *end;
  struct timeval newT;
  struct timeval prevT;
};

extern struct st_ttyBufR ttyBufR;

void
ttyBufRReset(void);

int
getTty1(void);

void ttyBufRead (void);


/* writing tty */

#define TTYBUFW_SIZE (2 * SOCKBUFR_SIZE)        /* this seems to be any number */
#define TTYBUFW_SIZE_A (TTYBUFW_SIZE + SOCKBUFR_SIZE)   /* important */

struct st_ttyBufW
{
  uchar buf[TTYBUFW_SIZE_A];
  uchar *top;
  uchar *ptr;
  int stop;
};

extern struct st_ttyBufW ttyBufW;

#define ttyBufWReset() { \
    ttyBufW.ptr = ttyBufW.top = ttyBufW.buf; \
    ttyBufW.stop = 0; \
}
#define ttyBufWHasData() (ttyBufW.ptr > ttyBufW.buf)
#define ttyBufWReady() (!ttyBufW.stop)
#define putTtyStr(s) putTtyN(s, sizeof(s)-1)

void ttyBufWrite (void);
void putTty1 (uchar c);
void putTtyN (const char *cp, int n);
