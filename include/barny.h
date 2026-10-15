#ifndef BARNY_H
#define BARNY_H

#include <math.h>
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
#define BARNY_BAR_OVERRUN    6

/* Analytic split of the glass frame lighting: the broad part is painted by
   barny_draw_broad_frame, the edge part is re-derived along the (possibly
   deformed) contour; broad + edge must sum to the gradients painted by
   barny_draw_glass_frame (glass.c). */
#define BARNY_FRAME_EDGE_TOP_A    0.08
#define BARNY_FRAME_EDGE_TOP_STOP 0.14

typedef struct barny_config barny_config_t;
typedef struct barny_state  barny_state_t;
typedef struct barny_output barny_output_t;
typedef struct barny_module barny_module_t;
typedef struct barny_menu   barny_menu_t;
typedef struct barny_rim    barny_rim_t;

typedef struct {
	int x;
	int y;
	int w;
	int h;
} barny_rect_t;

static inline bool
barny_rect_empty(barny_rect_t r)
{
	return r.w <= 0 || r.h <= 0;
}

static inline barny_rect_t
barny_rect_union(barny_rect_t a, barny_rect_t b)
{
	barny_rect_t r;
	int          x1;
	int          y1;

	if (barny_rect_empty(b))
		return a;
	if (barny_rect_empty(a))
		return b;

	r.x = a.x < b.x ? a.x : b.x;
	r.y = a.y < b.y ? a.y : b.y;
	x1  = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
	y1  = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
	r.w = x1 - r.x;
	r.h = y1 - r.y;

	return r;
}

static inline barny_rect_t
barny_rect_clamp(barny_rect_t r, int w, int h)
{
	int x0 = r.x;
	int y0 = r.y;
	int x1 = r.x + r.w;
	int y1 = r.y + r.h;

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > w)
		x1 = w;
	if (y1 > h)
		y1 = h;

	r.x = x0;
	r.y = y0;
	r.w = x1 > x0 ? x1 - x0 : 0;
	r.h = y1 > y0 ? y1 - y0 : 0;

	return r;
}

#define BARNY_SCALE_UNIT 120

static inline int
barny_scale_len(int v, uint32_t scale120)
{
	return (int)(((int64_t)v * scale120 + BARNY_SCALE_UNIT / 2)
	             / BARNY_SCALE_UNIT);
}

