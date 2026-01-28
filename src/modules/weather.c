#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  weather_str[128];
	double                temperature;
	PangoFontDescription *font_desc;
} weather_data_t;

static int
weather_init(barny_module_t *self, barny_state_t *state)
{
	weather_data_t *data = self->data;
	data->state          = state;

	data->font_desc      = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 11");

	/* Read initial weather data */
	FILE *f = fopen("/opt/barny/modules/weather", "r");
	if (f) {
		if (fgets(data->weather_str, sizeof(data->weather_str), f)) {
			/* Remove newline */
			char *nl = strchr(data->weather_str, '\n');
			if (nl)
				*nl = '\0';
		}
		fclose(f);
	} else {
		strcpy(data->weather_str, "--");
	}

	return 0;
}

static void
weather_destroy(barny_module_t *self)
{
	weather_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
weather_update(barny_module_t *self)
{
	weather_data_t *data = self->data;
	char            old_weather[128];
	strcpy(old_weather, data->weather_str);

	FILE *f = fopen("/opt/barny/modules/weather", "r");
	if (f) {
		if (fgets(data->weather_str, sizeof(data->weather_str), f)) {
			char *nl = strchr(data->weather_str, '\n');
			if (nl)
				*nl = '\0';
		}
		fclose(f);

		if (strcmp(old_weather, data->weather_str) != 0) {
			self->dirty = true;
		}
	}
}

static void
weather_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	weather_data_t *data = self->data;
	(void)w;

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->weather_str, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	/* Shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	/* Text */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_weather_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	weather_data_t *data = calloc(1, sizeof(weather_data_t));

	mod->name            = "weather";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = weather_init;
	mod->destroy         = weather_destroy;
	mod->update          = weather_update;
	mod->render          = weather_render;
	mod->data            = data;
	mod->width           = 100;
	mod->dirty           = true;

	return mod;
}
