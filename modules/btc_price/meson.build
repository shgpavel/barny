btc_price_sources = files(
    'test.c',
    'get_price.c',
)

btc_price_deps = [
    dependency('libcurl'),
    dependency('libcjson'),
]

barny_btc_price = executable(
    'barny-btc-price',
    btc_price_sources,
    dependencies: btc_price_deps,
    install: true,
    install_dir: get_option('bindir'),
)

# Install systemd user service
install_data(
    'btc_price.service',
    install_dir: '/usr/lib/systemd/user',
    rename: 'barny-btc-price.service',
)
