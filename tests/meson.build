# TODO: Add more tests!

test_cases = [
  'connect',
  'timeval',
  'sockbuf',
  'atcmd',
  'ipv6',
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
