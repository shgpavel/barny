#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[128];
	time_t                last_update;
	PangoFontDescription *font_desc;
} clock_data_t;

static void
build_time_string(char *buf, size_t buflen, struct tm *tm, barny_config_t *cfg)
{
	buf[0] = '\0';

	if (!cfg->clock_show_time)
		return;

	if (cfg->clock_24h_format) {
		if (cfg->clock_show_seconds) {
			snprintf(buf, buflen, "%02d:%02d:%02d",
			         tm->tm_hour, tm->tm_min, tm->tm_sec);
		} else {
			snprintf(buf, buflen, "%02d:%02d",
			         tm->tm_hour, tm->tm_min);
		}
	} else {
		int hour = tm->tm_hour % 12;
		if (hour == 0) hour = 12;
		const char *ampm = tm->tm_hour >= 12 ? "PM" : "AM";

		if (cfg->clock_show_seconds) {
			snprintf(buf, buflen, "%d:%02d:%02d %s",
			         hour, tm->tm_min, tm->tm_sec, ampm);
		} else {
			snprintf(buf, buflen, "%d:%02d %s",
			         hour, tm->tm_min, ampm);
		}
	}
}

static void
build_date_string(char *buf, size_t buflen, struct tm *tm, barny_config_t *cfg)
{
	buf[0] = '\0';

	if (!cfg->clock_show_date)
		return;

	char weekday[16] = "";
	char day[16]     = "";
	char month[16]   = "";
	char year[16]    = "";
	char sep[2]      = { cfg->clock_date_separator, '\0' };

	if (cfg->clock_show_weekday) {
		strftime(weekday, sizeof(weekday), "%a ", tm);
	}

	if (cfg->clock_show_day) {
		snprintf(day, sizeof(day), "%02d", tm->tm_mday);
	}

	if (cfg->clock_show_month) {
		snprintf(month, sizeof(month), "%02d", tm->tm_mon + 1);
	}

	if (cfg->clock_show_year) {
		snprintf(year, sizeof(year), "%d", tm->tm_year + 1900);
	}

	/* Build date based on order preference */
	char date_part[64] = "";
	size_t dp_off = 0;
	const char *parts[3] = { NULL, NULL, NULL };

	switch (cfg->clock_date_order) {
	case 0:  /* dd/mm/yyyy */
		if (cfg->clock_show_day)   parts[0] = day;
		if (cfg->clock_show_month) parts[1] = month;
		if (cfg->clock_show_year)  parts[2] = year;
		break;
	case 1:  /* mm/dd/yyyy */
		if (cfg->clock_show_month) parts[0] = month;
		if (cfg->clock_show_day)   parts[1] = day;
		if (cfg->clock_show_year)  parts[2] = year;
		break;
	case 2:  /* yyyy/mm/dd */
		if (cfg->clock_show_year)  parts[0] = year;
		if (cfg->clock_show_month) parts[1] = month;
		if (cfg->clock_show_day)   parts[2] = day;
		break;
	default:
		break;
	}

	for (int i = 0; i < 3; i++) {
		if (!parts[i] || !parts[i][0])
			continue;
		if (dp_off > 0) {
			dp_off += snprintf(date_part + dp_off,
			                   sizeof(date_part) - dp_off, "%s", sep);
		}
		dp_off += snprintf(date_part + dp_off,
		                   sizeof(date_part) - dp_off, "%s", parts[i]);
	}

	snprintf(buf, buflen, "%s%s", weekday, date_part);
}

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
	if (!data)
		return;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
clock_update(barny_module_t *self)
{
	clock_data_t    *data = self->data;
	barny_config_t  *cfg  = &data->state->config;
	time_t           now  = time(NULL);

	/* Update every second */
	if (now != data->last_update) {
		data->last_update = now;

		struct tm *tm = localtime(&now);

		char time_str[64];
		char date_str[64];

		build_time_string(time_str, sizeof(time_str), tm, cfg);
		build_date_string(date_str, sizeof(date_str), tm, cfg);

		/* Combine time and date */
		if (time_str[0] && date_str[0]) {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s  %s", time_str, date_str);
		} else if (time_str[0]) {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s", time_str);
		} else if (date_str[0]) {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s", date_str);
		} else {
			data->display_str[0] = '\0';
		}

		self->dirty = true;
	}
}

static void
clock_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	clock_data_t *data = self->data;
	(void)w;

	if (data->display_str[0] == '\0') {
		self->width = 0;
		return;
	}

	/* Create Pango layout */
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->display_str, -1);

	/* Get text dimensions */
	int text_width, text_height;
	pango_layout_get_pixel_size(layout, &text_width, &text_height);

	/* Draw text with shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - text_height) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set)
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g, cfg->text_color_b, 1);
	else
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, x, y + (h - text_height) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	/* Update module width based on rendered content */
	self->width = text_width + 8;
}

barny_module_t *
barny_module_clock_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	clock_data_t   *data = calloc(1, sizeof(clock_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name            = "clock";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = clock_init;
	mod->destroy         = clock_destroy;
	mod->update          = clock_update;
	mod->render          = clock_render;
	mod->data            = data;
	mod->width           = 80;
	mod->dirty           = true;

	return mod;
}
