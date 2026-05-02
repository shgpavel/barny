crypto_prices_sources = files(
    'main.c',
)

crypto_prices_deps = [
    dependency('libcurl'),
    dependency('libcjson'),
    helper_util_dep,
]

barny_crypto_prices = executable(
    'barny-crypto-prices',
    crypto_prices_sources,
    dependencies: crypto_prices_deps,
    install: true,
    install_dir: get_option('bindir'),
)

if init_system == 'systemd'
    install_data(
        'crypto_prices.service',
        install_dir: '/usr/lib/systemd/user',
        rename: 'barny-crypto-prices.service',
    )
endif
