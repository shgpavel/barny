#include <stdio.h>
#include <stdlib.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  price_str[64];
	double                price;
	PangoFontDescription *font_desc;
} crypto_data_t;

static int
crypto_init(barny_module_t *self, barny_state_t *state)
{
	crypto_data_t *data = self->data;
	data->state         = state;

	data->font_desc     = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 11");

	snprintf(data->price_str, sizeof(data->price_str), "BTC --");
	data->price = 0;

	return 0;
}

static void
crypto_destroy(barny_module_t *self)
{
	crypto_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
crypto_update(barny_module_t *self)
{
	crypto_data_t *data = self->data;

	/* Try to read from btc_price module */
	FILE          *f    = fopen("/opt/barny/modules/btc_price", "r");
	if (f) {
		double price;
		if (fscanf(f, "%lf", &price) == 1) {
			if (price != data->price) {
				data->price = price;
				snprintf(data->price_str, sizeof(data->price_str),
				         "BTC $%.0f", price);
				self->dirty = true;
			}
		}
		fclose(f);
	}
}

static void
crypto_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	crypto_data_t *data = self->data;
	(void)w;

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->price_str, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	/* Shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	/* Text */
	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set)
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g, cfg->text_color_b, 0.9);
	else
		cairo_set_source_rgba(cr, 0.5, 1, 0.5, 0.9);
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

	mod->name            = "crypto";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = crypto_init;
	mod->destroy         = crypto_destroy;
	mod->update          = crypto_update;
	mod->render          = crypto_render;
	mod->data            = data;
	mod->width           = 120;
	mod->dirty           = true;

	return mod;
}
