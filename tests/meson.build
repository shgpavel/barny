test_sources = files(
    'test_config.c',
    'test_liquid_glass.c',
    'test_main.c',
    'test_modules.c',
    'test_module_layout.c',
    'test_new_modules.c',
    'test_stubs.c',
)

test_support_sources = files(
    '../src/config.c',
    '../src/modules/clock.c',
    '../src/modules/crypto.c',
    '../src/modules/disk.c',
    '../src/modules/fileread.c',
    '../src/modules/layout.c',
    '../src/modules/layout_apply.c',
    '../src/modules/module.c',
    '../src/modules/network.c',
    '../src/modules/ram.c',
    '../src/modules/sysinfo.c',
    '../src/modules/weather.c',
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

# --- Internal function tests (include .c files directly) ---
test_internals_sources = files(
    'test_internals_main.c',
    'test_internals_stubs.c',
    'test_config_internals.c',
    'test_clock_internals.c',
    'test_disk_internals.c',
    'test_ram_internals.c',
    'test_network_internals.c',
    'test_workspace_internals.c',
    'test_tray_internals.c',
)

barny_test_internals = executable(
    'barny_test_internals',
    test_internals_sources,
    dependencies: all_deps,
    include_directories: test_inc_dirs,
    build_by_default: false,
)

test('barny_test_internals', barny_test_internals)

# --- IPC framing tests ---
barny_test_ipc = executable(
    'barny_test_ipc',
    files('test_ipc.c'),
    dependencies: all_deps,
    include_directories: test_inc_dirs,
    build_by_default: false,
)

test('barny_test_ipc', barny_test_ipc)

# --- Static analysis ---
run_clang_tidy = find_program('run-clang-tidy', required: false)
if run_clang_tidy.found()
    test('clang-tidy',
        find_program('../scripts/run-clang-tidy.sh'),
        args: [meson.project_build_root(), meson.project_source_root()],
        suite: 'static-analysis',
        timeout: 300,
        is_parallel: false)
endif

clangd_prog = find_program('clangd', required: false)
if clangd_prog.found()
    test('clangd-check',
        find_program('../scripts/run-clangd-check.sh'),
        args: [meson.project_build_root(), meson.project_source_root()],
        suite: 'static-analysis',
        timeout: 300,
        is_parallel: false)
endif

clang_prog = find_program('clang', required: false)
if clang_prog.found()
    test('clang-analyzer',
        find_program('../scripts/run-clang-analyze.sh'),
        args: [meson.project_build_root(), meson.project_source_root()],
        suite: 'static-analysis',
        timeout: 300,
        is_parallel: false)
endif
