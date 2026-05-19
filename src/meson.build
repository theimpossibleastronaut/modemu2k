src = [
  'cmdlex.c',
  'atcmd.c',
  'commx.c',
  'm2k_api.c',
  'sockbuf.c',
  'stty.c',
  'timeval.c',
  'verbose.c',
  'sock.c',
  'telopt.c',
  'ttybuf.c',
  'utils.c'
  ]

version = meson.project_version()

lib_modemu2k = library(
  meson.project_name(),
  src,
  version: version,
  c_args : '-DMODEMU2K_VERSION="@0@"'.format(version),
  include_directories : ['..', '../include'],
  install: true,
  )

dep_modemu2k = declare_dependency(
  link_with: lib_modemu2k,
  include_directories : include_directories('../include')
  )

pkg.generate(
  lib_modemu2k,
  description: 'PTY-based modem emulator library',
  )
