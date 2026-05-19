#pragma once

typedef struct m2k_s m2k_t;

typedef void (*m2k_log_fn)(const char *msg, void *userdata);

typedef enum {
  M2K_OK = 0,
  M2K_ERR_NOMEM,
  M2K_ERR_PTY,
  M2K_ERR_SOCKET,
  M2K_ERR_TIMEOUT,
  M2K_ERR_CANCELED,
  M2K_ERR_BUG,
} m2k_err_t;

m2k_t      *m2k_new(void);
void        m2k_free(m2k_t *ctx);
void        m2k_set_log_fn(m2k_t *ctx, m2k_log_fn fn, void *userdata);

m2k_err_t   m2k_atcmd(m2k_t *ctx, const char *cmd);
m2k_err_t   m2k_dial(m2k_t *ctx, const char *host, const char *port);
m2k_err_t   m2k_online(m2k_t *ctx);
m2k_err_t   m2k_hangup(m2k_t *ctx);

const char *m2k_strerror(m2k_err_t err);
