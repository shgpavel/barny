#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "popup.h"

#define POPUP_LINE_H          26
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
	barny_module_t       *self;
	char                  price_str[64];
	PangoFontDescription *font_desc;
	PangoFontDescription *popup_font_desc;
	crypto_pair_t        *pairs;
	int                   pair_count;

	barny_popup_t        *popup;
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
format_pair_price(const barny_config_t *cfg, char *buf, size_t buf_size,
                  double price)
{
	const char *sym = (cfg && cfg->crypto_currency_symbol)
	                          ? cfg->crypto_currency_symbol
	                          : "$";
	bool        suffix    = cfg && cfg->crypto_symbol_suffix;
	int         decimals  = cfg ? cfg->crypto_decimals : 0;
	int         auto_dec  = decimals;

	if (cfg && cfg->crypto_decimals == 0) {
		if (price < 1.0)
			auto_dec = 4;
		else if (price < 1000.0)
			auto_dec = 2;
		else
			auto_dec = 0;
	}

	if (suffix)
		snprintf(buf, buf_size, "%.*f%s", auto_dec, price, sym);
	else
		snprintf(buf, buf_size, "%s%.*f", sym, auto_dec, price);
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
crypto_popup_height(void *ud)
{
	const crypto_data_t *data = ud;
	return popup_row_count(data) * POPUP_LINE_H;
}

static int
crypto_popup_width(void *ud)
{
	crypto_data_t *data = ud;
	int            max_label = 0;
	int            max_value = 0;

	for (int i = 1; i < data->pair_count; i++) {
		int lw = barny_popup_measure_text(data->popup_font_desc,
		                                  data->pairs[i].name);
		int vw = barny_popup_measure_text(data->popup_font_desc,
		                                  data->pairs[i].price_str);
		if (lw > max_label)
			max_label = lw;
		if (vw > max_value)
			max_value = vw;
	}
	int content = max_label + 24 + max_value;
	int total   = content + 2 * BARNY_POPUP_PAD_X;
	if (total < 180)
		total = 180;
	return total;
}

static void
crypto_popup_render(void *ud, cairo_t *cr, int w, int h)
{
	crypto_data_t  *data = ud;
	barny_config_t *cfg  = &data->state->config;
	PangoLayout    *layout;

	(void)h;

	if (popup_row_count(data) == 0)
		return;

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->popup_font_desc);

	for (int i = 1; i < data->pair_count; i++) {
		int line_y = (i - 1) * POPUP_LINE_H;
		int tw, th;
		int text_y;
		int price_x;

		pango_layout_set_text(layout, data->pairs[i].name, -1);
		pango_layout_get_pixel_size(layout, &tw, &th);
		text_y = line_y + (POPUP_LINE_H - th) / 2;

		cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
		cairo_move_to(cr, 1, text_y + 1);
		pango_cairo_show_layout(cr, layout);

		cairo_set_source_rgba(cr, 0.6, 0.7, 0.65, 0.9);
		cairo_move_to(cr, 0, text_y);
		pango_cairo_show_layout(cr, layout);

		pango_layout_set_text(layout, data->pairs[i].price_str, -1);
		pango_layout_get_pixel_size(layout, &tw, &th);
		price_x = w - tw;

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
}

static void
crypto_on_hover(barny_module_t *self, bool hovering, int x, int y)
{
	crypto_data_t *data = self->data;
	(void)x;
	(void)y;

	if (hovering) {
		if (!data->popup && popup_row_count(data) > 0) {
			barny_popup_callbacks_t cb = {
				.content_height = crypto_popup_height,
				.content_width  = crypto_popup_width,
				.render         = crypto_popup_render,
				.userdata       = data,
			};
			data->popup = barny_popup_create(
			        data->state, self, &cb,
			        data->state->config.crypto_popup_gap);
		}
	} else {
		if (data->popup) {
			barny_popup_destroy(data->popup);
			data->popup = NULL;
		}
	}
}

static int
crypto_init(barny_module_t *self, barny_state_t *state)
{
	crypto_data_t        *data = self->data;
	const barny_config_t *cfg  = &state->config;

	data->state = state;
	data->self  = self;
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

	barny_popup_destroy(data->popup);
	data->popup = NULL;

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
			format_pair_price(&data->state->config, formatted,
			                  sizeof(formatted), price);
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

	if (popup_changed && barny_popup_visible(data->popup))
		barny_popup_redraw(data->popup);
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
	mod->update_interval_ms = 1000; /* prices file refreshed by helper at most every ~1s */
	mod->render   = crypto_render;
	mod->on_hover = crypto_on_hover;
	mod->data     = data;
	mod->width    = 120;
	mod->dirty    = true;

	return mod;
}
