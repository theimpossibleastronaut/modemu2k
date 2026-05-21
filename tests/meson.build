# TODO: Add more tests!

test_cases = [
  'connect',
  'timeval',
  'sockbuf',
  'atcmd',
  'ipv6',
  'listen',
  'listen_ipv6',
  'localhost',
  'ttybuf',
  ]

foreach case : test_cases
  name = 'test_' + case
  exe = executable(
    name,
    name + '.c',
    include_directories: '../src',
    c_args : ['-DUSE_AS_TEST_LIB'],
    dependencies: [dep_modemu2k]
    )
  test(name, exe)
endforeach

python3 = find_program('python3', required: false)
if python3.found()
  py_test_cases = {
    'listen_integration': 'test_listen_integration.py',
    'dial_integration':   'test_dial_integration.py',
  }
  foreach case, script : py_test_cases
    test(
      'test_' + case,
      python3,
      args: [files(script), modemu2k_exe.full_path()],
      depends: modemu2k_exe,
      timeout: 30,
      )
  endforeach
endif

sh = find_program('sh', required: false)
if sh.found()
  test(
    'test_no_arg_usage',
    sh,
    args: [files('test_no_arg_usage.sh'), modemu2k_exe.full_path()],
    depends: modemu2k_exe,
    )
endif
