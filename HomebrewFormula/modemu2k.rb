# frozen_string_literal: true

# The comment above is here because this file lives in HomebrewFormula/ rather
# than Formula/ -- Homebrew's formula rubocop rules only apply to the latter, so
# the generic Ruby cops run and brew audit --strict fails without it. No formula
# in homebrew-core carries it; do not "tidy" it away.

class Modemu2k < Formula
  desc "Hayes modem emulator that dials over TCP instead of a phone line"
  homepage "https://github.com/theimpossibleastronaut/modemu2k"
  url "https://github.com/theimpossibleastronaut/modemu2k/releases/download/v0.2.4/modemu2k-0.2.4.tar.xz"
  sha256 "7ad7994e3ffee235a62228f0cb99f37a8b473979b54ae089b5e249796f70650c"
  license "GPL-2.0-or-later"
  head "https://github.com/theimpossibleastronaut/modemu2k.git", branch: "master"

  depends_on "meson" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build

  def install
    system "meson", "setup", "build", *std_meson_args
    system "meson", "compile", "-C", "build", "--verbose"
    system "meson", "install", "-C", "build"
  end

  def caveats
    <<~EOS
      modemu2k presents a modem on a pty, so software that can only talk to a
      serial modem can reach a telnet BBS. Print the pty to hand to an emulator
      (QEMU/UTM -serial pty, 86Box serial passthrough, VICE RS232) with:

        modemu2k -s

      Then dial from the guest's own comm program:

        ATD"bbs.example.org 23"

      To drive it from a terminal program here instead, see the
      "with a comm program" section of:
        #{doc}/QUICKSTART.md
    EOS
  end

  test do
    assert_path_exists bin/"modemu2k"
    assert_path_exists lib/"pkgconfig/modemu2k.pc"
    assert_path_exists include/"modemu2k/modemu2k.h"
    assert_match "dialup TCP connecting", (man1/"modemu2k.1").read

    # Invoked with no arguments it prints its help and exits 0, which is the
    # only run that needs neither a pty nor a network peer. NO_COLOR keeps the
    # ANSI styling out of the comparison.
    ENV["NO_COLOR"] = "1"
    assert_match "Usage:", shell_output(bin/"modemu2k")

    output = shell_output("#{bin}/modemu2k -V")
    assert_match "modemu2k", output
    assert_match version.to_s, output unless build.head?
  end
end
