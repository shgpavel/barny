project(
    'barny',
    'c',
    version: '0.0.1',
    license: 'BSD-3-Clause',
    default_options: [
        'c_std=c2x',
        'warning_level=3',
        'optimization=2',
    ],
)

cc = meson.get_compiler('c')

wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
libjpeg = dependency('libjpeg')
cjson = dependency('libcjson')
libcurl = dependency('libcurl')
math = cc.find_library('m')

all_deps = [
    wayland_client,
    cairo,
    pangocairo,
    libjpeg,
    cjson,
    libcurl,
    math,
]

inc_dirs = include_directories('include')

protocols_dir = 'protocols'

wl_protocol_sources = []

protocols = [
    'xdg-shell',
    'wlr-layer-shell-unstable-v1',
]

foreach proto : protocols
    xml_file = files(protocols_dir / proto + '.xml')

    header = custom_target(
        proto + '-client-protocol.h',
        input: xml_file,
        output: proto + '-client-protocol.h',
        command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
    )

    source = custom_target(
        proto + '-protocol.c',
        input: xml_file,
        output: proto + '-protocol.c',
        command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    wl_protocol_sources += [header, source]
endforeach

subdir('src')

barny = executable(
    'barny',
    barny_sources,
    wl_protocol_sources,
    dependencies: all_deps,
    include_directories: inc_dirs,
    install: true,
)

subdir('tests')

install_data(
    'config/barny.conf',
    install_dir: get_option('sysconfdir') / 'barny',
)
