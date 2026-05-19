#pragma once
#include "modemu2k.h"

struct m2k_s {
    Atcmd              atcmd;
    Atcmd              atcmdNV;
    st_sock            sock;
    struct st_sockBufR sockBufR;
    struct st_sockBufW sockBufW;
    struct st_tty      tty;
    struct st_ttyBufR  ttyBufR;
    struct st_ttyBufW  ttyBufW;
    struct st_telOpt   telOpt;
    m2k_log_fn         log_fn;
    void              *log_userdata;
};
