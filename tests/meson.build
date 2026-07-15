test_cases = [
    'answer',
    'app_io',
    'atcmd',
    'comm_program',
    'connect',
    'describe',
    'dial_crlf',
    'dial_step',
    'log_level',
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

helper_cases = [
    'answer',
    'connect',
    'describe',
    'dial_crlf',
    'dial_step',
    'ipv6',
    'listen',
    'listen_ipv6',
]

foreach case : test_cases
    name = 'test_' + case
    srcs = [name + '.c']
    if case in helper_cases
        srcs += 'test_helpers.c'
    endif
    exe = executable(
        name,
        srcs,
        include_directories: '../src',
        # M2K_SUPPRESS_DEPRECATED keeps tests that still exercise the
        # deprecated synchronous API (m2k_dial / m2k_online) compiling
        # clean. Drop it once those functions are removed.
        c_args: ['-DUSE_AS_TEST_LIB', '-DM2K_SUPPRESS_DEPRECATED'],
        dependencies: [dep_modemu2k],
    )
    test(name, exe, suite: 'unit')
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
            suite: 'integration',
            # docker_telnet uses a fixed container name and host port, so it can't
            # run concurrently with itself (e.g. under `meson test --repeat`).
            is_parallel: case != 'docker_telnet',
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
            suite: 'cli',
        )
    endforeach
endif