static inline int64_t
barny_div_floor(int64_t a, int64_t b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static inline barny_rect_t
barny_rect_to_buffer(barny_rect_t r, uint32_t scale120)
{
	int64_t x0;
	int64_t y0;
	int64_t x1;
	int64_t y1;

	if (barny_rect_empty(r))
		return (barny_rect_t){ 0 };

	x0 = barny_div_floor((int64_t)r.x * scale120, BARNY_SCALE_UNIT);
	y0 = barny_div_floor((int64_t)r.y * scale120, BARNY_SCALE_UNIT);
	x1 = -barny_div_floor(-(int64_t)(r.x + r.w) * scale120, BARNY_SCALE_UNIT);
	y1 = -barny_div_floor(-(int64_t)(r.y + r.h) * scale120, BARNY_SCALE_UNIT);

	return (barny_rect_t){ (int)x0, (int)y0, (int)(x1 - x0),
		               (int)(y1 - y0) };
}

static inline void
barny_snap_to_pixel(cairo_t *cr, double *x, double *y)
{
	cairo_user_to_device(cr, x, y);
	*x = round(*x);
	*y = round(*y);
	cairo_device_to_user(cr, x, y);
}

static inline void
barny_move_to_pixel(cairo_t *cr, double x, double y)
{
	barny_snap_to_pixel(cr, &x, &y);
	cairo_move_to(cr, x, y);
}

static inline void
barny_clip_buffer_rects(cairo_t *cr, const barny_rect_t *rects, int n,
                        uint32_t scale120)
{
	cairo_matrix_t m;
	barny_rect_t   r;
	int            i;

	cairo_get_matrix(cr, &m);
	cairo_identity_matrix(cr);
	cairo_new_path(cr);
	for (i = 0; i < n; i++) {
		r = barny_rect_to_buffer(rects[i], scale120);
		cairo_rectangle(cr, r.x, r.y, r.w, r.h);
	}
	cairo_set_matrix(cr, &m);
	cairo_clip(cr);
}

typedef struct {
	struct wp_viewport            *viewport;
	struct wp_fractional_scale_v1 *fractional;
	uint32_t                       preferred;
	void                         (*changed)(void *data);
	void                          *data;
} barny_scaler_t;

void
barny_scaler_init(barny_scaler_t *sc, barny_state_t *state,
                  struct wl_surface *surface, void (*changed)(void *),
                  void *data);
void
barny_scaler_fini(barny_scaler_t *sc);
uint32_t
barny_scaler_scale(const barny_scaler_t *sc, uint32_t guess120,
                   int32_t output_scale);
void
barny_scaler_apply(const barny_scaler_t *sc, struct wl_surface *surface,
                   uint32_t scale120, int w, int h);

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
	bool                    tray_overflow;

	bool                    dynamic_glass;
	double                  glass_gleam;
	double                  glass_bulge;
	double                  glass_prism;
	double                  glass_prism_flow;
	int                     glass_prism_fps;
	bool                    popup_animations;

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
	uint32_t                      scale120;
	barny_scaler_t                scaler;
	char                         *name;
	uint32_t                      registry_name;

	struct wl_callback           *frame_cb;
	bool                          configured;
	bool                          frame_pending;
	bool                          redraw_queued;

	/* where each module landed on THIS output; the layout differs per
	   output (widths differ, modules get dropped), so input hit-testing
	   must never share one geometry across outputs */
	int                           mod_x[BARNY_MAX_MODULES];
	int                           mod_w[BARNY_MAX_MODULES];
	int                           mod_width[BARNY_MAX_MODULES];
	bool                          mod_width_valid;
	uint32_t                      mod_stale;

	cairo_surface_t              *bg_cache;
	cairo_surface_t              *lens_map;
	cairo_surface_t              *shadow_cache;
	cairo_surface_t              *glass_clean;
	bool                          glass_clean_opaque;

	/* droplet scratch, reused across frames: the patch geometry is fixed
	   for a given bar, so allocating a surface and the distance-field
	   buffers per frame was pure churn */
	cairo_surface_t              *lens_patch;
	int                           lens_patch_w;
	int                           lens_patch_h;
	float                        *lens_field;
	double                       *lens_cols;

	/* rect the droplet dirtied last frame; the next frame restores just
	   that strip from bg_cache instead of repainting the whole bar */
	barny_rect_t                  lens_dmg;
	bool                          lens_dmg_valid;

	barny_rim_t                  *rim;
	uint64_t                      rim_tick;
	unsigned int                  rim_mods_touch;

	/* while set, modules that fall outside clip skip their render
	   entirely (a lens frame must not re-shape text it cannot show) */
	bool                          render_clipped;
	barny_rect_t                  clip;

	barny_state_t                *state;
	struct barny_output          *next;
};

struct barny_state {
	struct wl_display          *display;
	struct wl_registry         *registry;
	struct wl_compositor       *compositor;
	struct wl_shm              *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wp_viewporter       *viewporter;
	struct wp_fractional_scale_manager_v1 *fractional_scale;
	struct wl_seat             *seat;
	struct wl_pointer          *pointer;

	barny_output_t             *outputs;

	barny_output_t             *pointer_output;
	double                      pointer_x, pointer_y;
	struct wl_surface          *pointer_surface;

	barny_output_t             *dyn_output;
	bool                        dyn_dirty;

