prog_flex = find_program('flex')

cmd_lex = custom_target(
  'cmdlex.c',
  input : 'cmdlex.l',
  output : 'cmdlex.c',
  command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@'],
)

src = [
  cmd_lex,
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

lib_modemu2k = library(
  'modemu2k',
  src,
  version: meson.project_version(),
  c_args : '-DMODEMU2K_VERSION="@0@"'.format(meson.project_version()),
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
