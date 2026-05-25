#pragma once

/**
 * @file modemu2k.h
 * @brief Public API for the modemu2k PTY-based modem emulator library.
 *
 * Typical usage:
 * @code
 *   m2k_t *ctx = m2k_new();
 *   m2k_set_log_fn(ctx, my_logger, NULL);
 *   m2k_dial(ctx, "bbs.example.com", "23");
 *   m2k_online(ctx);   // blocks until connection closes or +++ escape
 *   m2k_free(ctx);
 * @endcode
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
 * @code
 * m2k_err_t err = m2k_dial(ctx, "bbs.example.com", "23");
 * if (err != M2K_OK) {
 *     fprintf(stderr, "%s\n", m2k_strerror(err));
 *     return 1;
 * }
 * @endcode
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
 * To fork a comm program on the slave, prefer m2k_setup_commx().
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
 */
m2k_err_t   m2k_setup_commx(m2k_t *ctx, const char *cmd);

/**
 * @brief Open an existing PTY device as the TTY.
 *
 * @param ctx Modem context.
 * @param dev Path to the PTY master device (e.g. @c "/dev/ptyp0").
 * @return M2K_OK on success, M2K_ERR_PTY if the device cannot be opened.
 */
m2k_err_t   m2k_setup_dev(m2k_t *ctx, const char *dev);

/**
 * @brief Listen for and accept a single incoming TCP connection on @p port.
 *
 * Binds to all interfaces on the given port, waits (blocking) for a client
 * to connect, then uses that connection as the TTY.  Suitable for use as a
 * plugin with programs such as dosemu2 that connect to modemu2k over TCP.
 * Call before m2k_run().
 *
 * @param ctx  Modem context.
 * @param port Service name or decimal port number to listen on.
 * @return M2K_OK on success, M2K_ERR_SOCKET on failure.
 */
m2k_err_t   m2k_setup_listen(m2k_t *ctx, const char *port);

/**
 * @brief Run the modem command/online loop until the PTY closes.
 *
 * Handles the full state machine: reads Hayes AT commands in command mode,
 * dials on ATD, relays data in online mode, and returns to command mode on
 * +++ escape or disconnection.  Call one of the m2k_setup_*() functions
 * before calling m2k_run().
 *
 * @param ctx Modem context.
 * @return M2K_OK when the session ends normally.
 */
m2k_err_t   m2k_run(m2k_t *ctx);

#ifdef __cplusplus
}
#endif
