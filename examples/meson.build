# Compilable examples for the public API. Each file mirrors a docblock
# in modemu2k.h via doxygen's @snippet, so building these (which happens
# as part of the regular `ninja`) guarantees the snippets in the docs
# can never drift from compilable code.
#
# `example_sources` is exposed unconditionally so the doc target's
# depend_files can list them without each entry needing an `examples/`
# prefix. Executables are only built in standalone mode — parent
# projects pulling us in as a subproject don't need them.
example_cases = [
  'm2k_set_log_fn',
  'm2k_setup_commx',
  'm2k_setup_listen',
  ]
example_sources = files()
foreach case : example_cases
  example_sources += files(case + '.c')
endforeach

if not meson.is_subproject()
  foreach case : example_cases
    executable(
      'example_' + case,
      case + '.c',
      dependencies: dep_modemu2k,
      install: false,
      )
  endforeach
endif
