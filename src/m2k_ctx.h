#pragma once
#include "m2k_private.h"

struct m2k_s
{
  Atcmd atcmd;
  Atcmd atcmdNV;
  st_sock sock;
  struct st_sockBufR sockBufR;
  struct st_sockBufW sockBufW;
  struct st_tty tty;
  struct st_ttyBufR ttyBufR;
  struct st_ttyBufW ttyBufW;
  struct st_telOpt telOpt;
  m2k_log_fn log_fn;
  void *log_userdata;
  char *err_buf; /* m2k_set_error_buffer() — caller-owned. */
  size_t err_buf_size;
  char slave_path[64];           /* PTY slave path filled by
                                           m2k_setup_pty/m2k_setup_comm_program;
                                           lifetime = ctx. */
  int listen_fd;                 /* Bound listener from m2k_setup_listen,
                                       consumed by m2k_listen_accept; -1 when
                                       no listener is open. */
  m2k_step_state step_state;     /* Steppable event-loop state machine. */
  struct m2k_cmdbuf step_cmdbuf; /* Per-step persistent cmd-mode line buffer. */
  bool app_io;                   /* m2k_setup_app_io() flag — host supplies/
                                       drains TTY bytes via m2k_{write_from,
                                       read_to}_app() instead of read()/write()
                                       on tty.{rfd,wfd}. */
  bool escape_req;               /* m2k_escape() — drives one online→cmd
                                       transition on the next m2k_step(). */
  bool quit_req;                 /* atcmdPQ (at%q) — drives transition to
                                       M2K_STATE_DONE once ttyBufW drains. */
  bool intr_armed;               /* First Ctrl-C in CMD mode arms; a second
                                       Ctrl-C with no intervening keystroke
                                       sets quit_req. Cleared by any other
                                       input byte. */
  bool dtr;                      /* m2k_set_dtr() — DTE control line. */
  bool rts;                      /* m2k_set_rts() — DTE control line. */
  bool force_verbose;            /* m2k_set_force_verbose() — bypasses the
                                       AT%V mask in verboseOut/verbosePerror so
                                       ATZ (which resets atcmd.pv via the
                                       atcmdNV copy) can't silence the host. */
  struct addrinfo *dial_result;  /* getaddrinfo() head during non-blocking
                                       dial (M2K_STATE_DIAL); freed when the
                                       dial resolves or is aborted. */
  struct timeval dial_deadline;
  struct m2k_escseq escSeq;   /* +++ escape detector (online mode). */
  struct m2k_linebuf lineBuf; /* Line buffer for non-SGA telnet mode. */
};
