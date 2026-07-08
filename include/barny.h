#ifndef BARNY_H
#define BARNY_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <systemd/sd-bus.h>

#define BARNY_VERSION        "0.1.0"
#define BARNY_DEFAULT_HEIGHT 48
#define BARNY_BORDER_RADIUS  28
#define BARNY_BLUR_RADIUS    2
#define BARNY_MAX_MODULES    32

typedef struct barny_config barny_config_t;
typedef struct barny_state  barny_state_t;
typedef struct barny_output barny_output_t;
typedef struct barny_module barny_module_t;
typedef struct barny_menu   barny_menu_t;

typedef enum {
	BARNY_POS_LEFT,
	BARNY_POS_CENTER,
	BARNY_POS_RIGHT
} barny_position_t;

struct barny_module {
	const char      *name;
	barny_position_t position;
	int              (*init)(barny_module_t *self, barny_state_t *state);
	void             (*destroy)(barny_module_t *self);
	void             (*update)(barny_module_t *self);
	void             (*render)(barny_module_t *self, cairo_t *cr, int x, int y, int w,
	                           int h);
	void             (*on_click)(barny_module_t *self, int button, int x, int y);
	void             (*on_hover)(barny_module_t *self, bool hovering, int x, int y);
	void            *data;
	int              render_x;
	int              width;
	int              height;
	bool             dirty;
	int              update_interval_ms;
	uint64_t         last_update_ms;
};

typedef enum {
	BARNY_REFRACT_NONE,
	BARNY_REFRACT_LENS,
	BARNY_REFRACT_LIQUID,
} barny_refraction_mode_t;

struct barny_config {
	int                     height;
	int                     margin_top;
	int                     margin_bottom;
	int                     margin_left;
	int                     margin_right;
	int                     border_radius;
	bool                    position_top;
	char                   *font;
	char                   *wallpaper_path;
	double                  blur_radius;
	double                  brightness;

	char                   *text_color;
	double                  text_color_r;
	double                  text_color_g;
	double                  text_color_b;
	bool                    text_color_set;

	int                     workspace_indicator_size;
	int                     workspace_spacing;
	char                  **workspace_names;
	int                     workspace_name_count;
	char                   *workspace_shape;
	int                     workspace_corner_radius;

	int                     module_spacing;
	char                   *modules_left;
	char                   *modules_center;
	char                   *modules_right;

	char                  **crypto_pairs;
	int                     crypto_pair_count;
	int                     crypto_popup_gap;
	char                   *crypto_currency_symbol;
	bool                    crypto_symbol_suffix;
	int                     crypto_decimals;

	bool                    sysinfo_freq_combined;
	int                     sysinfo_freq_decimals;
	int                     sysinfo_power_decimals;
	int                     sysinfo_popup_gap;
	bool                    sysinfo_popup_per_core;
	int                     sysinfo_p_cores;
	int                     sysinfo_e_cores;
	int                     sysinfo_item_spacing;
	bool                    sysinfo_freq_show_unit;
	bool                    sysinfo_freq_label_space;
	bool                    sysinfo_freq_unit_space;
	bool                    sysinfo_power_unit_space;
	bool                    sysinfo_temp_unit_space;

	int                     tray_icon_size;
	int                     tray_icon_spacing;
	char                   *tray_icon_shape;
	int                     tray_icon_corner_radius;
	double                  tray_icon_bg_r;
	double                  tray_icon_bg_g;
	double                  tray_icon_bg_b;
	double                  tray_icon_bg_opacity;
	int                     tray_menu_gap;

	barny_refraction_mode_t refraction_mode;
	double                  displacement_scale;
	double                  chromatic_aberration;
	double                  edge_refraction;
	double                  noise_scale;
	int                     noise_octaves;

	bool                    clock_show_time;
	bool                    clock_24h_format;
	bool                    clock_show_seconds;
	bool                    clock_show_date;
	bool                    clock_show_year;
	bool                    clock_show_month;
	bool                    clock_show_day;
	bool                    clock_show_weekday;
	int                     clock_date_order;
	char                    clock_date_separator;

	char                   *disk_path;
	char                   *disk_mode;
	int                     disk_decimals;
	bool                    disk_unit_space;

	char                   *sysinfo_temp_path;
	int                     sysinfo_temp_zone;
	bool                    sysinfo_temp_show_unit;

	char                   *ram_mode;
	int                     ram_decimals;
	char                   *ram_used_method;
	bool                    ram_unit_space;

	char                   *network_interface;
	bool                    network_show_ip;
	bool                    network_show_interface;
	bool                    network_prefer_ipv4;
	int                     network_popup_gap;
	bool                    network_popup_show_ssid;
	bool                    network_popup_show_ipv6;
	bool                    network_popup_show_mac;

	char                   *fileread_path;
	char                   *fileread_title;
	int                     fileread_max_chars;

	char                   *battery_path;
	bool                    battery_show_status;
	bool                    battery_unit_space;

