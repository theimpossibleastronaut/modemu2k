#pragma once

/**
 * @file modemu2k.h
 * @brief Public API for the modemu2k PTY-based modem emulator library.
 *
 * ## Typical usage
 *
 * The pattern used by the bundled `modemu2k` executable (see `src/main.c`)
 * shows the recommended way to use the library:
 *
 * @code{.c}
 *   m2k_t *ctx = m2k_new();
 *   if (!ctx) return EXIT_FAILURE;
 *
 *   // Choose one setup mode:
 *   m2k_setup_stdin(ctx);                              // read/write stdin/stdout
 *   // m2k_setup_comm_program(ctx, "minicom -p %s");   // fork a comm program on a PTY
 *   // m2k_setup_listen(ctx, "5000");                  // bind a TCP port, then
 *   // m2k_listen_accept(ctx);                         //   wait for a client to connect
 *
 *   // Optional: apply AT commands before entering the command loop
 *   m2k_atcmd(ctx, "ATS7=30");
 *
 *   // Run the full command/online state machine (blocks until session ends)
 *   m2k_run(ctx);
 *
 *   m2k_free(ctx);
 * @endcode
 *
 * For finer control — for example, dialling programmatically and entering
 * online mode without the AT command loop — use m2k_dial(), m2k_online(),
 * and m2k_hangup() directly.
 *
 * @example main.c
 * The full source of the bundled `modemu2k` executable.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque modem emulator context. Create with m2k_new(), destroy with m2k_free(). */
typedef struct m2k_s m2k_t;

/**
 * @brief Log callback type.
 * @param msg      NUL-terminated log message.
 * @param userdata Caller-supplied pointer passed unchanged from m2k_set_log_fn().
 */
typedef void (*m2k_log_fn)(const char *msg, void *userdata);

/**
 * @brief Return codes used by all m2k_* functions.
 */
typedef enum {
  M2K_OK = 0,       /**< Success. */
  M2K_ERR_NOMEM,    /**< Memory allocation failed. */
  M2K_ERR_PTY,      /**< PTY open or allocation failed. */
  M2K_ERR_SOCKET,   /**< TCP connect or I/O error. */
  M2K_ERR_TIMEOUT,  /**< Operation timed out. */
  M2K_ERR_CANCELED, /**< Operation canceled (e.g., +++ escape sequence). */
  M2K_ERR_BUG,      /**< Internal assertion failure — should not happen. */
} m2k_err_t;

/**
 * @brief Allocate and initialise a new modem context.
 * @return Newly allocated context, or NULL on memory exhaustion.
 */
m2k_t      *m2k_new(void);

/**
 * @brief Release all resources held by @p ctx.
 * @param ctx Context to destroy. Safe to call with NULL.
 */
void        m2k_free(m2k_t *ctx);

/**
 * @brief Install a log callback.
 *
 * When @p fn is non-NULL, all diagnostic messages are routed to it instead of
 * stderr.  Pass @p fn = NULL to restore the default stderr output.
 *
 * @param ctx      Modem context.
 * @param fn       Callback to receive log messages, or NULL.
 * @param userdata Opaque pointer forwarded to every @p fn invocation.
 *
 * @snippet examples/m2k_set_log_fn.c set_log_fn-callback
 *
 * @snippet examples/m2k_set_log_fn.c set_log_fn-install
 */
void        m2k_set_log_fn(m2k_t *ctx, m2k_log_fn fn, void *userdata);

/**
 * @brief Feed a Hayes AT command string to the modem.
 * @param ctx Modem context.
 * @param cmd NUL-terminated AT command (e.g. @c "ATZ" or @c "ATS0=1").
 * @return M2K_OK on success.
 *
 * @code
 * m2k_atcmd(ctx, "ATZ");        // reset to defaults
 * m2k_atcmd(ctx, "ATS0=1");    // auto-answer on first ring
 * @endcode
 */
m2k_err_t   m2k_atcmd(m2k_t *ctx, const char *cmd);

/**
 * @brief Open a TCP connection to @p host : @p port.
 *
 * Transitions the modem from idle to connected state.  Call m2k_online() after
 * this to start relaying data.
 *
 * @param ctx  Modem context.
 * @param host Hostname or numeric IP address.
 * @param port Service name or decimal port number.
 * @return M2K_OK on success, M2K_ERR_SOCKET on failure.
 *
 * @snippet tests/test_connect.c dial
 */
