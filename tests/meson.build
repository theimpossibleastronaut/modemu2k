# TODO: Add more tests!

test_cases = [
  'app_io',
  'atcmd',
  'comm_program',
  'connect',
  'dial_step',
  'error_buffer',
  'ipv6',
  'listen',
  'listen_ipv6',
  'localhost',
  'sockbuf',
  'telopt',
  'timeval',
  'ttybuf',
  ]

# Tests that dial out to public hosts; excluded in sandboxed builds
# (e.g. OBS, sbuild) via `meson test --no-suite network`.
network_cases = ['connect', 'ipv6']

foreach case : test_cases
  name = 'test_' + case
  exe = executable(
    name,
    name + '.c',
    include_directories: '../src',
    # M2K_SUPPRESS_DEPRECATED keeps tests that still exercise the
    # deprecated synchronous API (m2k_dial / m2k_online) compiling
    # clean. Drop it once those functions are removed.
    c_args : ['-DUSE_AS_TEST_LIB', '-DM2K_SUPPRESS_DEPRECATED'],
    dependencies: [dep_modemu2k]
    )
  if case in network_cases
    test(name, exe, suite: 'network')
  else
    test(name, exe)
  endif
endforeach

python3 = find_program('python3', required: false)
if python3.found()
  py_test_cases = [
    'listen_integration',
    'dial_integration',
    ]
  # The docker test brings up a real container; opt-in via -Ddocker-test=true
  # so the default `meson test` doesn't depend on the docker daemon or
  # silently skip a test the user didn't ask to run. The script itself
  # still skips at runtime if docker is unavailable.
  if get_option('docker-test')
    py_test_cases += 'docker_telnet'
  endif
  foreach case : py_test_cases
    name = 'test_' + case
    test(
      name,
      python3,
      args: [files(name + '.py'), modemu2k_exe.full_path()],
      depends: modemu2k_exe,
      timeout: 60,
      )
  endforeach
endif

sh = find_program('sh', required: false)
if sh.found()
  sh_test_cases = [
    'no_arg_usage',
    'options',
    'verbose',
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
