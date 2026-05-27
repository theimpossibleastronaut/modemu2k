src = [
  'atcmd.c',
  'comm_program.c',
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

# Regenerate cmdlex.c from cmdlex.l when flex is available; otherwise
# fall back to the committed pre-generated copy so consumers without
# flex (typical release tarball install) still build cleanly.
flex = find_program('flex', required: false)
if flex.found()
  cmdlex_c = custom_target('cmdlex.c',
    input:   'cmdlex.l',
    output:  'cmdlex.c',
    command: [flex, '-o', '@OUTPUT@', '@INPUT@'],
    )
else
  cmdlex_c = files('cmdlex.c')
endif

version = meson.project_version()

lib_modemu2k = library(
  meson.project_name(),
  src, cmdlex_c,
  version: version,
  include_directories : ['..'],
  install: true,
  )

dep_modemu2k = declare_dependency(
  link_with: lib_modemu2k,
  include_directories : include_directories('..')
  )

pkg.generate(
  lib_modemu2k,
  description: 'PTY-based modem emulator library',
  )