m2k_err_t   m2k_dial(m2k_t *ctx, const char *host, const char *port);

/**
 * @brief Enter online mode and relay data between the PTY and the socket.
 *
 * Blocks until the connection closes or the @c +++ escape sequence is detected.
 * Returns M2K_ERR_CANCELED when the caller escapes back to command mode.
 *
 * @param ctx Modem context (must have an active connection from m2k_dial()).
 * @return M2K_OK when the remote end closes, M2K_ERR_CANCELED on +++ escape.
 *
 * @code
 * // Reconnect loop: re-enter online mode if the user escapes with +++
 * m2k_err_t st;
 * do {
 *     st = m2k_online(ctx);
 * } while (st == M2K_ERR_CANCELED);
 * m2k_hangup(ctx);
 * @endcode
 */
m2k_err_t   m2k_online(m2k_t *ctx);

/**
 * @brief Tear down the active TCP connection.
 * @param ctx Modem context.
 * @return M2K_OK on success.
 */
m2k_err_t   m2k_hangup(m2k_t *ctx);

/**
 * @brief Return a human-readable string for @p err.
 * @param err Error code.
 * @return Static string; never NULL.
 */
const char *m2k_strerror(m2k_err_t err);

/**
 * @brief Use stdin/stdout as the TTY (standalone mode).
 *
 * Puts the terminal into raw mode.  Call before m2k_run().
 *
 * @param ctx Modem context.
 * @return M2K_OK.
 */
m2k_err_t   m2k_setup_stdin(m2k_t *ctx);

/**
 * @brief Allocate a PTY master and return the slave device path.
 *
 * The returned string is heap-allocated; the caller must free() it.
 * Use this when you need the slave path (e.g. to print it with --show).
 * To fork a comm program on the slave, prefer m2k_setup_comm_program().
 *
 * @param ctx       Modem context.
 * @param slave_out Receives a newly allocated NUL-terminated slave path.
 * @return M2K_OK on success, M2K_ERR_PTY on failure.
 */
m2k_err_t   m2k_setup_pty(m2k_t *ctx, char **slave_out);

/**
 * @brief Allocate a PTY and fork/exec a comm program on the slave.
 *
 * The slave device path is substituted for @c %s in @p cmd.
 * Call before m2k_run().
 *
 * @param ctx Modem context.
 * @param cmd Shell command; @c %s is replaced by the slave device path.
 * @return M2K_OK on success, M2K_ERR_PTY or M2K_ERR_NOMEM on failure.
 *
 * @snippet examples/m2k_setup_comm_program.c setup_comm_program
 */
m2k_err_t   m2k_setup_comm_program(m2k_t *ctx, const char *cmd);

/**
 * @brief Open an existing PTY device as the TTY.
 *
 * @param ctx Modem context.
 * @param dev Path to the PTY master device (e.g. @c "/dev/ptyp0").
 * @return M2K_OK on success, M2K_ERR_PTY if the device cannot be opened.
 */
m2k_err_t   m2k_setup_dev(m2k_t *ctx, const char *dev);

/**
 * @brief Bind a TCP listening socket on @p port.
 *
 * Binds and listens on all interfaces (dual-stack IPv4/IPv6); returns
 * immediately without waiting for a connection. Pair with
 * m2k_listen_accept() to accept one connection and use it as the TTY,
 * or integrate the listener fd with the caller's own event loop.
 *
 * Suitable for use as a virtual-modem backend for programs that connect
 * to modemu2k over TCP, such as dosemu2.
 *
 * @param ctx  Modem context.
 * @param port Service name or decimal port number to listen on.
 * @return M2K_OK on success, M2K_ERR_SOCKET on failure.
 *
 * @snippet examples/m2k_setup_listen.c setup_listen
 */
m2k_err_t   m2k_setup_listen(m2k_t *ctx, const char *port);

/**
 * @brief Accept a single incoming connection on the listening socket
 *        opened by m2k_setup_listen() and adopt it as the TTY.
 *
 * Blocks until a client connects. The listening socket is closed once
 * a connection is accepted (modemu2k handles one client per session).
 * Call before m2k_run().
 *
 * @param ctx Modem context (must have an active listener from
 *            m2k_setup_listen()).
 * @return M2K_OK on success, M2K_ERR_SOCKET on accept failure or if no
 *         listener has been set up.
 */
m2k_err_t   m2k_listen_accept(m2k_t *ctx);

