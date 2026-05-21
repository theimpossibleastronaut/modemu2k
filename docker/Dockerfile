# Minimal telnet server for manual testing of modemu2k.
#
# Build: docker build -t m2k-telnetd docker
# Run:   docker run --rm -p 2323:2323 m2k-telnetd
# Dial:  inside modemu2k, type:  ATDlocalhost:2323
#
# busybox telnetd performs real NVT/IAC negotiation, so this exercises
# modemu2k's telnet option-handling code paths the same way a real BBS would.
FROM alpine:latest

RUN apk add --no-cache busybox-extras

EXPOSE 2323

# -F: run in foreground (so the container stays alive)
# -p 2323: listen on an unprivileged port
# -l /bin/sh: skip login, drop straight into a shell on connect
CMD ["busybox-extras", "telnetd", "-F", "-p", "2323", "-l", "/bin/sh"]
