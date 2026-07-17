src = [
    'atcmd.c',
    'comm_program.c',
    'm2k_api.c',
    'm2k_setup.c',
    'm2k_step.c',
    'sockbuf.c',
    'stty.c',
    'timeval.c',
    'verbose.c',
    'sock.c',
    'telopt.c',
    'ttybuf.c',
    'utils.c',
]

# Regenerate cmdlex.c from cmdlex.l when flex is available; otherwise
# fall back to the committed pre-generated copy so consumers without
# flex (typical release tarball install) still build cleanly.
flex = find_program('flex', required: false)
if flex.found()
    cmdlex_c = custom_target(
        'cmdlex.c',
        input: 'cmdlex.l',
        output: 'cmdlex.c',
        command: [flex, '-o', '@OUTPUT@', '@INPUT@'],
    )
else
    cmdlex_c = files('cmdlex.c')
endif

# Isolate the generated lexer so we can relax warnings flex's output
# trips (e.g. clang's -Wsign-compare on yy_size_t vs int). The rest of
# the library keeps the project's strict flags.
cmdlex_lib = static_library(
    'm2k-cmdlex',
    cmdlex_c,
    c_args: ['-Wno-sign-compare'],
    include_directories: ['..'],
    pic: true,
    gnu_symbol_visibility: 'hidden',
)

version = meson.project_version()

# Export only the public m2k_* API. Everything else is hidden by
# compiling with -fvisibility=hidden; the M2K_API macro in modemu2k.h
# (visibility("default")) re-exposes the public symbols. Without this,
# internal symbols leak — notably flex's yylex/yyrestart/yyin — and ELF
# interposition makes an embedding host with its own lexer (dosemu2's
# config parser) capture our lexer calls, silently breaking all AT
# command parsing.
lib_modemu2k = library(
    meson.project_name(),
    src,
    link_with: cmdlex_lib,
    version: version,
    include_directories: ['..'],
    gnu_symbol_visibility: 'hidden',
    install: true,
)

dep_modemu2k = declare_dependency(
    link_with: lib_modemu2k,
    include_directories: include_directories('..'),
)

# White-box tests need the internal symbols the version script hides,
# so give them a static archive of the same objects (no recompile).
lib_modemu2k_whitebox = static_library(
    'modemu2k-whitebox',
    objects: lib_modemu2k.extract_all_objects(recursive: true),
)

dep_modemu2k_whitebox = declare_dependency(
    link_with: [lib_modemu2k_whitebox, cmdlex_lib],
    include_directories: include_directories('..'),
)

pkg.generate(
    lib_modemu2k,
    description: 'PTY-based modem emulator library',
    subdirs: 'modemu2k',
)
