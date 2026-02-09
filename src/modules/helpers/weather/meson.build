weather_sources = files(
    'main.c',
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

if init_system == 'systemd'
    install_data(
        'weather.service',
        install_dir: '/usr/lib/systemd/user',
        rename: 'barny-weather.service',
    )
endif
