# barny

A customizable status bar for swaywm and wlroots-based Wayland compositors, featuring Apple-style "liquid glass" effects with real displacement/refraction.

![Barny Screenshot](current.png)

## Features

- **Liquid Glass Effects** - Real displacement/refraction rendering with multiple modes
- **Multi-Monitor Support** - Automatic detection and per-output rendering
- **Modular Design** - Built-in modules for clock, workspaces, weather, crypto, and system info
- **Sway Integration** - IPC communication for workspace events
- **Hot Reload** - inotify-based config file watching

## Dependencies

- wayland-client
- cairo
- pangocairo
- libjpeg
- cjson
- libcurl

## Notice

This project is currently being developed using a new workflow model.

The process follows a two-level waterfall approach combined with a PDCA cycle.

Stage 1 -- Vibe coding:
All planned features are implemented as quickly as possible. During this stage,
code quality, structure, and tooling choices are intentionally **ignored**.
Automated tools or AI-generated code may be used without constraint,
and consistency is not a priority. In simple words **I DONT CARE** and no one should.

Stage 2 -- Refactoring and Stabilization:
Once all planned functionality exists, the entire project will be refactored,
cleaned up, and standardized manually.

## Important
Until Stage 2 is complete, the project should be considered 
**unstable and effectively unusable for any serious or rational use**.

However its not that bad. Some code is still being written by me even on the first stage. 
Like modules/ is not vibecoded. Its mine. I used them for years.

## Building

Barny uses Meson as its build system and requires a C compiler with C2x support (GCC or Clang).

```bash
meson setup build
meson compile -C build
```

To run tests:
```bash
meson test -C build
```

To install:
```bash
sudo meson install -C build
```

To reconfigure from scratch:
```bash
meson setup --wipe build
```

## Configuration

Barny reads configuration from:
1. `/etc/barny/barny.conf` (system-wide)
2. `~/.config/barny/barny.conf` (user override)

### Liquid Glass Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| `refraction` | none/lens/liquid | Refraction mode |
| `displacement_scale` | 0-50 | Distortion strength |
| `chromatic_aberration` | 0-5 | RGB channel separation |
| `noise_scale` | 0.01-0.1 | Perlin noise frequency |
| `noise_octaves` | 1-4 | Noise detail level |

### Refraction Modes

- **none** - Plain blurred glass
- **lens** - Smooth radial gradient displacement (lens bubble effect)
- **liquid** - Perlin noise-based displacement (organic wavy glass)

## Architecture

### Core Layers

| Layer | Location | Description |
|-------|----------|-------------|
| Wayland Client | `src/wayland/` | Protocol communication, layer shell surfaces, shared memory buffers |
| Rendering Engine | `src/render/` | Frame orchestration, blur, displacement maps |
| Module System | `src/modules/` | Plugin architecture with LEFT/CENTER/RIGHT positioning |
| IPC | `src/ipc/` | Sway socket communication, config file watching |
| Configuration | `src/config.c` | Key-value config parsing |

### Liquid Glass Pipeline

1. Load wallpaper, apply blur, adjust brightness
2. Generate displacement map (radial for lens, Perlin noise for liquid)
3. Apply displacement with chromatic aberration (separate R/G/B channel offsets)

### Key Files

- `include/barny.h` - Public API declarations and data structures
- `src/main.c` - Entry point, epoll event loop, signal handlers
- `src/render/liquid_glass.c` - Core glass effects implementation
- `protocols/*.xml` - Wayland protocol definitions

## License

See [LICENSE](LICENSE) for details.
