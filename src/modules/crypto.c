#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "barny.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POPUP_WIDTH          180
#define POPUP_LINE_H          26
#define POPUP_PAD_Y           10
#define POPUP_PAD_X           14
#define POPUP_RADIUS          12
#define POPUP_BAR_OVERLAP      6
#define CRYPTO_NAME_LEN       24
#define CRYPTO_FILE_PATH_LEN 128
#define CRYPTO_PRICE_LEN      32

static const char *default_crypto_pairs[] = {
	"BTC-USDT-SWAP",
	"ETH-USDT-SWAP",
	"SOL-USDT-SWAP",
	"XRP-USDT-SWAP",
	"ADA-USDT-SWAP",
	"DOGE-USDT-SWAP",
	"DOT-USDT-SWAP",
};

typedef struct {
	char   name[CRYPTO_NAME_LEN];
	char   file_path[CRYPTO_FILE_PATH_LEN];
	char   price_str[CRYPTO_PRICE_LEN];
	double price;
} crypto_pair_t;

typedef struct {
	barny_state_t        *state;
	char                  price_str[64];
	PangoFontDescription *font_desc;
	PangoFontDescription *popup_font_desc;
	crypto_pair_t        *pairs;
	int                   pair_count;

	bool                          hover_active;
	bool                          popup_configured;
	struct wl_surface            *popup_surface;
	struct zwlr_layer_surface_v1 *popup_layer_surface;
	struct wl_buffer             *popup_buffer;
	cairo_surface_t              *popup_cairo_surface;
	cairo_t                      *popup_cr;
	void                         *popup_shm_data;
	int                           popup_shm_size;
	int                           popup_screen_x;
	int                           popup_screen_y;
} crypto_data_t;

static int
configured_pair_count(const barny_config_t *cfg)
{
	if (cfg->crypto_pair_count > 0 && cfg->crypto_pairs)
		return cfg->crypto_pair_count;
	return (int)(sizeof(default_crypto_pairs)
	             / sizeof(default_crypto_pairs[0]));
}

static const char *
configured_pair_at(const barny_config_t *cfg, int idx)
{
	if (cfg->crypto_pair_count > 0 && cfg->crypto_pairs)
		return cfg->crypto_pairs[idx];
	return default_crypto_pairs[idx];
}

static void
pair_name_from_market(const char *market, char *buf, size_t buf_size)
{
	size_t out = 0;

	if (!buf || buf_size == 0)
		return;

	buf[0] = '\0';
	if (!market || !*market)
		return;

	while (*market && *market != '-' && out + 1 < buf_size) {
		buf[out++] = (char)toupper((unsigned char)*market);
		market++;
	}
	buf[out] = '\0';
}

static void
pair_file_from_market(const char *market, char *buf, size_t buf_size)
{
	char   slug[64];
	size_t out = 0;

	if (!buf || buf_size == 0)
		return;

	buf[0] = '\0';

	while (*market && *market != '-' && out + 1 < sizeof(slug)) {
		if (isalnum((unsigned char)*market))
			slug[out++] = (char)tolower((unsigned char)*market);
		market++;
	}

	if (out == 0) {
		snprintf(buf, buf_size, "/opt/barny/modules/crypto_price");
		return;
	}

	slug[out] = '\0';
	snprintf(buf, buf_size, "/opt/barny/modules/%s_price", slug);
}

static void
format_pair_price(char *buf, size_t buf_size, double price)
{
	if (price >= 1000.0)
		snprintf(buf, buf_size, "$%.0f", price);
	else if (price >= 1.0)
		snprintf(buf, buf_size, "$%.2f", price);
	else
		snprintf(buf, buf_size, "$%.4f", price);
}

static bool
read_pair_price(const char *path, double *price_out)
{
	FILE *f;
	char  line[64];
	char *end = NULL;

	if (!path || !price_out)
		return false;

	f = fopen(path, "r");
	if (!f)
		return false;

	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return false;
	}
	fclose(f);

	*price_out = strtod(line, &end);
	return end != line;
}

static int
popup_row_count(const crypto_data_t *data)
{
	if (!data || data->pair_count <= 1)
		return 0;
	return data->pair_count - 1;
}

static int
popup_height(const crypto_data_t *data)
{
	return POPUP_PAD_Y * 2 + popup_row_count(data) * POPUP_LINE_H;
}

