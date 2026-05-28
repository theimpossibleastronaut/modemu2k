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

#include <poll.h>     /* struct pollfd, used by m2k_get_pollfds()/m2k_step() below */
#include <stddef.h>   /* size_t */

#include "modemu2k_version.h"  /* M2K_VERSION_MAJOR / _MINOR / _PATCH / M2K_VERSION */

/**
 * @brief Public-symbol visibility/export marker.
 *
 * On platforms where it matters (Windows DLLs, ELF builds with
 * `-fvisibility=hidden`) this macro decorates every documented public
 * function below. Defaults to nothing on Unix-default builds, where
 * symbols are visible already. Define M2K_BUILDING_DLL before including
 * this header inside the modemu2k source tree on Windows to flip the
 * dllimport/dllexport direction; consumers leave it undefined.
 *
 * Borrowed from libuv (UV_EXTERN) / libcurl (CURL_EXTERN) /
 * libssh2 (LIBSSH2_API).
 */
#ifndef M2K_API
# if defined(_WIN32)
#  if defined(M2K_BUILDING_DLL)
#   define M2K_API __declspec(dllexport)
#  else
#   define M2K_API __declspec(dllimport)
#  endif
# elif defined(__GNUC__) && __GNUC__ >= 4
#  define M2K_API __attribute__((visibility("default")))
# else
#  define M2K_API
# endif
#endif

/* Deprecation marker. Consumers see a -Wdeprecated-declarations
 * warning at the call site. modemu2k's own build defines
 * M2K_SUPPRESS_DEPRECATED so the in-tree tests that still exercise
 * the deprecated path build clean. */
#if defined(M2K_SUPPRESS_DEPRECATED)
# define M2K_DEPRECATED(msg)
#elif defined(__GNUC__) || defined(__clang__)
# define M2K_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
# define M2K_DEPRECATED(msg) __declspec(deprecated(msg))
#else
# define M2K_DEPRECATED(msg)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque modem emulator context. Create with m2k_new(), destroy with m2k_free(). */
typedef struct m2k_s m2k_t;

/**
 * @brief Runtime version string of the linked libmodemu2k.
 *
 * Lets a caller detect a header/library version mismatch by comparing
 * @ref M2K_VERSION (the value at the time the caller's translation unit
 * was compiled) against m2k_version() (the value the linked library was
 * built with).
 *
 * @return Static "MAJOR.MINOR.PATCH" string; never NULL, never owned by
 *         the caller.
 */
M2K_API const char *m2k_version(void);

/**
 * @brief Log callback type.
 * @param msg      NUL-terminated log message.
 * @param userdata Caller-supplied pointer passed unchanged from m2k_set_log_fn().
 */
typedef void (*m2k_log_fn)(const char *msg, void *userdata);

/**
 * @brief Return codes used by all m2k_* functions.
 */
/* Note: new codes are appended; existing values must never be reordered
   so external callers can safely cache numeric values across releases. */
typedef enum {
  M2K_OK = 0,       /**< Success. */
  M2K_ERR_NOMEM,    /**< Memory allocation failed. */
  M2K_ERR_PTY,      /**< PTY open or allocation failed. */
  M2K_ERR_SOCKET,   /**< TCP connect or I/O error. */
  M2K_ERR_TIMEOUT,  /**< Operation timed out. */
  M2K_ERR_CANCELED, /**< Operation canceled (e.g., +++ escape sequence). */
  M2K_ERR_BUG,      /**< Internal assertion failure — should not happen. */
  M2K_ERR_WOULDBLOCK, /**< Operation would block; call m2k_step() (or wait
                           for the relevant fd to become ready) and retry.
                           Mirrors libssh2's LIBSSH2_ERROR_EAGAIN. */
  M2K_ERR_AT,       /**< AT command rejected by the lexer (malformed input). */
} m2k_err_t;

/**
 * @brief Allocate and initialise a new modem context.
 * @return Newly allocated context, or NULL on memory exhaustion.
 */
M2K_API m2k_t      *m2k_new(void);

/**
 * @brief Release all resources held by @p ctx.
 * @param ctx Context to destroy. Safe to call with NULL.
 */
M2K_API void        m2k_free(m2k_t *ctx);

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
M2K_API void        m2k_set_log_fn(m2k_t *ctx, m2k_log_fn fn, void *userdata);

/** Recommended minimum size for the m2k_set_error_buffer() buffer. */
#define M2K_ERROR_BUFFER_SIZE 256

