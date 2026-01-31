#ifndef BARNY_H
#define BARNY_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <systemd/sd-bus.h>

#define BARNY_VERSION "0.1.0"
#define BARNY_DEFAULT_HEIGHT 48
#define BARNY_BORDER_RADIUS 28
#define BARNY_BLUR_RADIUS 2
#define BARNY_MAX_MODULES 32

typedef struct barny_config barny_config_t;
typedef struct barny_state barny_state_t;
typedef struct barny_output barny_output_t;
typedef struct barny_module barny_module_t;

typedef enum {
    BARNY_POS_LEFT,
    BARNY_POS_CENTER,
    BARNY_POS_RIGHT
} barny_position_t;

struct barny_module {
    const char *name;
    barny_position_t position;
    int (*init)(barny_module_t *self, barny_state_t *state);
    void (*destroy)(barny_module_t *self);
    void (*update)(barny_module_t *self);
    void (*render)(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h);
    void (*on_click)(barny_module_t *self, int button, int x, int y);
    void *data;
    int width;
    int height;
    bool dirty;
};

typedef enum {
    BARNY_REFRACT_NONE,      /* No displacement */
    BARNY_REFRACT_LENS,      /* Smooth lens/bubble effect */
    BARNY_REFRACT_LIQUID,    /* Turbulence/liquid effect using Perlin noise */
} barny_refraction_mode_t;

struct barny_config {
    int height;
    int margin_top;
    int margin_bottom;
    int margin_left;
    int margin_right;
    int border_radius;
    bool position_top;  /* true = top, false = bottom */
    char *font;
    char *wallpaper_path;
    double blur_radius;
    double brightness;

    /* Global text color */
    char *text_color;              /* NULL = default module colors, or "#XXXXXX" hex */
    double text_color_r;           /* Parsed RGB values (0.0-1.0) */
    double text_color_g;
    double text_color_b;
    bool text_color_set;           /* true if custom color is set */

    /* Workspace module */
    int workspace_indicator_size;  /* Diameter of workspace bubbles (default 24) */
    int workspace_spacing;         /* Space between bubbles (default 6) */

    /* Sysinfo module */
    bool sysinfo_freq_combined;    /* true = combined avg, false = "P: X.XX E: X.XX" */
    int sysinfo_power_decimals;    /* 0, 1, or 2 decimal places for watts */

    /* Tray module */
    int tray_icon_size;            /* Icon size in pixels (default 24) */
    int tray_icon_spacing;         /* Space between icons (default 4) */

    /* Liquid glass effect parameters */
    barny_refraction_mode_t refraction_mode;  /* Type of displacement */
    double displacement_scale;    /* Strength of lens/displacement effect (0-50) */
    double chromatic_aberration;  /* RGB channel separation (0-5) */
    double edge_refraction;       /* Extra displacement at edges (0-2) */
    double noise_scale;           /* Scale for Perlin noise (0.01-0.1) */
    int noise_octaves;            /* Perlin noise detail level (1-4) */

    /* Clock module */
    bool clock_show_time;
    bool clock_24h_format;
    bool clock_show_seconds;
    bool clock_show_date;
    bool clock_show_year;
    bool clock_show_month;
    bool clock_show_day;
    bool clock_show_weekday;
    int clock_date_order;         /* 0=dd/mm/yyyy, 1=mm/dd/yyyy, 2=yyyy/mm/dd */
    char clock_date_separator;

    /* Disk module */
    char *disk_path;
    bool disk_show_percentage;
    int disk_decimals;

    /* CPU temperature module */
    char *cpu_temp_path;
    int cpu_temp_zone;
    bool cpu_temp_show_unit;

    /* RAM module */
    bool ram_show_percentage;
    int ram_decimals;
    char *ram_used_method;        /* "available" or "free" */

    /* Network module */
    char *network_interface;      /* interface name or "auto" */
    bool network_show_ip;
    bool network_show_interface;
    bool network_prefer_ipv4;

    /* File read module */
    char *fileread_path;
    char *fileread_title;
    int fileread_max_chars;
};

/* Per-output state (for multi-monitor support) */
struct barny_output {
    struct wl_output *wl_output;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    /* Buffer management */
    struct wl_buffer *buffer;
    cairo_surface_t *cairo_surface;
    cairo_t *cr;
    void *shm_data;
    int shm_size;

    /* Dimensions */
    int32_t width;
    int32_t height;
    int32_t scale;
    char *name;

    /* State */
    bool configured;
    bool frame_pending;

    barny_state_t *state;
    struct barny_output *next;
};

/* Global application state */
struct barny_state {
    /* Wayland globals */
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_seat *seat;
    struct wl_pointer *pointer;

    /* Outputs (linked list) */
    barny_output_t *outputs;

    /* Currently focused output for pointer events */
    barny_output_t *pointer_output;
    double pointer_x, pointer_y;