static void
rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static int
popup_create_shm(int size)
{
	static unsigned counter = 0;
	char            name[64];
	struct timespec ts;
	int             fd;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		ts.tv_sec  = 0;
		ts.tv_nsec = 0;
	}

	snprintf(name, sizeof(name), "/barny-popup-%d-%u-%lu",
	         (int)getpid(), (unsigned)counter++,
	         (unsigned long)ts.tv_nsec);

	fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
		return -1;

	if (ftruncate(fd, size) < 0) {
		shm_unlink(name);
		close(fd);
		return -1;
	}

	shm_unlink(name);
	return fd;
}

static void
popup_render(crypto_data_t *data)
{
	if (!data->popup_cr || popup_row_count(data) == 0)
		return;

	barny_state_t *state = data->state;
	cairo_t       *cr    = data->popup_cr;
	int            pw    = POPUP_WIDTH;
	int            ph    = popup_height(data);

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_save(cr);
	rounded_rect(cr, 0, 0, pw, ph, POPUP_RADIUS);
	cairo_clip(cr);

	cairo_surface_t *bg_surface = state->displaced_wallpaper
	                                      ? state->displaced_wallpaper
	                                      : state->blurred_wallpaper;

	if (bg_surface && state->pointer_output) {
		int out_w = state->pointer_output->width;
		int out_h = state->pointer_output->height;
		int wp_w  = cairo_image_surface_get_width(bg_surface);
		int wp_h  = cairo_image_surface_get_height(bg_surface);

		double scale_x = (double)wp_w / out_w;
		double scale_y = (double)wp_h / out_h;
		double scale   = scale_x < scale_y ? scale_x : scale_y;
		int    src_y_off
		        = state->config.position_top
		                  ? 0
		                  : (wp_h - (int)(out_h * scale));

		cairo_scale(cr, 1.0 / scale, 1.0 / scale);
		cairo_set_source_surface(
		        cr, bg_surface,
		        -data->popup_screen_x * scale,
		        -(data->popup_screen_y * scale + src_y_off));
		cairo_paint(cr);
	} else {
		cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, ph);
		cairo_pattern_add_color_stop_rgba(bg, 0, 0.15, 0.15, 0.18,
		                                  0.85);
		cairo_pattern_add_color_stop_rgba(bg, 1, 0.08, 0.08, 0.10,
		                                  0.85);
		cairo_set_source(cr, bg);
		cairo_paint(cr);
		cairo_pattern_destroy(bg);
	}

	cairo_restore(cr);

	rounded_rect(cr, 0.5, 0.5, pw - 1, ph - 1, POPUP_RADIUS);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	rounded_rect(cr, 1.5, 1.5, pw - 3, ph - 3, POPUP_RADIUS - 1);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	cairo_save(cr);
	rounded_rect(cr, 0, 0, pw, ph, POPUP_RADIUS);
	cairo_clip(cr);
	{
		cairo_pattern_t *hl
		        = cairo_pattern_create_linear(0, 0, pw * 0.7, ph * 0.7);
		cairo_pattern_add_color_stop_rgba(hl, 0.0, 1, 1, 1, 0.15);
		cairo_pattern_add_color_stop_rgba(hl, 0.3, 1, 1, 1, 0.04);
		cairo_pattern_add_color_stop_rgba(hl, 1.0, 1, 1, 1, 0);
		cairo_set_source(cr, hl);
		cairo_paint(cr);
		cairo_pattern_destroy(hl);
	}
	{
		cairo_pattern_t *sh
		        = cairo_pattern_create_linear(pw * 0.3, ph * 0.3, pw, ph);
		cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(sh, 0.7, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.15);
		cairo_set_source(cr, sh);
		cairo_paint(cr);
		cairo_pattern_destroy(sh);
	}
	{
		cairo_pattern_t *top_r = cairo_pattern_create_linear(0, 0, 0, 8);
		cairo_pattern_add_color_stop_rgba(top_r, 0.0, 1, 1, 1, 0.08);
		cairo_pattern_add_color_stop_rgba(top_r, 1.0, 1, 1, 1, 0);
		cairo_set_source(cr, top_r);
		cairo_rectangle(cr, 0, 0, pw, 8);
		cairo_fill(cr);
		cairo_pattern_destroy(top_r);

		cairo_pattern_t *left_r = cairo_pattern_create_linear(0, 0, 8, 0);
		cairo_pattern_add_color_stop_rgba(left_r, 0.0, 1, 1, 1, 0.06);
		cairo_pattern_add_color_stop_rgba(left_r, 1.0, 1, 1, 1, 0);
		cairo_set_source(cr, left_r);
		cairo_rectangle(cr, 0, 0, 8, ph);
		cairo_fill(cr);
		cairo_pattern_destroy(left_r);
	}
	cairo_restore(cr);

	PangoLayout    *layout = pango_cairo_create_layout(cr);
	barny_config_t *cfg    = &state->config;

	pango_layout_set_font_description(layout, data->popup_font_desc);

	for (int i = 1; i < data->pair_count; i++) {
		int line_y = POPUP_PAD_Y + (i - 1) * POPUP_LINE_H;
		int tw, th;
		int text_y;
		int price_x;

		pango_layout_set_text(layout, data->pairs[i].name, -1);
		pango_layout_get_pixel_size(layout, &tw, &th);
		text_y = line_y + (POPUP_LINE_H - th) / 2;

		cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
		cairo_move_to(cr, POPUP_PAD_X + 1, text_y + 1);
		pango_cairo_show_layout(cr, layout);

		cairo_set_source_rgba(cr, 0.6, 0.7, 0.65, 0.9);
		cairo_move_to(cr, POPUP_PAD_X, text_y);
		pango_cairo_show_layout(cr, layout);

		pango_layout_set_text(layout, data->pairs[i].price_str, -1);
		pango_layout_get_pixel_size(layout, &tw, &th);
		price_x = pw - POPUP_PAD_X - tw;

		cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
		cairo_move_to(cr, price_x + 1, text_y + 1);
		pango_cairo_show_layout(cr, layout);

		if (cfg->text_color_set) {
			cairo_set_source_rgba(cr, cfg->text_color_r,
			                      cfg->text_color_g,
			                      cfg->text_color_b, 0.9);
		} else {
			cairo_set_source_rgba(cr, 0.5, 1, 0.5, 0.9);
		}
		cairo_move_to(cr, price_x, text_y);
		pango_cairo_show_layout(cr, layout);
	}

	g_object_unref(layout);

	cairo_surface_flush(data->popup_cairo_surface);
	wl_surface_attach(data->popup_surface, data->popup_buffer, 0, 0);
	wl_surface_damage_buffer(data->popup_surface, 0, 0, pw, ph);
	wl_surface_commit(data->popup_surface);
}