/**
 * @brief Install a buffer that receives a detailed message for the most
 *        recent error.
 *
 * When @p buf is non-NULL, internal error paths write a human-readable,
 * contextual message into @p buf (NUL-terminated, truncated to fit) in
 * addition to any logging via m2k_set_log_fn(). The caller can then
 * present the message after a function returns a non-OK m2k_err_t —
 * curl-style CURLOPT_ERRORBUFFER semantics.
 *
 * The buffer is only updated when an error actually occurs; successful
 * calls leave it untouched, so callers should check the function's
 * return code first.
 *
 * @param ctx  Modem context.
 * @param buf  Caller-owned buffer that outlives the context, or NULL
 *             to detach. Recommended size: @ref M2K_ERROR_BUFFER_SIZE.
 * @param size Capacity of @p buf in bytes (including the trailing NUL).
 *             Ignored when @p buf is NULL.
 *
 * @snippet examples/m2k_set_error_buffer.c set_error_buffer
 */
M2K_API void        m2k_set_error_buffer(m2k_t *ctx, char *buf, size_t size);

/**
 * @brief Feed a Hayes AT command string to the modem.
 *
 * Intended for one-shot configuration commands (S-register sets, mode
 * toggles, etc.). Action commands like ATD/ATO are recognised by the
 * lexer but do not drive a connection from this entry point; use
 * m2k_dial() / m2k_online() for those.
 *
 * @param ctx Modem context.
 * @param cmd NUL-terminated AT command (e.g. @c "ATZ" or @c "ATS0=1").
 *            A string with no @c AT prefix is silently ignored.
 * @return M2K_OK on successful execution or no-op (missing @c AT prefix);
 *         M2K_ERR_AT if the lexer rejected the command as malformed, or
 *         the command was an action verb that should go through a
 *         dedicated entry point (ATD → m2k_dial, ATO → m2k_online);
 *         M2K_ERR_BUG only for unexpected lexer states.
 *
 * @code
 * m2k_atcmd(ctx, "ATZ");        // reset to defaults
 * m2k_atcmd(ctx, "ATS0=1");    // auto-answer on first ring
 * @endcode
 */
M2K_API m2k_err_t   m2k_atcmd(m2k_t *ctx, const char *cmd);

/**
 * @brief Open a TCP connection to @p host : @p port.
 *
 * @deprecated Slated for removal in 0.3.0. Drive the state machine via
 *             m2k_run() or the step API (m2k_get_pollfds + m2k_step)
 *             and let the AT lexer dispatch the dial.
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
M2K_API M2K_DEPRECATED("use m2k_run() or the step API instead")
m2k_err_t           m2k_dial(m2k_t *ctx, const char *host, const char *port);

/**
 * @brief Enter online mode and relay data between the PTY and the socket.
 *
 * @deprecated Slated for removal in 0.3.0. Use m2k_run() or the step API
 *             (m2k_get_pollfds + m2k_step) instead — they share one
 *             state-machine implementation with the rest of the library.
 *
 * Blocks until the connection closes or the @c +++ escape sequence is detected.
 * Returns M2K_ERR_CANCELED when the caller escapes back to command mode.
 *
 * @param ctx Modem context (must have an active connection from m2k_dial()).
 * @return M2K_OK when the remote end closes, M2K_ERR_CANCELED on +++ escape.
 */
M2K_API M2K_DEPRECATED("use m2k_run() or the step API instead")
m2k_err_t           m2k_online(m2k_t *ctx);

/**
 * @brief Tear down the active TCP connection.
 * @param ctx Modem context.
 * @return M2K_OK on success.
 */
M2K_API m2k_err_t   m2k_hangup(m2k_t *ctx);

/**
 * @brief Request an immediate return to command mode from online mode.
 *
 * Drives the same transition that the @c +++ escape sequence would —
 * but without the silence-guard timing or sending the literal @c +++
 * bytes through the I/O path. Useful from a host application that
 * embeds modemu2k and wants programmatic control of the mode.
 *
 * The transition takes effect on the next m2k_step() / m2k_run()
 * iteration. The underlying TCP connection is left intact; use
 * m2k_hangup() afterward if you also want to drop it.
 *
 * A no-op when the context is not currently in online mode.
 *
 * @param ctx Modem context.
 * @return M2K_OK.
 */
M2K_API m2k_err_t   m2k_escape(m2k_t *ctx);

/**
 * @brief Return a human-readable string for @p err.
 * @param err Error code.
 * @return Static string; never NULL.
 */
M2K_API const char *m2k_strerror(m2k_err_t err);

/**
 * @brief Use stdin/stdout as the TTY (standalone mode).
 *
 * Puts the terminal into raw mode.  Call before m2k_run().
 *
 * @param ctx Modem context.
 * @return M2K_OK.
 */
M2K_API m2k_err_t   m2k_setup_stdin(m2k_t *ctx);