    /* Modules */
    barny_module_t *modules[BARNY_MAX_MODULES];
    int module_count;

    /* Configuration */
    barny_config_t config;

    /* Liquid glass background */
    cairo_surface_t *wallpaper;
    cairo_surface_t *blurred_wallpaper;
    cairo_surface_t *displaced_wallpaper;  /* Wallpaper with displacement applied */
    cairo_surface_t *displacement_map;     /* R=X offset, G=Y offset */

    /* Event loop */
    int epoll_fd;
    bool running;

    /* Sway IPC */
    int sway_ipc_fd;

    /* D-Bus (for system tray) */
    sd_bus *dbus;
    int dbus_fd;
};

/* Wayland client functions */
int barny_wayland_init(barny_state_t *state);
void barny_wayland_cleanup(barny_state_t *state);
int barny_wayland_dispatch(barny_state_t *state);

/* Layer shell functions */
int barny_output_create_surface(barny_output_t *output);
void barny_output_destroy_surface(barny_output_t *output);
int barny_output_create_buffer(barny_output_t *output);
void barny_output_request_frame(barny_output_t *output);

/* Rendering functions */
void barny_render_frame(barny_output_t *output);
void barny_render_liquid_glass(barny_output_t *output, cairo_t *cr);
void barny_render_modules(barny_output_t *output, cairo_t *cr);

/* Effect functions */
void barny_blur_surface(cairo_surface_t *surface, int radius);
void barny_apply_brightness(cairo_surface_t *surface, double factor);
cairo_surface_t *barny_load_wallpaper(const char *path);

/* Liquid glass displacement effects */
cairo_surface_t *barny_create_displacement_map(int width, int height,
                                                barny_refraction_mode_t mode,
                                                int border_radius,
                                                double edge_strength,
                                                double noise_scale,
                                                int noise_octaves);
void barny_apply_displacement(cairo_surface_t *src, cairo_surface_t *dst,
                              cairo_surface_t *displacement_map,
                              double scale, double chromatic);

/* Module system */
void barny_module_register(barny_state_t *state, barny_module_t *module);
void barny_modules_init(barny_state_t *state);
void barny_modules_update(barny_state_t *state);
void barny_modules_destroy(barny_state_t *state);
void barny_modules_mark_dirty(barny_state_t *state);

/* Module implementations */
barny_module_t *barny_module_clock_create(void);
barny_module_t *barny_module_workspace_create(void);
void barny_workspace_refresh(barny_module_t *mod);
barny_module_t *barny_module_weather_create(void);
barny_module_t *barny_module_crypto_create(void);
barny_module_t *barny_module_sysinfo_create(void);
barny_module_t *barny_module_tray_create(void);
barny_module_t *barny_module_disk_create(void);
barny_module_t *barny_module_cpu_temp_create(void);
barny_module_t *barny_module_ram_create(void);
barny_module_t *barny_module_network_create(void);
barny_module_t *barny_module_fileread_create(void);

/* D-Bus (system tray support) */
int barny_dbus_init(barny_state_t *state);
void barny_dbus_cleanup(barny_state_t *state);
int barny_dbus_dispatch(barny_state_t *state);
int barny_dbus_get_fd(barny_state_t *state);

/* StatusNotifierWatcher */
int barny_sni_watcher_init(barny_state_t *state);
void barny_sni_watcher_cleanup(barny_state_t *state);

/* StatusNotifierHost */
typedef struct sni_item sni_item_t;

struct sni_item {
    char *service;           /* D-Bus service name */
    char *object_path;       /* Object path (usually /StatusNotifierItem) */
    char *id;
    char *title;
    char *status;            /* Passive, Active, NeedsAttention */
    char *icon_name;
    cairo_surface_t *icon;   /* Rendered icon surface */
    sni_item_t *next;
};

int barny_sni_host_init(barny_state_t *state);
void barny_sni_host_cleanup(barny_state_t *state);
sni_item_t *barny_sni_host_get_items(barny_state_t *state);
void barny_sni_item_activate(barny_state_t *state, sni_item_t *item, int x, int y);
void barny_sni_item_secondary_activate(barny_state_t *state, sni_item_t *item, int x, int y);

/* Sway IPC */
int barny_sway_ipc_init(barny_state_t *state);
void barny_sway_ipc_cleanup(barny_state_t *state);
int barny_sway_ipc_subscribe(barny_state_t *state, const char *events);
int barny_sway_ipc_send(barny_state_t *state, uint32_t type, const char *payload);
char *barny_sway_ipc_recv(barny_state_t *state, uint32_t *type);
char *barny_sway_ipc_recv_sync(barny_state_t *state, uint32_t *type, int timeout_ms);

/* Configuration */
int barny_config_load(barny_config_t *config, const char *path);
void barny_config_defaults(barny_config_t *config);
void barny_config_validate_font(const barny_config_t *config);

#endif /* BARNY_H */
