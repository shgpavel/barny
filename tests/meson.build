test_sources = files(
    'test_config.c',
    'test_liquid_glass.c',
    'test_main.c',
    'test_modules.c',
    'test_stubs.c',
)

test_support_sources = files(
    '../src/config.c',
    '../src/modules/clock.c',
    '../src/modules/module.c',
    '../src/modules/workspace.c',
)

test_inc_dirs = include_directories('.', '../include')

barny_tests = executable(
    'barny_tests',
    test_sources,
    test_support_sources,
    dependencies: all_deps,
    include_directories: test_inc_dirs,
    build_by_default: false,
)

test('barny_tests', barny_tests)