/**
 * @brief Allocate a PTY master and return the slave device path.
 *
 * The returned pointer is owned by the context — do NOT free() it.
 * It remains valid until the next m2k_setup_*() call or m2k_free(ctx).
 *
 * Use this when you need the slave path itself (e.g. to print it with
 * --show). To fork a comm program on the slave, prefer
 * m2k_setup_comm_program().
 *
 * @param ctx       Modem context.
 * @param slave_out Out: pointer to a NUL-terminated slave path inside @p ctx.
 *                  Must be non-NULL.
 * @return M2K_OK on success, M2K_ERR_PTY on failure.
 */
M2K_API m2k_err_t   m2k_setup_pty(m2k_t *ctx, const char **slave_out);

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
M2K_API m2k_err_t   m2k_setup_comm_program(m2k_t *ctx, const char *cmd);

/**
 * @brief Open an existing PTY device as the TTY.
 *
 * @param ctx Modem context.
 * @param dev Path to the PTY master device (e.g. @c "/dev/ptyp0").
 * @return M2K_OK on success, M2K_ERR_PTY if the device cannot be opened.
 */
M2K_API m2k_err_t   m2k_setup_dev(m2k_t *ctx, const char *dev);

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
M2K_API m2k_err_t   m2k_setup_listen(m2k_t *ctx, const char *port);

/**
 * @brief Embed mode: no real TTY fd; the host application supplies the
 *        bytes a TTY would normally produce, via m2k_write_from_app(),
 *        and consumes the bytes modemu2k would normally send to the TTY,
 *        via m2k_read_to_app().
 *
 * Intended for embedding modemu2k inside a host program that already
 * owns the terminal/serial interface (e.g. a comm program linking
 * libmodemu2k instead of forking the standalone binary). The socket
 * side still uses real fds and integrates with the host's event loop
 * via m2k_get_pollfds() as usual.
 *
 * @param ctx Modem context.
 * @return M2K_OK.
 */
M2K_API m2k_err_t   m2k_setup_app_io(m2k_t *ctx);

/**
 * @brief Push bytes into the modem as if they had been read from the TTY.
 *
 * Only valid after m2k_setup_app_io(). Accepts as many bytes from @p buf
 * as the internal TTY read buffer has room for (partial accept; the
 * actual count is returned via @p *consumed). The accepted bytes are
 * processed by the next m2k_step() call.
 *
 * If the buffer is completely full, *consumed is set to 0 and the
 * function returns M2K_ERR_WOULDBLOCK — call m2k_step() to drain, then
 * retry.
 *
 * @param ctx      Modem context.
 * @param buf      Bytes to inject. Must be non-NULL when @p len > 0.
 * @param len      Length of @p buf in bytes. May be 0.
 * @param consumed Out: bytes actually accepted (0 .. len). Must be non-NULL.
 * @return M2K_OK if any bytes were accepted, M2K_ERR_WOULDBLOCK if none
 *         were, M2K_ERR_PTY if the context is not in app-I/O mode.
 */
M2K_API m2k_err_t   m2k_write_from_app(m2k_t *ctx, const void *buf, size_t len,
                               size_t *consumed);

/**
 * @brief Drain bytes from the modem that would normally have been written
 *        to the TTY.
 *
 * Only valid after m2k_setup_app_io(). Copies up to @p max bytes from the
 * internal TTY write buffer into @p buf and stores the actual length in
 * @p *len_out (which may be zero if there is nothing pending).
 *
 * @par Back-pressure shape
 * After m2k_step() processes an incoming socket burst, the internal
 * TTY-bound buffer may hold more bytes than a single read_to_app call
 * can return. **The host must not block in poll() while bytes remain
 * pending** — none of modemu2k's fds will fire to wake it. Use one of:
 *
 * - Loop on read_to_app until m2k_has_pending_output() returns 0; or
 * - On a partial drain (got &lt; max OR you only wanted a small chunk),
 *   check m2k_has_pending_output() and use a zero timeout on the next
 *   poll() so the next read_to_app runs immediately.
 *
 * Skipping this collapses throughput to roughly "your buffer size
 * per poll timeout."
 *
 * @param ctx     Modem context.
 * @param buf     Destination buffer. Must be non-NULL when @p max > 0.
 * @param max     Maximum bytes to copy. May be 0.
 * @param len_out Out: number of bytes actually copied (0 means no data).
 *                Must be non-NULL.
 * @return M2K_OK on success, M2K_ERR_PTY if the context is not in
 *         app-I/O mode.
 */
M2K_API m2k_err_t   m2k_read_to_app(m2k_t *ctx, void *buf, size_t max, size_t *len_out);

