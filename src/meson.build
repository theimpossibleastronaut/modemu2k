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
  'sockbuf.c',
  'stty.c',
  'timeval.c',
  'verbose.c',
  'sock.c',
  'telopt.c',
  'ttybuf.c',
  'utils.c'
  ]

lib_modemu2k = static_library(
  'modemu2k',
  src,
  include_directories : '..',
  dependencies: [gettext_deps]
  )

dep_modemu2k = declare_dependency(link_with: lib_modemu2k, include_directories : '..')
