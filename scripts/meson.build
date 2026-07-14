if not get_option('helper-scripts')
    subdir_done()
endif

install_data(
    files('m2k-minicom', 'm2k-picocom'),
    install_dir: get_option('bindir'),
    install_mode: 'rwxr-xr-x',
)
