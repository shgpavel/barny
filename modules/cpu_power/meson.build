# Install cpu_power script
install_data(
    'i3cpu_power',
    install_dir: get_option('bindir'),
    rename: 'barny-cpu-power',
    install_mode: 'rwxr-xr-x',
)

# Install systemd user service
install_data(
    'cpu_power.service',
    install_dir: '/usr/lib/systemd/user',
    rename: 'barny-cpu-power.service',
)