static void
popup_layer_configure(void *userdata, struct zwlr_layer_surface_v1 *surface,
                      uint32_t serial, uint32_t width, uint32_t height)
{
	crypto_data_t *data = userdata;
	int            pw;
	int            ph;
	int            stride;
	int            size;
	int            fd;
	struct wl_shm_pool *pool;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	/* Compositor may send configure multiple times; tear down only the
	 * buffer chain (not the layer surface) before re-allocating. */
	if (data->popup_buffer) {
		if (data->popup_cr) {
			cairo_destroy(data->popup_cr);
			data->popup_cr = NULL;
		}
		if (data->popup_cairo_surface) {
			cairo_surface_destroy(data->popup_cairo_surface);
			data->popup_cairo_surface = NULL;
		}
		wl_buffer_destroy(data->popup_buffer);
		data->popup_buffer = NULL;
		if (data->popup_shm_data) {
			munmap(data->popup_shm_data,
			       (size_t)data->popup_shm_size);
			data->popup_shm_data = NULL;
		}
		data->popup_shm_size = 0;
	}

	pw     = (int)width > 0 ? (int)width : POPUP_WIDTH;
	ph     = (int)height > 0 ? (int)height : popup_height(data);
	stride = pw * 4;
	size   = stride * ph;

	fd = popup_create_shm(size);
	if (fd < 0)
		return;

	data->popup_shm_data
	        = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED,
	               fd, 0);
	if (data->popup_shm_data == MAP_FAILED) {
		close(fd);
		data->popup_shm_data = NULL;
		return;
	}
	data->popup_shm_size = size;

	pool = wl_shm_create_pool(data->state->shm, fd, size);
	data->popup_buffer = wl_shm_pool_create_buffer(
	        pool, 0, pw, ph, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	data->popup_cairo_surface = cairo_image_surface_create_for_data(
	        data->popup_shm_data, CAIRO_FORMAT_ARGB32, pw, ph, stride);
	data->popup_cr            = cairo_create(data->popup_cairo_surface);
	data->popup_configured    = true;

	popup_render(data);
}

