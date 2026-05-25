project(
  'modemu2k',
  'c',
  version: '0.2.0',
  meson_version : '>= 0.56.0',
  default_options: [
    'warning_level=3',
    'c_std=gnu99',
    ]
  )

add_global_arguments(
  ['-D_GNU_SOURCE'],
  language : 'c'
  )

cc = meson.get_compiler('c')

extra_flags = [
  '-fno-common',
  '-fstack-protector-strong',
  '-Wformat-security',
  '-Wformat-overflow=2'
]

add_project_arguments(cc.get_supported_arguments(extra_flags), language: 'c')

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_STRING', meson.project_name())
conf.set_quoted('PACKAGE_URL', 'http://theimpossibleastronaut.com/modemu2k/')
conf.set_quoted('PACKAGE_BUGREPORT', 'https://github.com/theimpossibleastronaut/modemu2k/issues')

config_h = configure_file(output : 'config.h', configuration : conf)

pkg = import('pkgconfig')

version_components = meson.project_version().split('.')
version_conf = configuration_data()
version_conf.set('MODEMU2K_VERSION_MAJOR', version_components[0])
version_conf.set('MODEMU2K_VERSION_MINOR', version_components[1])
version_conf.set('MODEMU2K_VERSION_PATCH', version_components[2])
version_conf.set_quoted('MODEMU2K_VERSION', meson.project_version())
version_h = configure_file(output: 'modemu2k_version.h', configuration: version_conf)

subdir('src')

modemu2k_exe = executable(
  'modemu2k',
  ['src/main.c', 'src/cmdarg.c'],
  include_directories: 'src',
  dependencies: dep_modemu2k,
  install : true
  )

install_headers('modemu2k.h', version_h, subdir: 'modemu2k')

if get_option('gen-docs')
  doxygen = find_program('doxygen', required: true)
  doxyfile = configure_file(
    input:  'Doxyfile.in',
    output: 'Doxyfile',
    configuration: {
      'PROJECT_NAME':    meson.project_name(),
      'PROJECT_VERSION': meson.project_version(),
      'SOURCE_ROOT':     meson.project_source_root(),
      'BUILD_ROOT':      meson.project_build_root(),
    }
  )
  docs_tgt = custom_target('doxygen-html',
    output: 'html',
    command: [doxygen, doxyfile],
    depend_files: files(
      'modemu2k.h',
      'README.md',
      'QUICKSTART.md',
      'ChangeLog',
      'doxygen/changelog.md',
      'doxygen/header.html',
      'doxygen/footer.html',
      'doxygen/doxygen-custom.css',
      'doxygen/DoxygenLayout.xml',
    ),
    install: true,
    install_dir: get_option('docdir'),
    build_by_default: true,
  )
  alias_target('docs', docs_tgt)
endif

subdir ('man')
subdir ('scripts')
subdir ('tests')

install_data(
  files(
    'README.md',
    'QUICKSTART.md',
    'COPYING',
    'AUTHORS',
    'ChangeLog',
    'ReleaseNotes',
    ),
  install_dir : get_option('docdir')
  )
