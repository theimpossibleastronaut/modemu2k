#!/usr/bin/env python3
"""Dial-mode integration test for modemu2k.

Launches modemu2k in CA_STDINOUT mode (entered via `-e "ATZ"`, the
minimum flag needed past the new no-arg usage hint), drives an
AT-command sequence through stdin/stdout, and verifies the
dial -> data -> disconnect -> exit lifecycle against a local
Python TCP server.

Usage: test_dial_integration.py <path-to-modemu2k>
"""
import os
import select
import socket
import subprocess
import sys
import threading
import time


def strip_iac(buf):
    """Remove telnet IAC sequences so plain data bytes can be echoed safely."""
    out = bytearray()
    i = 0
    while i < len(buf):
        b = buf[i]
        if b != 0xFF:
            out.append(b)
            i += 1
            continue
        if i + 1 >= len(buf):
            break
        cmd = buf[i + 1]
        if cmd in (0xFB, 0xFC, 0xFD, 0xFE):    # WILL/WONT/DO/DONT: 3-byte
            i += 3
        elif cmd == 0xFA:                       # SB ... IAC SE
            j = i + 2
            while j + 1 < len(buf) and not (buf[j] == 0xFF and buf[j + 1] == 0xF0):
                j += 1
            i = j + 2
        else:                                   # 2-byte command
            i += 2
    return bytes(out)


class EchoServer:
    def __init__(self):
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.conn = None
        self._stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()

    def _run(self):
        self.sock.settimeout(5.0)
        try:
            self.conn, _ = self.sock.accept()
        except socket.timeout:
            return
        self.conn.settimeout(0.2)
        while not self._stop.is_set():
            try:
                data = self.conn.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not data:
                break
            payload = strip_iac(data)
            if payload:
                try:
                    self.conn.sendall(payload)
                except OSError:
                    break
        try:
            self.conn.close()
        except OSError:
            pass

    def disconnect_client(self):
        self._stop.set()
        try:
            if self.conn:
                self.conn.shutdown(socket.SHUT_RDWR)
                self.conn.close()
        except OSError:
            pass

    def shutdown(self):
        self._stop.set()
        try:
            self.sock.close()
        except OSError:
            pass


def read_until(stream, needle, timeout=3.0):
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        r, _, _ = select.select([stream], [], [], 0.1)
        if r:
            chunk = os.read(stream.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
            if needle in buf:
                return bytes(buf)
    return bytes(buf)


def fail(msg, captured=b""):
    sys.stderr.write(f"FAIL: {msg}\n")
    if captured:
        sys.stderr.write(f"  captured: {captured!r}\n")
    return 1


def main():
    if len(sys.argv) < 2:
        print("usage: test_dial_integration.py <modemu2k-binary>", file=sys.stderr)
        return 2
    binary = sys.argv[1]

    server = EchoServer()
    server.start()

    proc = subprocess.Popen(
        [binary, "-e", "ATZ"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=0,
    )

    def send(s):
        proc.stdin.write(s.encode())

    try:
        send("AT\r")
        out = read_until(proc.stdout, b"OK")
        if b"OK" not in out:
            return fail("no OK to bare AT", out)

        send(f"ATD127.0.0.1 {server.port}\r")
        out = read_until(proc.stdout, b"CONNECT")
        if b"CONNECT" not in out:
            return fail("no CONNECT after dial", out)

        send("ping\n")
        out = read_until(proc.stdout, b"ping", timeout=3.0)
        if b"ping" not in out:
            return fail("data did not round-trip through online loop", out)

        server.disconnect_client()
        out = read_until(proc.stdout, b"NO CARRIER", timeout=3.0)
        if b"NO CARRIER" not in out:
            return fail("no NO CARRIER after server disconnect", out)

        proc.stdin.close()
        try:
            rc = proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            return fail("modemu2k did not exit on stdin EOF")
        if rc != 0:
            return fail(f"modemu2k exited with status {rc}")
        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
        server.shutdown()


if __name__ == "__main__":
    sys.exit(main())