static void
popup_layer_closed(void *userdata, struct zwlr_layer_surface_v1 *surface)
{
	(void)surface;
	((crypto_data_t *)userdata)->hover_active = false;
}

static const struct zwlr_layer_surface_v1_listener popup_layer_listener = {
	.configure = popup_layer_configure,
	.closed    = popup_layer_closed,
};

static void
popup_destroy(crypto_data_t *data)
{
	if (data->popup_cr) {
		cairo_destroy(data->popup_cr);
		data->popup_cr = NULL;
	}
	if (data->popup_cairo_surface) {
		cairo_surface_destroy(data->popup_cairo_surface);
		data->popup_cairo_surface = NULL;
	}
	if (data->popup_buffer) {
		wl_buffer_destroy(data->popup_buffer);
		data->popup_buffer = NULL;
	}
	if (data->popup_shm_data) {
		munmap(data->popup_shm_data, (size_t)data->popup_shm_size);
		data->popup_shm_data = NULL;
	}
	if (data->popup_layer_surface) {
		zwlr_layer_surface_v1_destroy(data->popup_layer_surface);
		data->popup_layer_surface = NULL;
	}
	if (data->popup_surface) {
		wl_surface_destroy(data->popup_surface);
		data->popup_surface = NULL;
	}
	data->popup_configured = false;
}

static void
popup_show(barny_module_t *self)
{
	crypto_data_t *data  = self->data;
	barny_state_t *state = data->state;
	barny_output_t *out;
	uint32_t        anchor;
	int             popup_h;
	int             left_margin;
	int             center_off;
	int             top_margin    = 0;
	int             bottom_margin = 0;

	if (data->popup_surface || popup_row_count(data) == 0)
		return;

	out = state->pointer_output;
	if (!out)
		return;

	popup_h = popup_height(data);

	data->popup_surface = wl_compositor_create_surface(state->compositor);
	if (!data->popup_surface)
		return;

	struct wl_region *empty
	        = wl_compositor_create_region(state->compositor);
	wl_surface_set_input_region(data->popup_surface, empty);
	wl_region_destroy(empty);

	data->popup_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
	        state->layer_shell, data->popup_surface, out->wl_output,
	        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "barny-popup");
	if (!data->popup_layer_surface) {
		wl_surface_destroy(data->popup_surface);
		data->popup_surface = NULL;
		return;
	}

	zwlr_layer_surface_v1_add_listener(data->popup_layer_surface,
	                                   &popup_layer_listener, data);

	anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	if (state->config.position_top)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	else
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

	zwlr_layer_surface_v1_set_anchor(data->popup_layer_surface, anchor);
	zwlr_layer_surface_v1_set_size(data->popup_layer_surface, POPUP_WIDTH,
	                               popup_h);
	zwlr_layer_surface_v1_set_exclusive_zone(data->popup_layer_surface, 0);

	left_margin = state->config.margin_left + self->render_x;
	center_off  = (POPUP_WIDTH - self->width) / 2;
	left_margin -= center_off;
	if (left_margin < 0)
		left_margin = 0;

	if (state->config.position_top) {
		top_margin = state->config.height + state->config.margin_top
		             - POPUP_BAR_OVERLAP;
		if (top_margin < 0)
			top_margin = 0;
	} else {
		bottom_margin = state->config.height
		                + state->config.margin_bottom
		                - POPUP_BAR_OVERLAP;
		if (bottom_margin < 0)
			bottom_margin = 0;
	}

	zwlr_layer_surface_v1_set_margin(data->popup_layer_surface,
	                                 top_margin, 0, bottom_margin,
	                                 left_margin);

	data->popup_screen_x = left_margin;
	if (state->config.position_top)
		data->popup_screen_y = top_margin;
	else
		data->popup_screen_y = out->height - bottom_margin - popup_h;

	wl_surface_commit(data->popup_surface);
}

