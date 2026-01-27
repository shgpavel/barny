#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  freq_str[32];
	char                  power_str[32];
	double                frequency;
	double                power;
	PangoFontDescription *font_desc;
} sysinfo_data_t;

static int
sysinfo_init(barny_module_t *self, barny_state_t *state)
{
	sysinfo_data_t *data = self->data;
	data->state          = state;

	data->font_desc      = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	strcpy(data->freq_str, "-- GHz");
	strcpy(data->power_str, "-- W");

	return 0;
}

static void
sysinfo_destroy(barny_module_t *self)
{
	sysinfo_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
sysinfo_update(barny_module_t *self)
{
	sysinfo_data_t *data = self->data;

	/* Read CPU frequency */
	FILE           *f    = fopen("/opt/misc/frequency", "r");
	if (f) {
		double freq;
		if (fscanf(f, "%lf", &freq) == 1) {
			if (freq != data->frequency) {
				data->frequency = freq;
				snprintf(data->freq_str,
				         sizeof(data->freq_str),
				         "%.1f GHz",
				         freq / 1000.0);
				self->dirty = true;
			}
		}
		fclose(f);
	}

	/* Read power consumption */
	f = fopen("/opt/misc/power", "r");
	if (f) {
		double power;
		if (fscanf(f, "%lf", &power) == 1) {
			if (power != data->power) {
				data->power = power;
				snprintf(data->power_str,
				         sizeof(data->power_str),
				         "%.0f W",
				         power);
				self->dirty = true;
			}
		}
		fclose(f);
	}
}

static void
sysinfo_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	sysinfo_data_t *data = self->data;
	(void)w;

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);

	/* Render frequency */
	pango_layout_set_text(layout, data->freq_str, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	cairo_set_source_rgba(cr, 0.7, 0.9, 1, 0.9);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	int total_width  = tw;

	/* Add separator */
	total_width     += 8;

	/* Render power */
	pango_layout_set_text(layout, data->power_str, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + total_width + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	cairo_set_source_rgba(cr, 1, 0.9, 0.7, 0.9);
	cairo_move_to(cr, x + total_width, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	total_width += tw;

	g_object_unref(layout);

	self->width = total_width + 8;
}

barny_module_t *
barny_module_sysinfo_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	sysinfo_data_t *data = calloc(1, sizeof(sysinfo_data_t));

	mod->name            = "sysinfo";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = sysinfo_init;
	mod->destroy         = sysinfo_destroy;
	mod->update          = sysinfo_update;
	mod->render          = sysinfo_render;
	mod->data            = data;
	mod->width           = 140;
	mod->dirty           = true;

	return mod;
}
