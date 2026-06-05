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
	int         hour;
	const char *ampm;

	buf[0] = '\0';

	if (!cfg->clock_show_time)
		return;

	if (cfg->clock_24h_format) {
		if (cfg->clock_show_seconds) {
			snprintf(buf, buflen, "%02d:%02d:%02d", tm->tm_hour,
			         tm->tm_min, tm->tm_sec);
		} else {
			snprintf(buf, buflen, "%02d:%02d", tm->tm_hour,
			         tm->tm_min);
		}
	} else {
		hour = tm->tm_hour % 12;
		if (hour == 0)
			hour = 12;
		ampm = tm->tm_hour >= 12 ? "PM" : "AM";

		if (cfg->clock_show_seconds) {
			snprintf(buf, buflen, "%d:%02d:%02d %s", hour, tm->tm_min,
			         tm->tm_sec, ampm);
		} else {
			snprintf(buf, buflen, "%d:%02d %s", hour, tm->tm_min,
			         ampm);
		}
	}
}

static void
build_date_string(char *buf, size_t buflen, struct tm *tm, barny_config_t *cfg)
{
	char        weekday[16];
	char        day[16];
	char        month[16];
	char        year[16];
	char        sep[2];
	char        date_part[64];
	size_t      dp_off;
	const char *parts[3];
	int         i;

	buf[0] = '\0';

	if (!cfg->clock_show_date)
		return;

	weekday[0] = '\0';
	day[0]     = '\0';
	month[0]   = '\0';
	year[0]    = '\0';
	sep[0]     = cfg->clock_date_separator;
	sep[1]     = '\0';

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

	date_part[0] = '\0';
	dp_off       = 0;
	parts[0]     = NULL;
	parts[1]     = NULL;
	parts[2]     = NULL;

	switch (cfg->clock_date_order) {
	case 0:
		if (cfg->clock_show_day)
			parts[0] = day;
		if (cfg->clock_show_month)
			parts[1] = month;
		if (cfg->clock_show_year)
			parts[2] = year;
		break;
	case 1:
		if (cfg->clock_show_month)
			parts[0] = month;
		if (cfg->clock_show_day)
			parts[1] = day;
		if (cfg->clock_show_year)
			parts[2] = year;
		break;
	case 2:
		if (cfg->clock_show_year)
			parts[0] = year;
		if (cfg->clock_show_month)
			parts[1] = month;
		if (cfg->clock_show_day)
			parts[2] = day;
		break;
	default:
		break;
	}

	for (i = 0; i < 3; i++) {
		if (!parts[i] || !parts[i][0])
			continue;
		if (dp_off > 0) {
			dp_off += snprintf(date_part + dp_off,
			                   sizeof(date_part) - dp_off, "%s", sep);
		}
		dp_off += snprintf(date_part + dp_off, sizeof(date_part) - dp_off,
		                   "%s", parts[i]);
	}

	snprintf(buf, buflen, "%s%s", weekday, date_part);
}

static int
clock_init(barny_module_t *self, barny_state_t *state)
{
	clock_data_t *data = self->data;
	data->state        = state;
	data->last_update  = 0;

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
	clock_data_t   *data;
	barny_config_t *cfg;
	time_t          now;
	struct tm      *tm;
	char            time_str[64];
	char            date_str[64];

	data = self->data;
	cfg  = &data->state->config;
	now  = time(NULL);

	if (now != data->last_update) {
		data->last_update = now;

		tm                = localtime(&now);

		build_time_string(time_str, sizeof(time_str), tm, cfg);
		build_date_string(date_str, sizeof(date_str), tm, cfg);

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
	clock_data_t *data;
	int           tw;

	data = self->data;
	(void)w;

	if (data->display_str[0] == '\0') {
		self->width = 0;
		return;
	}

	tw          = barny_module_render_text(cr, data->font_desc, data->display_str,
	                                       x, y, h, &data->state->config,
	                                       1, 1, 1, 1.0);
	self->width = tw + 8;
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

	mod->name               = "clock";
	mod->position           = BARNY_POS_RIGHT;
	mod->init               = clock_init;
	mod->destroy            = clock_destroy;
	mod->update             = clock_update;
	mod->update_interval_ms = 0;
	mod->render             = clock_render;
	mod->data               = data;
	mod->width              = 80;
	mod->dirty              = true;

	return mod;
}
