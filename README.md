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

If SDL3 is available, this also builds `barny-layout-editor` (graphical module
placement editor).

## Installation

Install barny and all modules:
```bash
sudo meson install -C build
```

Enable and start services (run as your regular user, not root):
```bash
systemctl --user daemon-reload
systemctl --user enable --now barny.service
```

## Uninstallation

```bash
ninja -C build uninstall-barny
```

This stops all services, removes binaries, and cleans up `/opt/barny/`.

## Modules

Barny uses external data provider modules that run as systemd user services:

| Module | Service | Data File | Description |
|--------|---------|-----------|-------------|
| Weather | `barny-weather` | `/opt/barny/modules/weather` | Weather data (requires API key) |
| BTC Price | `barny-btc-price` | `/opt/barny/modules/btc_price` | Bitcoin price from OKX |
| CPU Freq | `barny-cpu-freq` | `/opt/barny/modules/cpu_freq` | CPU frequency (P/E cores) |
| CPU Power | `barny-cpu-power` | `/opt/barny/modules/cpu_power` | CPU power consumption |

### Weather API Key

The weather module requires an API key:
```bash
echo "YOUR_API_KEY" | sudo tee /opt/barny/modules/api_key
```

## Configuration

Barny reads configuration from:
1. `/etc/barny/barny.conf` (system-wide)
2. `~/.config/barny/barny.conf` (user override)

### General Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `position` | top | Bar position: `top` or `bottom` |
| `height` | 48 | Bar height in pixels |
| `margin_*` | 0 | Margins from screen edges |
| `border_radius` | 28 | Corner radius for glass effect |
| `font` | Sans 12 | Pango font string |
| `wallpaper` | - | Path to wallpaper PNG for glass effect |
| `blur_radius` | 2 | Blur strength for glass background |
| `brightness` | 1.1 | Brightness multiplier |

### Workspace Module

| Parameter | Default | Description |
|-----------|---------|-------------|
| `workspace_indicator_size` | 24 | Diameter of workspace bubbles |
| `workspace_spacing` | 6 | Space between bubbles |

### Module Layout

| Parameter | Default | Description |
|-----------|---------|-------------|
| `modules_left` | `workspace` | CSV list of module IDs for left section |
| `modules_center` | *(empty)* | CSV list of module IDs for center section |
| `modules_right` | `clock, sysinfo, weather, disk, ram, network, fileread, crypto, tray` | CSV list of module IDs for right section |

If all three `modules_*` keys are omitted, Barny uses the legacy built-in
layout. If you set them explicitly (including empty values), Barny uses your
exact selection/order and allows an empty bar.

You can add proportional spacer tokens in these CSV lists:
- `gap:N` adds extra spacing of `N * module_spacing`
- Example: `modules_left = workspace, gap:3, clock`

### Graphical Layout Editor

Run:
```bash
barny-layout-editor
```

Optional config path:
```bash
barny-layout-editor ~/.config/barny/barny.conf
```

Controls:
- Drag blocks between the contiguous bar lane and `MODULE POOL`
- `S` saves contiguous placement into `modules_left` and emits `gap:N` tokens
- `R` resets to legacy defaults
- `C` clears all modules from the bar

The editor and runtime now keep module placement bounded by bar width while
considering each module's rendered width. If total content is too wide, gaps
are compressed first.

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