	int                     windowtitle_max_length;
	char                   *windowtitle_empty_text;

	int                     weather_popup_gap;
	bool                    weather_popup_show_humidity;
	bool                    weather_popup_show_wind;
	bool                    weather_popup_show_pressure;
	bool                    weather_popup_show_feels_like;
};

typedef struct barny_module_layout {
	char *left[BARNY_MAX_MODULES];
	int   left_count;
	char *center[BARNY_MAX_MODULES];
	int   center_count;
	char *right[BARNY_MAX_MODULES];
	int   right_count;
} barny_module_layout_t;

struct barny_output {
	struct wl_output             *wl_output;
	struct wl_surface            *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	struct wl_buffer             *buffer;
	cairo_surface_t              *cairo_surface;
	cairo_t                      *cr;
	void                         *shm_data;
	int                           shm_size;

	int32_t                       width;
	int32_t                       height;
	int32_t                       surf_width;
	int32_t                       surf_height;
	int32_t                       pad_left;
	int32_t                       pad_right;
	int32_t                       pad_top;
	int32_t                       pad_bottom;
	int32_t                       mode_height;
	int32_t                       scale;
	char                         *name;
	uint32_t                      registry_name;

	bool                          configured;
	bool                          frame_pending;
	bool                          redraw_queued;

	cairo_surface_t              *bg_cache;
	cairo_surface_t              *lens_map;

	barny_state_t                *state;
	struct barny_output          *next;
};

struct barny_state {
	struct wl_display          *display;
	struct wl_registry         *registry;
	struct wl_compositor       *compositor;
	struct wl_shm              *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_seat             *seat;
	struct wl_pointer          *pointer;

	barny_output_t             *outputs;

	barny_output_t             *pointer_output;
	double                      pointer_x, pointer_y;
	struct wl_surface          *pointer_surface;

	barny_menu_t               *menu;

	struct wl_keyboard         *keyboard;

	barny_module_t             *hover_module;

	uint32_t                    axis_source;
	double                      touchpad_scroll_accum;

	barny_module_t             *modules[BARNY_MAX_MODULES];
	int                         module_count;

	barny_config_t              config;

	cairo_surface_t            *wallpaper;
	cairo_surface_t            *blurred_wallpaper;
	cairo_surface_t
	                *displaced_wallpaper;
	cairo_surface_t *displacement_map;

	int              epoll_fd;
	bool             running;

	int              sway_ipc_fd;

	sd_bus          *dbus;
	int              dbus_fd;
};

int
barny_wayland_init(barny_state_t *state);
void
barny_wayland_cleanup(barny_state_t *state);

int
barny_output_create_surface(barny_output_t *output);
void
barny_output_destroy_surface(barny_output_t *output);
int
barny_output_create_buffer(barny_output_t *output);
void
barny_output_request_frame(barny_output_t *output);

void
barny_render_frame(barny_output_t *output);
void
barny_render_liquid_glass(barny_output_t *output, cairo_t *cr);
void
barny_render_modules(barny_output_t *output, cairo_t *cr);

void
barny_rounded_rect_path(cairo_t *cr, double x, double y, double w, double h,
                        double r);
void
barny_paint_glass_bg(cairo_t *cr, cairo_surface_t *bg, int out_w, int out_h,
                     int screen_x, int screen_y, int target_h,
                     bool position_top);
void
barny_draw_glass_frame(cairo_t *cr, double w, double h, double r);

void
barny_blur_surface(cairo_surface_t *surface, int radius);
void
barny_apply_brightness(cairo_surface_t *surface, double factor);
void
barny_apply_vibrancy(cairo_surface_t *surface, double saturation,
                     double brightness);
cairo_surface_t *
barny_create_edge_lens_map(int w, int h, int radius, double edge_w,
                           double max_disp);
cairo_surface_t *
barny_load_wallpaper(const char *path);

cairo_surface_t *
barny_create_displacement_map(int width, int height, barny_refraction_mode_t mode,
                              int border_radius, double edge_strength,
                              double noise_scale, int noise_octaves);
void
barny_apply_displacement(cairo_surface_t *src, cairo_surface_t *dst,
                         cairo_surface_t *displacement_map, double scale,
                         double chromatic);

void
barny_module_register(barny_state_t *state, barny_module_t *module);
void
barny_modules_init(barny_state_t *state);
void
barny_modules_update(barny_state_t *state);
void
barny_modules_destroy(barny_state_t *state);
void
barny_modules_mark_dirty(barny_state_t *state);

barny_module_t *
barny_module_find(barny_state_t *state, const char *name);

bool
barny_modules_any_dirty(const barny_state_t *state);

int
barny_module_render_text(cairo_t *cr, PangoFontDescription *font,
                         const char *text, int x, int y, int h,
                         const barny_config_t *cfg, double fb_r, double fb_g,
                         double fb_b, double alpha);

