project(
  'modemu2k',
  'c',
  version: '0.0.999',
#https://mesonbuild.com/Release-notes-for-0-56-0.html#mesonbuild_root-and-mesonsource_root-are-deprecated
  meson_version : '>= 0.56.0',
  default_options: ['warning_level=2']
  )

add_global_arguments(
  ['-fno-common', '-D_GNU_SOURCE'],
  language : 'c'
  )

cc = meson.get_compiler('c')

extra_flags = [
  ['-fstack-protector-strong'],
  ['-fanalyzer'],
  ['-Wformat-security'],
  ['-Wformat-overflow=2']
]

foreach cflag : extra_flags
  if cc.has_argument(cflag)
    add_project_arguments(cflag, language: 'c')
  endif
endforeach

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE_STRING', meson.project_name())
conf.set_quoted('PACKAGE_URL', 'https://github.com/theimpossibleastronaut/modemu2k')
conf.set_quoted('PACKAGE_BUGREPORT', 'https://github.com/theimpossibleastronaut/modemu2k/issues')

localedir = join_paths(get_option('prefix'), get_option('localedir'))
subdir('po')

config_h = configure_file(output : 'config.h', configuration : conf)

subdir('src')

executable(
  'modemu2k',
  'src/modemu2k.c',
  dependencies: dep_modemu2k,
  install : true
  )

subdir ('man')
subdir ('scripts')
subdir ('tests')

install_data(
  files(
    'README',
    'QuickStart',
    'COPYING',
    'AUTHORS',
    'ChangeLog',
    'ReleaseNotes',
    'TRANSLATE.md'
    ),
  install_dir : get_option('docdir')
  )

if get_option('nls')
  ul = configuration_data()
  ul.set('LOCALEDIR', localedir)
  ul.set('MESON_SOURCE_ROOT', meson.current_source_dir())
  ul.set('MESON_PROJECT_NAME', meson.project_name())
  configure_file(input : 'uninstall_langs.sh.in',
                 output : 'uninstall_langs.sh',
                 configuration : ul)
endif
