project(
    'barny',
    'c',
    version: '0.0.1',
    license: 'BSD-3-Clause',
    default_options: [
        'c_std=c2x',
        'warning_level=3',
        'buildtype=release',
        'strip=true',
        'werror=true',
        'prefix=/usr',
    ],
)

init_system = get_option('init_system')
preferred_compiler = get_option('compiler')
cc = meson.get_compiler('c')

if cc.get_id() == 'clang' and cc.has_argument('-Wno-c2y-extensions')
    add_project_arguments('-Wno-c2y-extensions', language: 'c')
endif

if cc.get_id() != preferred_compiler
    error(
        'Compiler mismatch: -Dcompiler=' + preferred_compiler + ' but Meson detected "' + cc.get_id() +
        '". Configure with a matching compiler (for example: CC=' + preferred_compiler + ' meson setup build --wipe).'
    )
endif

wayland_client = dependency('wayland-client')
wayland_scanner = find_program('wayland-scanner')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
libjpeg = dependency('libjpeg')
cjson = dependency('libcjson')
libcurl = dependency('libcurl')
libsystemd = dependency('libsystemd')
math = cc.find_library('m')

all_deps = [
    wayland_client,
    cairo,
    pangocairo,
    libjpeg,
    cjson,
    libcurl,
    libsystemd,
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

sdl3 = dependency('sdl3', required: false)
if sdl3.found()
    barny_layout_editor = executable(
        'barny-layout-editor',
        files(
            'src/editor/main.c',
            'src/config.c',
            'src/modules/layout.c',
            'src/util.c',
        ),
        dependencies: [sdl3, pangocairo, math],
        include_directories: inc_dirs,
        install: true,
    )
else
    warning('SDL3 not found; barny-layout-editor will not be built')
endif

subdir('tests')
subdir('src/modules/helpers')

# Install config file
install_data(
    'config/barny.conf',
    install_dir: get_option('sysconfdir') / 'barny',
)

# Install service files based on init system
if init_system == 'systemd'
    install_data(
        'config/barny.service',
        install_dir: '/usr/lib/systemd/user',
    )
elif init_system == 'openrc'
    # Configure and install barny-session launcher script
    barny_session = configure_file(
        input: 'config/barny-session.in',
        output: 'barny-session',
        configuration: {'BINDIR': get_option('prefix') / get_option('bindir')},
    )
    install_data(
        barny_session,
        install_dir: get_option('bindir'),
        install_mode: 'rwxr-xr-x',
    )
endif

# Post-install script: create dirs, enable and start services
meson.add_install_script(
    'scripts/post-install.sh',
    get_option('prefix'),
    get_option('bindir'),
)

# Uninstall target
uninstall_script = files('scripts/uninstall.sh')
run_target('uninstall-barny',
    command: ['sudo', 'sh', uninstall_script],
)
