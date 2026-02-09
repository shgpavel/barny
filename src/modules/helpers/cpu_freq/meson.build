cpu_freq_sources = files(
    'main.c',
)

barny_cpu_freq = executable(
    'barny-cpu-freq',
    cpu_freq_sources,
    install: true,
    install_dir: get_option('bindir'),
)

if init_system == 'systemd'
    install_data(
        'cpu_freq.service',
        install_dir: '/usr/lib/systemd/user',
        rename: 'barny-cpu-freq.service',
    )
endif
