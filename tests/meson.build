# TODO: Add more tests!

test_cases = [
  'test_connect',
  ]

foreach case : test_cases
  exe = executable(
    case,
    case + '.c',
    include_directories: '../src',
    c_args : ['-DUSE_AS_TEST_LIB'],
    dependencies: [dep_modemu2k]
    )
  test(case, exe)
endforeach