/**
 * @brief Test whether the modem has TTY-bound bytes still buffered.
 *
 * Returns nonzero whenever a subsequent m2k_read_to_app() would deliver
 * at least one byte without first needing another m2k_step()/poll
 * cycle. See m2k_read_to_app() for the back-pressure pattern this
 * predicate exists to support.
 *
 * Always returns 0 when the context isn't in app-I/O mode.
 *
 * @param ctx Modem context.
 * @return Nonzero if buffered output is pending, zero otherwise.
 */
M2K_API int         m2k_has_pending_output(const m2k_t *ctx);

/**
 * @brief Expose the listening socket's fd (after m2k_setup_listen,
 *        before m2k_listen_accept).
 *
 * Lets a host event loop poll the fd for POLLIN — i.e. detect an
 * incoming TCP connection (the "RING" event in modem terms) so it can
 * call m2k_listen_accept() at the right moment instead of blocking
 * unconditionally on it.
 *
 * @param ctx Modem context.
 * @return The fd of a bound-but-not-yet-accepted listener, or -1 when
 *         no listener is open (either never set up, or already accepted).
 */
M2K_API int         m2k_get_listen_fd(const m2k_t *ctx);

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
M2K_API m2k_err_t   m2k_listen_accept(m2k_t *ctx);

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
M2K_API m2k_err_t   m2k_run(m2k_t *ctx);

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
 * Dialing (ATD) is non-blocking: m2k_step() transitions the context
 * into an internal DIAL state on ATD, returns control immediately,
 * and resumes the connect() across subsequent m2k_step() calls. The
 * host event loop is not held up by connect(). m2k_get_pollfds()
 * publishes the in-progress socket fd while DIAL is active, so the
 * host pollset stays accurate without special-casing.
 */

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
M2K_API m2k_err_t   m2k_get_pollfds(m2k_t *ctx, struct pollfd *fds,
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
M2K_API m2k_err_t   m2k_step(m2k_t *ctx, struct pollfd *fds, size_t nfds);

/**
 * @brief Test whether the session has ended.
 * @param ctx Modem context.
 * @return Nonzero when the state machine has reached a terminal state
 *         (PTY closed, etc.) and the caller's loop should stop.
 */
M2K_API int         m2k_run_done(const m2k_t *ctx);

/**
 * @brief Test whether the modem is currently in online mode.
 *
 * Useful for embed-mode hosts that paint mode-specific UI (status bar,
 * local echo toggle, etc.) and need to know whether m2k is relaying
 * bytes to/from a connected socket vs. consuming AT commands.
 *
 * @param ctx Modem context.
 * @return Nonzero in online mode, zero in command mode or after the
 *         session has ended.
 */
M2K_API int         m2k_is_online(const m2k_t *ctx);

/**
 * @brief Test whether the modem has an active carrier (live TCP socket).
 *
 * Maps to the DCD (Data Carrier Detect) signal real serial modems
 * expose. Stays true after a `+++` escape back to command mode as long
 * as the TCP connection is still up; goes false on hangup, NO CARRIER,
 * or before the first dial.
 *
 * @param ctx Modem context.
 * @return Nonzero when a connection is alive, zero otherwise.
 */
M2K_API int         m2k_has_carrier(const m2k_t *ctx);

/**
 * @brief Set the host's DTR (Data Terminal Ready) signal state.
 *
 * On the 1→0 transition while a connection is live, modemu2k hangs the
 * connection up — matching the typical real-modem `AT&D2` default of
 * "DTR drop terminates the call". Drop is otherwise a no-op (no carrier
 * to drop). The DTR `&D` register is not (yet) lexable from AT command
 * strings, so this function is the sole way to drive DTR-related
 * behavior. Default state is asserted (1).
 *
 * @param ctx Modem context.
 * @param on  Non-zero to assert DTR, zero to drop it.
 */
M2K_API void        m2k_set_dtr(m2k_t *ctx, int on);

/**
 * @brief Set the host's RTS (Request to Send) signal state.
 *
 * Tracked for symmetry with real Hayes modems; modemu2k does not act on
 * RTS state (hardware flow control is meaningless for a TCP-based
 * virtual modem — the kernel's socket buffers and TCP windowing do
 * that job). Default state is asserted (1).
 *
 * @param ctx Modem context.
 * @param on  Non-zero to assert RTS, zero to drop it.
 */
M2K_API void        m2k_set_rts(m2k_t *ctx, int on);

/** Return the most recent value passed to m2k_set_dtr() (1 by default). */
M2K_API int         m2k_get_dtr(const m2k_t *ctx);

/** Return the most recent value passed to m2k_set_rts() (1 by default). */
M2K_API int         m2k_get_rts(const m2k_t *ctx);

#ifdef __cplusplus
}
#endif
