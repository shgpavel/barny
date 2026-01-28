weather_sources = files(
    'main.c',
    'get_location.c',
    'get_weather.c',
    'write_callback.c',
)

weather_deps = [
    dependency('libcurl'),
    dependency('libcjson'),
]

barny_weather = executable(
    'barny-weather',
    weather_sources,
    dependencies: weather_deps,
    install: true,
    install_dir: get_option('bindir'),
)

# Install systemd user service
install_data(
    'weather.service',
    install_dir: '/usr/lib/systemd/user',
    rename: 'barny-weather.service',
)
