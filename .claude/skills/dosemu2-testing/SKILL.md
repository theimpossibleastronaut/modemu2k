---
name: dosemu2-testing
description: Build dosemu2 against a local libmodemu2k and run the vmodem tests in a container. Use when changing the modemu2k API that dosemu2's vmodem driver consumes, or when working on the dosemu2 modemu plugin itself.
---

# Testing modemu2k against dosemu2

dosemu2's `vmodem` serial driver (`src/plugin/modemu/modemu2k.c`) links
libmodemu2k. Nothing in modemu2k's own suite exercises that pairing, so
API changes get validated by building dosemu2 against the working tree
and running its serial tests.

Building dosemu2 natively means installing fdpp and a long tail of
other dependencies on the host. Everything below happens inside a
container instead, so the host needs only Docker.

## Container setup

Image: `ghcr.io/theimpossibleastronaut/dosemu2-container:build-env`
(Alpine-based, `apk`). It is built from
<https://github.com/theimpossibleastronaut/dosemu2-container>, which
also carries the Dockerfiles, the compose services and the entrypoint
described below — read that repo rather than guessing at the image's
behaviour. A local clone of it is worth having; it is the only place
the entrypoint's remap logic is written down.

The Alpine chain replaced an Arch one in
[issue 10](https://github.com/theimpossibleastronaut/dosemu2-container/issues/10),
so anything you find describing `pacman` phases is out of date.

The image already carries meson, ninja, flex, bison, autoconf,
automake, libtool, pkg-config and git, so the only thing the tests
need added is pexpect:

```
docker exec m2kdrv apk add --no-cache py3-pexpect
docker exec m2kdrv ln -sf /bin/echo /usr/bin/echo
```

The symlink is for `test_serial_simple_read_echo`, which runs
`/usr/bin/echo` as the port's `exec` program. Alpine has only
busybox's `/bin/echo`, and `apk add coreutils` does not create the
`/usr/bin` path either. Without the symlink that test fails with
`ERROR: SER: /usr/bin/echo ... not found` in the dosemu boot log.

**Bind-mount the source tree read-write at the workdir. Do not copy it
in.** The image is built for this: its `entrypoint.sh` stats the
workdir, remaps its internal `builder` user to that UID/GID, and drops
to it, so everything written from inside lands on the host correctly
owned.

```
docker run -d --name m2kdrv -w /workspace \
  -v /path/to/dosemu2:/workspace:rw \
  -v /path/to/modemu2k:/src/modemu2k:ro \
  ghcr.io/theimpossibleastronaut/dosemu2-container:build-env sleep infinity
```

**`docker exec` bypasses the entrypoint and lands as root**, which
defeats the whole mechanism. Pass `--user builder` for anything that
writes to the workspace or runs the tests; leave it off only for root
work like `apk` and `make install`:

```
docker exec --user builder -w /workspace m2kdrv <command>
```

Building as root instead is not merely untidy. It leaves root-owned
objects through the repo, and it makes `test_serial_simple_read_echo`
fail — dosemu2 refuses `exec` under root (`SER: "exec" ignored because
of root privs`). That failure is easy to write off as an environment
artifact; it is really a sign the build ran as the wrong user. Read
the boot log rather than guessing which of its two causes you have:
the other is the missing `/usr/bin/echo` above, and they look
identical from the test output.

The read-write mount also means `make` is incremental across sessions
and there is no copy to keep in sync. If you ever do fall back to
copying a tree in, **copy the working tree, never `git clone` it** — a
clone carries only what is committed, so an uncommitted fix is silently
absent and the run tests the old code. `docker cp` has a matching trap:
it preserves the host mtime, so a file older than the build objects
makes `make` a no-op and the run tests a stale binary.

Building in the workspace writes `configure`, `Makefile.conf`,
`2.0-pre9/` and every `.o` into the source tree, so expect a crowd of
untracked files in the dosemu2 repo. Stage explicit paths when
committing there; never `git add -A`.

## Build and install

```
docker exec m2kdrv sh -c "mkdir -p /work && cp -a /src/modemu2k /work/ && cd /work/modemu2k && meson setup _b --prefix=/usr --libdir=lib && ninja -C _b && meson install -C _b"
docker exec --user builder -w /workspace m2kdrv bash -c "./autogen.sh && ./default-configure && nice -n 19 make -j\$(nproc)"
docker exec m2kdrv bash -c "cd /workspace && make install"
```

modemu2k is copied rather than mounted read-write because it installs
into `/usr` inside the container and nothing needs to come back out.

`--libdir=lib` matters: without it meson installs to `lib64` on this
image and dosemu2's link fails to find the library.

The image is musl, not glibc. modemu2k builds there and its own suite
passes, and so does dosemu2 with the vmodem driver (both verified
2026-08-03), but a musl-only compile failure is a thing to suspect if
something breaks here and not on the host.

**Alpine's `/sbin/ldconfig` is a shim that exits 1.** Nothing needs it
under musl, so it is left out of the commands above. Never `&&` it
into a chain: it will silently swallow everything after it.

`configure` printing `modemu` in the plugin list is the sign the
`PKG_CHECK_MODULES` in `src/plugin/modemu/configure.ac` was satisfied.

## Run the tests

```
docker exec --user builder -w /workspace m2kdrv python3 test/test_dosemu.py \
  PPDOSGITTestCase.test_serial_vmodem_dial_data_exchange \
  PPDOSGITTestCase.test_serial_vmodem_dial_data_exchange_ipv6 \
  PPDOSGITTestCase.test_serial_vmodem_answer_data_exchange \
  PPDOSGITTestCase.test_serial_simple_read_echo \
  PPDOSGITTestCase.test_serial_simple_write_file
```

Run the framework from the dosemu2 repo root: `topdir` resolves from
cwd, so running inside `test/` fails on missing bindist files.

Each test takes well under a second. That is fast enough to look like a
test that never ran, so **prove a green run is real before believing
it**: break one assertion, confirm it fails, and read the captured DOS
session in the failure message. It shows the actual `CONNECT` and the
bytes that crossed. Restore the file afterwards.

## Known non-failures

- `Usable KVM not found, falling back to emulation` is normal.
- `configure: error: sdl3 found, not enabling sdl2` is a configure
  notice, not a build failure.

## Checking the static-plugin build

The modemu plugin builds as a DL plugin by default. The other path
(everything linked into the main binary) is worth checking whenever the
plugin's `Makefile` or `Makefile.conf.in` changes.

Two extra packages are needed first. Static-plugin mode links *every*
plugin's libraries into the main binary, and the image ships neither
libao nor libieee1284 -- deliberately, since libao duplicates the
SDL3/alsa paths and ieee1284 wants a real parallel-port dongle. Without
them the link fails on `cannot find -lao` / `-lieee1284`, which looks
like a problem with the change under test and is not:

```
docker exec m2kdrv apk add --no-cache libao-dev libieee1284-dev
```

Then build on a copy, so the workspace keeps its normal configuration.
Use a fresh copy rather than reconfiguring in place: a previous
configure leaves each plugin's `Makefile.conf` behind, and static mode
includes them all, so the old library list is pulled back in and the
link fails again even after the packages are installed.

```
docker exec --user builder m2kdrv bash -c "rm -rf /tmp/static && cp -a /workspace /tmp/static && cd /tmp/static && ./autogen.sh && ./default-configure --disable-dlplugins && nice -n 19 make -j\$(nproc)"
```

Confirm the driver really went in by grepping the final link line for
`libplugin_modemu.a` and `-lmodemu2k`; that, plus `load_plugin()` being
a NULL-returning stub when `USE_DL_PLUGINS=0`, is why `serial_init()`
tests the driver pointer rather than `load_plugin()`'s return.

**Running the static build is a separate question from linking it.**
As of 2026-08-05 a static dosemu2 in this container dies at boot with
`leavedos(bad_rpc:69|0)` before the serial code runs, so the tests
cannot confirm vmodem behaviour there. Treat this as a build check
only, and do not report a static run as passing without reading the
boot log.

## Seeing the driver's narration

The test framework's `-td` log flags do not enable the serial class, so
`s_printf` output is invisible. Run dosemu2 with `-D+s` to see it; the
library's own messages are prefixed `MODEMU2K`.

## Keep the container

Keep it for the life of the branch it serves rather than removing it
at the end of a session. Recreating it costs an apk sync, a full
dosemu2 build and a modemu2k install, and all of that is wasted if the
next session needs the same thing. The read-write mount means a
returning session only has to rebuild what changed.

For AI sessions specifically: do not suggest tearing it down, and do
not put "remove the container" on the task list. Removing it is the
maintainer's call.
