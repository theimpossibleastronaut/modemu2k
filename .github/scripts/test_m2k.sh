#!/bin/sh

set -ev

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
