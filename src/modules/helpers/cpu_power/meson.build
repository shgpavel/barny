cpu_power_sources = files(
    'main.c',
)

barny_cpu_power = executable(
    'barny-cpu-power',
    cpu_power_sources,
    install: true,
    install_dir: get_option('bindir'),
)

# Install systemd user service
install_data(
    'cpu_power.service',
    install_dir: '/usr/lib/systemd/user',
    rename: 'barny-cpu-power.service',
)

# Install udev rule for RAPL permissions
install_data(
    '99-rapl-permissions.rules',
    install_dir: '/etc/udev/rules.d',
)