barny_module_t *
barny_module_clock_create(void);
barny_module_t *
barny_module_workspace_create(void);
void
barny_workspace_refresh(barny_module_t *mod);
barny_module_t *
barny_module_weather_create(void);
barny_module_t *
barny_module_crypto_create(void);
barny_module_t *
barny_module_sysinfo_create(void);
barny_module_t *
barny_module_tray_create(void);
barny_module_t *
barny_module_disk_create(void);
barny_module_t *
barny_module_ram_create(void);
barny_module_t *
barny_module_network_create(void);
barny_module_t *
barny_module_fileread_create(void);
barny_module_t *
barny_module_battery_create(void);
barny_module_t *
barny_module_windowtitle_create(void);
void
barny_windowtitle_refresh(barny_module_t *mod);

void
barny_module_layout_init(barny_module_layout_t *layout);
void
barny_module_layout_destroy(barny_module_layout_t *layout);
void
barny_module_layout_set_defaults(barny_module_layout_t *layout);
int
barny_module_layout_load_from_config(const barny_config_t  *config,
                                     barny_module_layout_t *layout);
bool
barny_module_layout_contains(const barny_module_layout_t *layout,
                             const char                  *name);
bool
barny_module_catalog_has(const char *name);
int
barny_module_catalog_names(const char **names, int max_names);
int
barny_module_layout_gap_units(const char *name);
int
barny_module_layout_insert(barny_module_layout_t *layout,
                           barny_position_t position, const char *name, int index);
bool
barny_module_layout_remove(barny_module_layout_t *layout, const char *name);
char *
barny_module_layout_serialize_csv(const char *const *names, int count);
int
barny_module_layout_apply_to_state(const barny_module_layout_t *layout,
                                   barny_state_t               *state);

int
barny_dbus_init(barny_state_t *state);
void
barny_dbus_cleanup(barny_state_t *state);
int
barny_dbus_dispatch(barny_state_t *state);

int
barny_sni_watcher_init(barny_state_t *state);
void
barny_sni_watcher_set_host_registered(bool registered);
void
barny_sni_watcher_cleanup(barny_state_t *state);

typedef struct sni_item sni_item_t;

struct sni_item {
	char            *service;
	char            *object_path;
	char            *id;
	char            *title;
	char            *status;
	char            *icon_name;
	cairo_surface_t *icon;
	sni_item_t      *next;
};

int
barny_sni_host_init(barny_state_t *state);
void
barny_sni_host_cleanup(barny_state_t *state);
sni_item_t *
barny_sni_host_get_items(barny_state_t *state);
void
barny_sni_item_activate(barny_state_t *state, sni_item_t *item, int x, int y);
void
barny_sni_item_secondary_activate(barny_state_t *state, sni_item_t *item, int x,
                                  int y);

typedef struct barny_menu_item barny_menu_item_t;

struct barny_menu_item {
	int                id;
	char              *label;
	bool               enabled;
	bool               visible;
	bool               separator;
	bool               has_submenu;
	int                toggle_state;
	barny_menu_item_t *children;
	int                child_count;
};

char *
barny_sni_item_menu_path(barny_state_t *state, sni_item_t *item);
bool
barny_sni_item_is_menu(barny_state_t *state, sni_item_t *item);
barny_menu_item_t *
barny_dbusmenu_get_layout(barny_state_t *state, const char *service,
                          const char *menu_path);
void
barny_dbusmenu_free(barny_menu_item_t *root);
void
barny_dbusmenu_about_to_show(barny_state_t *state, const char *service,
                             const char *menu_path, int id);
void
barny_dbusmenu_event_clicked(barny_state_t *state, const char *service,
                             const char *menu_path, int id);

void
barny_menu_open(barny_state_t *state, sni_item_t *item, int anchor_x);
void
barny_menu_close(barny_state_t *state);
bool
barny_menu_is_open(barny_state_t *state);
bool
barny_menu_owns_surface(barny_state_t *state, struct wl_surface *surface);
void
barny_menu_pointer_motion(barny_state_t *state, double sx, double sy);
void
barny_menu_pointer_button(barny_state_t *state, uint32_t button,
                          uint32_t button_state);
void
barny_menu_key_escape(barny_state_t *state);

int
barny_sway_ipc_init(barny_state_t *state);
void
barny_sway_ipc_cleanup(barny_state_t *state);
int
barny_sway_ipc_subscribe(barny_state_t *state, const char *events);
int
barny_sway_ipc_send(barny_state_t *state, uint32_t type, const char *payload);
char *
barny_sway_ipc_recv(barny_state_t *state, uint32_t *type);
char *
barny_sway_ipc_recv_sync(barny_state_t *state, uint32_t *type, int timeout_ms);

int
barny_config_load(barny_config_t *config, const char *path);
void
barny_config_defaults(barny_config_t *config);
void
barny_config_cleanup(barny_config_t *config);
void
barny_config_validate_font(const barny_config_t *config);
int
barny_config_write_module_layout(const char *path,
                                 const char *modules_left,
                                 const char *modules_center,
                                 const char *modules_right);

#endif
