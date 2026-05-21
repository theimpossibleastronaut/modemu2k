#!/usr/bin/env python3
"""Integration test for modemu2k listen mode (-l/--listen).

Launches the real `modemu2k` binary with `-l <port>`, connects a TCP
client to that port, exchanges an AT command + response, then closes
the connection and asserts modemu2k exits cleanly.

Usage: test_listen_integration.py <path-to-modemu2k>
"""
import os
import signal
import socket
import subprocess
import sys
import time

# Fixed port to avoid the bind-race between the test's port picker and
# modemu2k's listen(). The unit tests use 19876 (IPv4) and 19877 (IPv6);
# the integration test reserves the next slot in that range.
TEST_PORT = 19878


def connect_with_retry(port, timeout=5.0):
    deadline = time.monotonic() + timeout
    last_err = None
    while time.monotonic() < deadline:
        try:
            return socket.create_connection(("127.0.0.1", port), timeout=1.0)
        except OSError as e:
            last_err = e
            time.sleep(0.05)
    raise RuntimeError(f"could not connect to 127.0.0.1:{port}: {last_err}")


def recv_until(sock, needle, timeout=2.0):
    sock.settimeout(timeout)
    buf = b""
    while needle not in buf:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
    return buf


def main():
    if len(sys.argv) < 2:
        print("usage: test_listen_integration.py <modemu2k-binary>",
              file=sys.stderr)
        return 2
    binary = sys.argv[1]
    if not os.access(binary, os.X_OK):
        print(f"FAIL: {binary} is not executable", file=sys.stderr)
        return 1

    port = TEST_PORT
    proc = subprocess.Popen(
        [binary, "-l", str(port)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    try:
        try:
            sock = connect_with_retry(port)
        except RuntimeError as e:
            print(f"FAIL: {e}", file=sys.stderr)
            return 1

        try:
            sock.sendall(b"AT\r")
            data = recv_until(sock, b"OK")
            if b"OK" not in data:
                print(f"FAIL: no OK response (got {data!r})", file=sys.stderr)
                return 1
        finally:
            sock.close()

        try:
            rc = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("FAIL: modemu2k did not exit after client disconnect",
                  file=sys.stderr)
            return 1
        if rc != 0:
            print(f"FAIL: modemu2k exited with status {rc}", file=sys.stderr)
            return 1
        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.exit(main())
