#!/usr/bin/env python3
"""Integration test: dial a real busybox telnetd in a docker container.

Skipped (exit 77, meson's standard skip code) if docker isn't installed
or the daemon isn't reachable without sudo. The latter covers both
"daemon not running" and "current user not in the docker group" — in
both cases `docker info` exits non-zero, and neither case should fail
the test suite.

Usage: test_docker_telnet.py <path-to-modemu2k>
"""
import os
import select
import shutil
import socket
import subprocess
import sys
import time

# Reserved test port — same numbering family as
# test_listen (19876/19877) and test_*_integration (19878).
HOST_PORT = 19879
CONTAINER_PORT = 2323
CONTAINER = "m2k-telnetd-test"
IMAGE = "m2k-telnetd-test"

DOCKER_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "docker"
)


def docker_usable():
    if not shutil.which("docker"):
        return False, "docker binary not found in PATH"
    r = subprocess.run(["docker", "info"], capture_output=True, timeout=10)
    if r.returncode != 0:
        msg = (r.stderr or r.stdout).decode("utf-8", "replace").strip()
        first_line = msg.split("\n")[0][:120]
        return False, f"docker info failed (daemon down or needs sudo): {first_line}"
    return True, ""


def read_until(stream, needle, timeout):
    deadline = time.monotonic() + timeout
    buf = bytearray()
    while time.monotonic() < deadline:
        r, _, _ = select.select([stream], [], [], 0.2)
        if r:
            chunk = os.read(stream.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
            if needle in buf:
                return bytes(buf)
    return bytes(buf)


def teardown():
    subprocess.run(
        ["docker", "rm", "-f", CONTAINER],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def main():
    if len(sys.argv) < 2:
        print("usage: test_docker_telnet.py <modemu2k-binary>", file=sys.stderr)
        return 2

    binary = sys.argv[1]

    ok, why = docker_usable()
    if not ok:
        print(f"SKIP: {why}", file=sys.stderr)
        return 77

    teardown()  # paranoia: clear any stray container from a prior aborted run

    # --load is required when the active buildx driver is docker-container
    # (the modern default): without it the built image stays in the build
    # cache rather than landing in the local image store, and `docker run`
    # below would then fail to find it. --load is a no-op on the legacy
    # docker driver.
    build = subprocess.run(
        ["docker", "build", "--load", "-q", "-t", IMAGE, DOCKER_DIR],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    if build.returncode != 0:
        err = build.stderr.decode("utf-8", "replace")[:200]
        print(f"SKIP: docker build failed: {err}", file=sys.stderr)
        return 77

    run = subprocess.run(
        ["docker", "run", "--rm", "-d", "--name", CONTAINER,
         "-p", f"{HOST_PORT}:{CONTAINER_PORT}", IMAGE],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    if run.returncode != 0:
        err = run.stderr.decode("utf-8", "replace")[:200]
        print(f"FAIL: docker run failed: {err}", file=sys.stderr)
        return 1

    try:
        # Wait for the container's telnetd to actually accept connections.
        for _ in range(50):
            try:
                with socket.create_connection(("127.0.0.1", HOST_PORT), timeout=1):
                    break
            except OSError:
                time.sleep(0.2)
        else:
            print(f"FAIL: container never opened port {HOST_PORT}", file=sys.stderr)
            return 1

        # Dial it via modemu2k. CA_STDINOUT mode is reached by passing a
        # token via -e (matching test_dial_integration.py). S7=5 keeps the
        # dial timeout short; %D1 disables keypress-cancel since stdin is
        # a pipe, not a tty.
        proc = subprocess.Popen(
            [binary, "-e", "ATS7=5"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        try:
            # Synchronise: wait for each OK before sending the next command
            # (matching test_dial_integration.py's pattern).
            proc.stdin.write(b"AT%D1\r")
            ack = read_until(proc.stdout, b"OK", timeout=3.0)
            if b"OK" not in ack:
                print(f"FAIL: no OK to AT%D1\n  captured: {ack!r}", file=sys.stderr)
                return 1

            proc.stdin.write(f'ATD"127.0.0.1 {HOST_PORT}"\r'.encode())
            out = read_until(proc.stdout, b"CONNECT", timeout=8.0)
            if b"CONNECT" not in out:
                print(f"FAIL: no CONNECT in modemu2k output\n  captured: {out!r}",
                      file=sys.stderr)
                return 1

            # busybox telnetd negotiates options on connect; observing an
            # IAC (0xFF) byte in the relayed stream proves the telnet path
            # is exercised, not just the raw TCP dial. Absence is
            # non-fatal — some telnetd configs are quieter.
            iac = read_until(proc.stdout, b"\xff", timeout=2.0)
            if b"\xff" not in iac:
                print("note: no IAC bytes observed (CONNECT only)", file=sys.stderr)
        finally:
            try:
                proc.stdin.close()
            except OSError:
                pass
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
    finally:
        teardown()

    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