	double                      lens_x, lens_vx;
	double                      lens_scale, lens_sv;
	double                      lens_target_scale;
	uint64_t                    lens_prev_us;
	bool                        lens_animating;

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
bool
barny_lens_advance(barny_output_t *output, double dt);
bool
barny_lens_step(barny_output_t *output);
/* rect the droplet covers this frame; false when it is not drawn */
bool
barny_lens_rect(barny_output_t *output, int *x, int *y, int *w, int *h);
void
barny_output_free_lens_cache(barny_output_t *output);
void
barny_output_free_glass_caches(barny_output_t *output, bool keep_lens_map);
bool
barny_rim_flowing(const barny_config_t *config);
uint64_t
barny_rim_tick(const barny_config_t *config, uint64_t now_us);
double
barny_rim_phase(const barny_config_t *config, uint64_t tick);
int
barny_rim_timeout_ms(const barny_config_t *config, uint64_t now_us);
bool
barny_rim_due(const barny_output_t *output, uint64_t now_us);
int
barny_rim_rects(const barny_output_t *output, barny_rect_t *rects);
void
barny_output_free_rim(barny_output_t *output);
void
barny_render_modules(barny_output_t *output, cairo_t *cr);
/* rect the module occupies on this output, false when it is not drawn there */
bool
barny_output_module_rect(const barny_output_t *output,
                         const barny_module_t *mod, int *x, int *w);
/* same, for the output the pointer is currently on */
bool
barny_pointer_module_rect(barny_state_t *state, const barny_module_t *mod,
                          int *x, int *w);

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
barny_draw_broad_frame(cairo_t *cr, double w, double h);
/* The three below run per pixel inside the lens and morph loops -- five signed
   distances and a sample each. They live in the header so those loops can
   inline them: as calls into another unit they cost the droplet more time than
   its own shading did. */

static inline double
barny_sd_round_rect(double px, double py, double hw, double hh, double r)
{
	double qx      = fabs(px) - (hw - r);
	double qy      = fabs(py) - (hh - r);
	double ax      = qx > 0 ? qx : 0;
	double ay      = qy > 0 ? qy : 0;
	double outside = sqrt(ax * ax + ay * ay);
	double mq      = qx > qy ? qx : qy;
	double inside  = mq < 0 ? mq : 0;

	return outside + inside - r;
}

/* Polynomial smooth-minimum: blends two signed distances so the union of two
   shapes grows a concave "surface tension" fillet instead of a hard corner. */
static inline double
barny_smin(double a, double b, double k)
{
	double h;

	if (k <= 0.0)
		return a < b ? a : b;

	h = 0.5 + 0.5 * (b - a) / k;
	if (h < 0.0)
		h = 0.0;
	if (h > 1.0)
		h = 1.0;

	return (b * (1.0 - h) + a * h) - k * h * (1.0 - h);
}

static inline void
barny_sample_bilinear(uint8_t *data, int stride, int width, int height,
                      double x, double y, uint8_t *out)
{
	int      x0;
	int      y0;
	int      x1;
	int      y1;
	double   fx;
	double   fy;
	uint8_t *p00;
	uint8_t *p10;
	uint8_t *p01;
	uint8_t *p11;
	int      i;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= width - 1)
		x = width - 1.001;
	if (y >= height - 1)
		y = height - 1.001;

	x0  = (int)x;
	y0  = (int)y;
	x1  = x0 + 1;
	y1  = y0 + 1;

	fx  = x - x0;
	fy  = y - y0;

	p00 = data + y0 * stride + x0 * 4;
	p10 = data + y0 * stride + x1 * 4;
	p01 = data + y1 * stride + x0 * 4;
	p11 = data + y1 * stride + x1 * 4;

	for (i = 0; i < 4; i++) {
		double v0 = p00[i] + (p10[i] - p00[i]) * fx;
		double v1 = p01[i] + (p11[i] - p01[i]) * fx;
		double v  = v0 + (v1 - v0) * fy;

		out[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
	}
}

static inline uint8_t
barny_sample_bilinear_ch(uint8_t *data, int stride, int width, int height,
                         double x, double y, int ch)
{
	int      x0;
	int      y0;
	double   fx;
	double   fy;
	uint8_t *p00;
	uint8_t *p01;
	double   v0;
	double   v1;
	double   v;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= width - 1)
		x = width - 1.001;
	if (y >= height - 1)
		y = height - 1.001;

	x0  = (int)x;
	y0  = (int)y;
	fx  = x - x0;
	fy  = y - y0;
	p00 = data + y0 * stride + x0 * 4 + ch;
	p01 = p00 + stride;
	v0  = p00[0] + (p00[4] - p00[0]) * fx;
	v1  = p01[0] + (p01[4] - p01[0]) * fx;
	v   = v0 + (v1 - v0) * fy;

	return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}
void
barny_blur_surface(cairo_surface_t *surface, int radius);
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

PangoLayout *
barny_text_layout(cairo_t *cr, PangoFontDescription *font, const char *text);

void
barny_text_cache_clear(void);

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
barny_menu_open(barny_state_t *state, sni_item_t *item, int anchor_x,
                int anchor_w);
void
barny_tray_menu_open(barny_state_t *state, int anchor_x, int anchor_y,
                     int anchor_w);
void
barny_menu_close(barny_state_t *state);
void
barny_menu_output_gone(barny_state_t *state, barny_output_t *out);
bool
barny_menu_is_open(barny_state_t *state);
bool
barny_menu_owns_surface(barny_state_t *state, struct wl_surface *surface);
void
barny_menu_pointer_enter(barny_state_t *state);
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
barny_config_exclusive_zone(const barny_config_t *config);
int
barny_config_write_module_layout(const char *path,
                                 const char *modules_left,
                                 const char *modules_center,
                                 const char *modules_right);

#endif
