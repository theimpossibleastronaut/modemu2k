
bindir = join_paths(get_option('prefix'), get_option('bindir'))

ul = configuration_data()
ul.set('BINDIR', bindir)
configure_file(input : 'm2k-minicom.in',
               output : 'm2k-minicom',
               configuration : ul)
install_data(
  files(join_paths(meson.current_build_dir(), 'm2k-minicom')), install_dir : get_option('bindir'))

docs_cfg = configuration_data()
docs_cfg.set('VERSION', meson.project_version())
docs_cfg.set('SOURCE_ROOT', meson.project_source_root())
configure_file(
  input : 'publish-docs.in',
  output : 'publish-docs',
  configuration : docs_cfg,
)
