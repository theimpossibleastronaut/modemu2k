project(
  'modemu2k',
  'c',
  version: '0.1.1',
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

subdir('src')

executable(
  'modemu2k',
  ['src/main.c', 'src/cmdarg.c'],
  dependencies: dep_modemu2k,
  install : true
  )

subdir ('man')
subdir ('scripts')
subdir ('tests')

install_data(
  files(
    'README.md',
    'QuickStart',
    'COPYING',
    'AUTHORS',
    'ChangeLog',
    'ReleaseNotes',
    ),
  install_dir : get_option('docdir')
  )

install_subdir('docs', install_dir : get_option('docdir'))
