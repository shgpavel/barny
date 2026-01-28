# Install cpu_freq script
install_data(
    'i3cpu_freq',
    install_dir: get_option('bindir'),
    rename: 'barny-cpu-freq',
    install_mode: 'rwxr-xr-x',
)

# Install systemd user service
install_data(
    'cpu_freq.service',
    install_dir: '/usr/lib/systemd/user',
    rename: 'barny-cpu-freq.service',
)
