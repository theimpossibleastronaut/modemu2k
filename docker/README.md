# Test telnet server

A throwaway busybox `telnetd` container for manual testing of modemu2k.
Performs real NVT/IAC negotiation, so it exercises the telnet option-handling
code paths.

## Run

```sh
docker compose up --build      # or: docker build -t m2k-telnetd . && docker run --rm -p 2323:2323 m2k-telnetd
```

Listens on `localhost:2323`. Connect from modemu2k with:

```
ATD"localhost 2323"
```

(Quoted, space-separated — see `QuickStart`. Unquoted `ATD` only accepts
dotted IPv4 numerics.)

Drops you into `/bin/sh` (no login). `Ctrl-]` then `quit` exits the telnet
client side; `+++` returns modemu2k to command mode.