static void
crypto_on_hover(barny_module_t *self, bool hovering, int x, int y)
{
	crypto_data_t *data = self->data;
	(void)x;
	(void)y;

	if (hovering) {
		data->hover_active = true;
		if (!data->popup_surface)
			popup_show(self);
	} else {
		/* Defer hide: cursor crossing the bar/popup boundary briefly
		 * fires pointer_leave; let crypto_update reap stale popups. */
		data->hover_active = false;
	}
}

static int
crypto_init(barny_module_t *self, barny_state_t *state)
{
	crypto_data_t        *data = self->data;
	const barny_config_t *cfg  = &state->config;

	data->state = state;
	data->font_desc = pango_font_description_from_string(
	        cfg->font ? cfg->font : "Sans 11");
	data->popup_font_desc = pango_font_description_from_string(
	        cfg->font ? cfg->font : "Sans 11");

	int base_size = pango_font_description_get_size(data->popup_font_desc);
	if (base_size > 0) {
		pango_font_description_set_size(data->popup_font_desc,
		                                base_size * 85 / 100);
	} else {
		pango_font_description_set_size(data->popup_font_desc,
		                                9 * PANGO_SCALE);
	}

	data->pair_count = configured_pair_count(cfg);
	if (data->pair_count > 64)
		data->pair_count = 64;
	data->pairs      = calloc((size_t)data->pair_count,
	                          sizeof(*data->pairs));
	if (!data->pairs)
		return -1;

	for (int i = 0; i < data->pair_count; i++) {
		const char *market = configured_pair_at(cfg, i);

		pair_name_from_market(market, data->pairs[i].name,
		                      sizeof(data->pairs[i].name));
		pair_file_from_market(market, data->pairs[i].file_path,
		                      sizeof(data->pairs[i].file_path));
		snprintf(data->pairs[i].price_str,
		         sizeof(data->pairs[i].price_str), "--");
	}

	snprintf(data->price_str, sizeof(data->price_str), "%s --",
	         data->pairs[0].name);

	return 0;
}

static void
crypto_destroy(barny_module_t *self)
{
	crypto_data_t *data = self->data;

	if (!data)
		return;

	if (data->state && data->state->hover_module == self)
		data->state->hover_module = NULL;

	popup_destroy(data);

	if (data->font_desc)
		pango_font_description_free(data->font_desc);
	if (data->popup_font_desc)
		pango_font_description_free(data->popup_font_desc);

	free(data->pairs);
	free(data);
	self->data = NULL;
}

static void
crypto_update(barny_module_t *self)
{
	crypto_data_t *data          = self->data;
	bool           popup_changed = false;

	for (int i = 0; i < data->pair_count; i++) {
		double price;

		if (!read_pair_price(data->pairs[i].file_path, &price))
			continue;

		if (price != data->pairs[i].price) {
			char formatted[CRYPTO_PRICE_LEN];

			data->pairs[i].price = price;
			format_pair_price(formatted, sizeof(formatted), price);
			snprintf(data->pairs[i].price_str,
			         sizeof(data->pairs[i].price_str), "%s",
			         formatted);

			if (i == 0) {
				snprintf(data->price_str, sizeof(data->price_str),
				         "%s %s", data->pairs[i].name, formatted);
				self->dirty = true;
			} else {
				popup_changed = true;
			}
		}
	}

	if (popup_changed && data->popup_configured)
		popup_render(data);

	/* Grace window: hide popup only if hover hasn't returned by next
	 * tick, avoiding flicker on bar->popup cursor crossings. */
	if (!data->hover_active && data->popup_configured)
		popup_destroy(data);
}

static void
crypto_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	crypto_data_t *data = self->data;
	PangoLayout   *layout;
	barny_config_t *cfg;
	int            tw;
	int            th;

	(void)w;

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->price_str, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	cfg = &data->state->config;
	if (cfg->text_color_set) {
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
		                      cfg->text_color_b, 0.9);
	} else {
		cairo_set_source_rgba(cr, 0.5, 1, 0.5, 0.9);
	}
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_crypto_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	crypto_data_t  *data = calloc(1, sizeof(crypto_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name     = "crypto";
	mod->position = BARNY_POS_RIGHT;
	mod->init     = crypto_init;
	mod->destroy  = crypto_destroy;
	mod->update   = crypto_update;
	mod->render   = crypto_render;
	mod->on_hover = crypto_on_hover;
	mod->data     = data;
	mod->width    = 120;
	mod->dirty    = true;

	return mod;
}