/**
 * @brief Run the modem command/online loop until the PTY closes.
 *
 * Handles the full state machine: reads Hayes AT commands in command mode,
 * dials on ATD, relays data in online mode, and returns to command mode on
 * +++ escape or disconnection.  Call one of the m2k_setup_*() functions
 * before calling m2k_run().
 *
 * Equivalent to:
 * @code
 *   while (!m2k_run_done(ctx)) {
 *     struct pollfd fds[M2K_MAX_POLLFDS];
 *     size_t nfds = M2K_MAX_POLLFDS;
 *     int timeout_ms;
 *     m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms);
 *     poll(fds, nfds, timeout_ms);
 *     m2k_step(ctx, fds, nfds);
 *   }
 * @endcode
 *
 * @param ctx Modem context.
 * @return M2K_OK when the session ends normally.
 */
m2k_err_t   m2k_run(m2k_t *ctx);

/* ── Steppable event-loop API ───────────────────────────────────────
 *
 * The functions below let a host application integrate modemu2k into
 * its own event loop (poll/epoll/select/etc.) instead of handing the
 * fds to the blocking m2k_run() above.
 *
 * Usage:
 *   while (!m2k_run_done(ctx)) {
 *       struct pollfd fds[M2K_MAX_POLLFDS];   // caller-provided buffer
 *       size_t nfds = M2K_MAX_POLLFDS;
 *       int timeout_ms;
 *       m2k_get_pollfds(ctx, fds, &nfds, &timeout_ms);
 *       // Splice fds into your own pollset, call poll()/epoll_wait().
 *       // On return, write back the .revents the OS reported into the
 *       // corresponding entries of `fds`.
 *       m2k_step(ctx, fds, nfds);
 *   }
 *
 * Dialing (ATD) is still synchronous inside m2k_step() in this phase —
 * the connect() call will block your event loop briefly. That will be
 * addressed in a later, non-blocking-dial pass.
 */

#include <poll.h>     /* struct pollfd */
#include <stddef.h>   /* size_t */

/** Maximum number of pollfds modemu2k will ever ask the caller to watch. */
#define M2K_MAX_POLLFDS 3

/**
 * @brief Describe the fds and timeout the caller's event loop should watch.
 *
 * Fills @p fds with the fds modemu2k currently wants to monitor and stores
 * the longest acceptable wait in @p *timeout_ms (-1 = wait indefinitely,
 * 0 = poll immediately). Each pollfd's `.fd` and `.events` are set; the
 * caller is responsible for the `.revents` field after calling poll().
 *
 * @param ctx        Modem context.
 * @param fds        Caller-provided array of at least @ref M2K_MAX_POLLFDS entries.
 * @param nfds_inout In:  capacity of @p fds (must be >= @ref M2K_MAX_POLLFDS).
 *                   Out: number of entries actually populated. Zero means
 *                        the session is done; the caller should exit the loop.
 * @param timeout_ms Out: maximum wait in milliseconds before m2k_step() needs
 *                   to be called again. -1 means no deadline.
 * @return M2K_OK on success, M2K_ERR_BUG if @p *nfds_inout is too small.
 */
m2k_err_t   m2k_get_pollfds(m2k_t *ctx, struct pollfd *fds,
                            size_t *nfds_inout, int *timeout_ms);

/**
 * @brief Run one non-blocking iteration of the state machine.
 *
 * Consumes the events the caller's poll() reported (via the `.revents`
 * fields of @p fds) and advances the cmd-mode / online-mode state once.
 * Does no poll() of its own. Safe to call with all `.revents == 0`
 * (the call is then a no-op).
 *
 * @param ctx  Modem context.
 * @param fds  The pollfd array previously filled by m2k_get_pollfds(),
 *             with the OS's `.revents` values populated by the caller.
 * @param nfds Number of entries in @p fds (the value m2k_get_pollfds
 *             returned in @p *nfds_inout).
 * @return M2K_OK on success.
 */
m2k_err_t   m2k_step(m2k_t *ctx, struct pollfd *fds, size_t nfds);

/**
 * @brief Test whether the session has ended.
 * @param ctx Modem context.
 * @return Nonzero when the state machine has reached a terminal state
 *         (PTY closed, etc.) and the caller's loop should stop.
 */
int         m2k_run_done(const m2k_t *ctx);

#ifdef __cplusplus
}
#endif
