#pragma once
#include "m2k_private.h"

struct m2k_s
{
  Atcmd atcmd;
  Atcmd atcmdNV;
  struct
  {
    st_sock conn;
    struct st_sockBufR bufR;
    struct st_sockBufW bufW;
    int listen_fd;            /* Bound listener from m2k_setup_listen,
                                   consumed by m2k_listen_accept; -1 when
                                   no listener is open. */
  } sock;
  struct
  {
    int rfd, wfd;             /* absorbed st_tty */
    bool owned;               /* true when rfd/wfd is a library-opened fd
                                   (PTY master, dev, or accepted client)
                                   that m2k_free() must close; false for
                                   stdin (0/1, caller-owned) and app-IO.
                                   rfd == wfd in every owned case, so one
                                   close suffices. */
    struct st_ttyBufR bufR;
    struct st_ttyBufW bufW;
  } tty;
  struct st_telOpt telOpt;
  struct
  {
    m2k_log_fn fn;
    void *userdata;
    char *err_buf;            /* m2k_set_error_buffer() — caller-owned. */
    size_t err_buf_size;
    bool force_verbose;       /* m2k_set_force_verbose() — bypasses the
                                   AT%V mask in verboseOut/verbosePerror so
                                   ATZ (which resets atcmd.pv via the
                                   atcmdNV copy) can't silence the host. */
  } log;
  char slave_path[64];           /* PTY slave path filled by
                                           m2k_setup_pty/m2k_setup_comm_program;
                                           lifetime = ctx. */
  struct
  {
    int fd;                   /* Line-side listener from m2k_setup_answer
                                   (ATA/RING answer path); -1 when unbound.
                                   Survives hangup; closed by m2k_free(). */
    struct timeval deadline;  /* S7 wait window for M2K_STATE_ANSWER. */
    struct timeval ring_next; /* Next RING emission while a caller waits. */
  } answer;
  struct
  {
    m2k_step_state state;     /* Steppable event-loop state machine. */
    struct m2k_cmdbuf cmdbuf; /* Per-step persistent cmd-mode line buffer. */
    bool escape_req;          /* m2k_escape() — drives one online→cmd
                                   transition on the next m2k_step(). */
    bool quit_req;            /* atcmdPQ (at%q) — transition to DONE once
                                   the TTY write buffer drains. */
    bool intr_armed;          /* First Ctrl-C in CMD mode arms; a second
                                   with no intervening keystroke sets
                                   quit_req. Cleared by any other byte. */
    bool app_io;              /* m2k_setup_app_io() flag — host supplies/
                                   drains TTY bytes via m2k_{write_from,
                                   read_to}_app() instead of read()/write()
                                   on tty.{rfd,wfd}. */
  } step;
  struct
  {
    bool dtr, rts;            /* m2k_set_dtr()/m2k_set_rts() — DTE control
                                   lines. */
  } ctrl;
  struct
  {
    struct addrinfo *result;  /* getaddrinfo() head during non-blocking
                                   dial (M2K_STATE_DIAL); freed when the
                                   dial resolves or is aborted. */
    struct timeval deadline;
  } dial;
  struct
  {
    int state, cmd, opt;      /* sockReadLoop's telnet IAC stream parser
                                   (SRL_* states in m2k_api.c). */
  } srl;
  struct m2k_escseq escSeq;   /* +++ escape detector (online mode). */
  struct m2k_linebuf lineBuf; /* Line buffer for non-SGA telnet mode. */
};
