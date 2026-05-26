# TODO: Add more tests!

test_cases = [
  'app_io',
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
  py_test_cases = [
    'listen_integration',
    'dial_integration',
    ]
  foreach case : py_test_cases
    name = 'test_' + case
    test(
      name,
      python3,
      args: [files(name + '.py'), modemu2k_exe.full_path()],
      depends: modemu2k_exe,
      timeout: 30,
      )
  endforeach
endif

sh = find_program('sh', required: false)
if sh.found()
  sh_test_cases = [
    'no_arg_usage',
    'options',
    ]
  foreach case : sh_test_cases
    name = 'test_' + case
    test(
      name,
      sh,
      args: [files(name + '.sh'), modemu2k_exe.full_path()],
      depends: modemu2k_exe,
      )
  endforeach
endif
