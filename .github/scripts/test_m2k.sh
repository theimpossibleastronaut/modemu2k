#!/bin/sh

set -ev

sudo apt-get install -y gettext

USE_VALGRIND=""

if [ "$MATRIX_OS" = "ubuntu-latest" ] && [ "$MATRIX_ARCH" = "x86" ]; then
  USE_VALGRIND=1
  sudo apt-get install -y valgrind
fi

if [ "$MATRIX_OS" = "ubuntu-18.04" ]; then
  sudo apt-get install -y python3-pip python3-setuptools
fi

sudo -H python3 -m pip install meson ninja

meson builddir
cd builddir
ninja -v
# Temporarily disabled. The test fails because it can't connect, but this worked
# on Travis. Something different about GitHub Actions?
#
#ninja dist

if [ -n "$USE_VALGRIND" ]; then
  meson test --setup=valgrind
fi

# nls disabled
meson configure -Dnls=false
ninja -v
