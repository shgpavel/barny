helper_util_lib = static_library(
    'helper_util',
    files('common/helper_util.c'),
    dependencies: [dependency('libcurl'), dependency('libcjson')],
    include_directories: include_directories('common'),
)

helper_util_dep = declare_dependency(
    link_with: helper_util_lib,
    include_directories: include_directories('common'),
)

subdir('weather')
subdir('cpu_freq')
subdir('cpu_power')
subdir('crypto_prices')
