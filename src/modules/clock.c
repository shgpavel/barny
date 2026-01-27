#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  time_str[64];
	char                  date_str[64];
	time_t                last_update;
	PangoFontDescription *font_desc;
} clock_data_t;

static int
clock_init(barny_module_t *self, barny_state_t *state)
{
	clock_data_t *data = self->data;
	data->state        = state;
	data->last_update  = 0;

	/* Create font description */
	data->font_desc    = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 12");

	return 0;
}

static void
clock_destroy(barny_module_t *self)
{
	clock_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
clock_update(barny_module_t *self)
{
	clock_data_t *data = self->data;
	time_t        now  = time(NULL);

	/* Update every second */
	if (now != data->last_update) {
		data->last_update = now;

		struct tm *tm     = localtime(&now);
		strftime(data->time_str, sizeof(data->time_str), "%H:%M:%S", tm);
		strftime(data->date_str, sizeof(data->date_str), "%a %b %d", tm);

		self->dirty = true;
	}
}

static void
clock_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	clock_data_t *data = self->data;
	(void)w;

	/* Create Pango layout for time */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->time_str, -1);

	/* Get text dimensions */
	int time_width, time_height;
	pango_layout_get_pixel_size(layout, &time_width, &time_height);

	/* Draw time text with shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - time_height) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, x, y + (h - time_height) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	/* Update module width based on rendered content */
	self->width = time_width + 8;
}

barny_module_t *
barny_module_clock_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	clock_data_t   *data = calloc(1, sizeof(clock_data_t));

	mod->name            = "clock";
	mod->position        = BARNY_POS_CENTER;
	mod->init            = clock_init;
	mod->destroy         = clock_destroy;
	mod->update          = clock_update;
	mod->render          = clock_render;
	mod->data            = data;
	mod->width           = 80;
	mod->dirty           = true;

	return mod;
}
