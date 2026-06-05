#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "popup.h"
#include "util.h"

#define POPUP_LINE_H 26
#define WEATHER_FILE "/opt/barny/modules/weather"

typedef struct {
	barny_state_t        *state;
	barny_module_t       *self;

	char                  weather_str[128];

	double                temperature;
	double                feels_like;
	double                wind_speed;
	int                   humidity;
	int                   pressure;
	int                   wind_deg;
	char                  condition[64];
	char                  description[128];
	char                  location[64];
	char                  wind_dir[8];

	bool                  have_temp;
	bool                  have_feels_like;
	bool                  have_humidity;
	bool                  have_wind;
	bool                  have_pressure;
	bool                  have_location;
	bool                  have_description;

	PangoFontDescription *font_desc;
	PangoFontDescription *popup_font_desc;

	barny_popup_t        *popup;
} weather_data_t;

static inline char *
trim(char *s)
{
	return s ? barny_trim(s) : NULL;
}

static void
weather_reset(weather_data_t *d)
{
	d->have_temp        = false;
	d->have_feels_like  = false;
	d->have_humidity    = false;
	d->have_wind        = false;
	d->have_pressure    = false;
	d->have_location    = false;
	d->have_description = false;
	d->condition[0]     = '\0';
	d->description[0]   = '\0';
	d->location[0]      = '\0';
	d->wind_dir[0]      = '\0';
}

static bool
weather_parse_file(weather_data_t *d, char *bar_str, size_t bar_str_size)
{
	FILE *f;
	char  line[256];
	bool  legacy_first_line;

	f = fopen(WEATHER_FILE, "r");
	if (!f)
		return false;

	weather_reset(d);

	legacy_first_line = true;

	while (fgets(line, sizeof(line), f)) {
		char *p = trim(line);
		char *eq;
		char *key;
		char *value;

		if (!*p)
			continue;

		eq = strchr(p, '=');
		if (!eq) {
			if (legacy_first_line) {
				double temp = 0.0;
				char   tail[128];
				tail[0] = '\0';
				if (sscanf(p, "%lf %127[^\n]", &temp, tail)
				    >= 1) {
					d->temperature = temp;
					d->have_temp   = true;
					snprintf(d->condition,
					         sizeof(d->condition), "%s",
					         tail);
				}
			}
			legacy_first_line = false;
			continue;
		}
		legacy_first_line = false;

		*eq               = '\0';
		key               = trim(p);
		value             = trim(eq + 1);

		if (strcmp(key, "temp") == 0) {
			d->temperature = strtod(value, NULL);
			d->have_temp   = true;
		} else if (strcmp(key, "condition") == 0) {
			snprintf(d->condition, sizeof(d->condition), "%s",
			         value);
		} else if (strcmp(key, "description") == 0) {
			snprintf(d->description, sizeof(d->description), "%s",
			         value);
			d->have_description = true;
		} else if (strcmp(key, "location") == 0) {
			snprintf(d->location, sizeof(d->location), "%s", value);
			d->have_location = true;
		} else if (strcmp(key, "feels_like") == 0) {
			d->feels_like      = strtod(value, NULL);
			d->have_feels_like = true;
		} else if (strcmp(key, "humidity") == 0) {
			d->humidity      = (int)strtol(value, NULL, 10);
			d->have_humidity = true;
		} else if (strcmp(key, "wind_speed") == 0) {
			d->wind_speed = strtod(value, NULL);
			d->have_wind  = true;
		} else if (strcmp(key, "wind_deg") == 0) {
			d->wind_deg = (int)strtol(value, NULL, 10);
		} else if (strcmp(key, "wind_dir") == 0) {
			snprintf(d->wind_dir, sizeof(d->wind_dir), "%s", value);
		} else if (strcmp(key, "pressure") == 0) {
			d->pressure      = (int)strtol(value, NULL, 10);
			d->have_pressure = true;
		}
	}
	fclose(f);

	if (d->have_temp) {
		if (d->condition[0])
			snprintf(bar_str, bar_str_size,
			         "%.0f\xc2\xb0"
			         "C %s",
			         d->temperature, d->condition);
		else
			snprintf(bar_str, bar_str_size, "%.0f\xc2\xb0"
			                                "C",
			         d->temperature);
	} else {
		snprintf(bar_str, bar_str_size, "--");
	}
	return true;
}

static int
popup_active_rows(const weather_data_t *d)
{
	const barny_config_t *cfg  = &d->state->config;
	int                   rows = 0;

	if (d->have_location)
		rows++;
	if (d->have_description || d->condition[0])
		rows++;
	if (d->have_temp)
		rows++;
	if (cfg->weather_popup_show_feels_like && d->have_feels_like)
		rows++;
	if (cfg->weather_popup_show_humidity && d->have_humidity)
		rows++;
	if (cfg->weather_popup_show_wind && d->have_wind)
		rows++;
	if (cfg->weather_popup_show_pressure && d->have_pressure)
		rows++;
	return rows;
}

static int
weather_popup_height(void *ud)
{
	const weather_data_t *d = ud;
	return popup_active_rows(d) * POPUP_LINE_H;
}

static int
weather_popup_width(void *ud)
{
	weather_data_t       *d         = ud;
	const barny_config_t *cfg       = &d->state->config;
	int                   max_label = 0;
	int                   max_value = 0;
	char                  buf[64];
	int                   total;

#define MEASURE_LABEL(s)                                                    \
	do {                                                                \
		int _w = barny_popup_measure_text(d->popup_font_desc, (s)); \
		if (_w > max_label)                                         \
			max_label = _w;                                     \
	} while (0)
#define MEASURE_VALUE(s)                                                    \
	do {                                                                \
		int _w = barny_popup_measure_text(d->popup_font_desc, (s)); \
		if (_w > max_value)                                         \
			max_value = _w;                                     \
	} while (0)

	if (d->have_location) {
		MEASURE_LABEL("Location");
		MEASURE_VALUE(d->location);
	}
	if (d->have_description) {
		MEASURE_LABEL("Condition");
		MEASURE_VALUE(d->description);
	} else if (d->condition[0]) {
		MEASURE_LABEL("Condition");
		MEASURE_VALUE(d->condition);
	}
	if (d->have_temp) {
		MEASURE_LABEL("Temperature");
		snprintf(buf, sizeof(buf), "%.1f\xc2\xb0"
		                           "C",
		         d->temperature);
		MEASURE_VALUE(buf);
	}
	if (cfg->weather_popup_show_feels_like && d->have_feels_like) {
		MEASURE_LABEL("Feels like");
		snprintf(buf, sizeof(buf), "%.1f\xc2\xb0"
		                           "C",
		         d->feels_like);
		MEASURE_VALUE(buf);
	}
	if (cfg->weather_popup_show_humidity && d->have_humidity) {
		MEASURE_LABEL("Humidity");
		snprintf(buf, sizeof(buf), "%d%%", d->humidity);
		MEASURE_VALUE(buf);
	}
	if (cfg->weather_popup_show_wind && d->have_wind) {
		MEASURE_LABEL("Wind");
		if (d->wind_dir[0])
			snprintf(buf, sizeof(buf), "%.1f m/s %s", d->wind_speed,
			         d->wind_dir);
		else
			snprintf(buf, sizeof(buf), "%.1f m/s", d->wind_speed);
		MEASURE_VALUE(buf);
	}
	if (cfg->weather_popup_show_pressure && d->have_pressure) {
		MEASURE_LABEL("Pressure");
		snprintf(buf, sizeof(buf), "%d hPa", d->pressure);
		MEASURE_VALUE(buf);
	}
#undef MEASURE_LABEL
#undef MEASURE_VALUE

	total = max_label + 24 + max_value + 2 * BARNY_POPUP_PAD_X;
	if (total < 200)
		total = 200;

	return total;
}

static void
draw_row(cairo_t *cr, PangoLayout *layout, const barny_config_t *cfg,
         int line_idx, int width, const char *label, const char *value)
{
	double vr = 1.0, vg = 1.0, vb = 1.0;
	if (cfg->text_color_set) {
		vr = cfg->text_color_r;
		vg = cfg->text_color_g;
		vb = cfg->text_color_b;
	}
	barny_popup_draw_row(cr, layout, line_idx * POPUP_LINE_H, POPUP_LINE_H,
	                     width, label, value, 0.7, 0.75, 0.85, vr, vg, vb,
	                     0.95);
}

static void
weather_popup_render(void *ud, cairo_t *cr, int w, int h)
{
	weather_data_t       *d   = ud;
	const barny_config_t *cfg = &d->state->config;
	PangoLayout          *layout;
	int                   row = 0;
	char                  buf[64];

	(void)h;

	if (popup_active_rows(d) == 0)
		return;

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, d->popup_font_desc);

	if (d->have_location)
		draw_row(cr, layout, cfg, row++, w, "Location", d->location);

	if (d->have_description) {
		draw_row(cr, layout, cfg, row++, w, "Condition",
		         d->description);
	} else if (d->condition[0]) {
		draw_row(cr, layout, cfg, row++, w, "Condition", d->condition);
	}

	if (d->have_temp) {
		snprintf(buf, sizeof(buf), "%.1f\xc2\xb0"
		                           "C",
		         d->temperature);
		draw_row(cr, layout, cfg, row++, w, "Temperature", buf);
	}

	if (cfg->weather_popup_show_feels_like && d->have_feels_like) {
		snprintf(buf, sizeof(buf), "%.1f\xc2\xb0"
		                           "C",
		         d->feels_like);
		draw_row(cr, layout, cfg, row++, w, "Feels like", buf);
	}

	if (cfg->weather_popup_show_humidity && d->have_humidity) {
		snprintf(buf, sizeof(buf), "%d%%", d->humidity);
		draw_row(cr, layout, cfg, row++, w, "Humidity", buf);
	}

	if (cfg->weather_popup_show_wind && d->have_wind) {
		if (d->wind_dir[0])
			snprintf(buf, sizeof(buf), "%.1f m/s %s", d->wind_speed,
			         d->wind_dir);
		else
			snprintf(buf, sizeof(buf), "%.1f m/s", d->wind_speed);
		draw_row(cr, layout, cfg, row++, w, "Wind", buf);
	}

	if (cfg->weather_popup_show_pressure && d->have_pressure) {
		snprintf(buf, sizeof(buf), "%d hPa", d->pressure);
		draw_row(cr, layout, cfg, row++, w, "Pressure", buf);
	}

	g_object_unref(layout);
}

static void
weather_on_hover(barny_module_t *self, bool hovering, int x, int y)
{
	weather_data_t *data = self->data;
	(void)x;
	(void)y;

	if (hovering) {
		if (!data->popup && popup_active_rows(data) > 0) {
			barny_popup_callbacks_t cb = {
				.content_height = weather_popup_height,
				.content_width  = weather_popup_width,
				.render         = weather_popup_render,
				.userdata       = data,
			};
			data->popup = barny_popup_create(
			        data->state, self, &cb,
			        data->state->config.weather_popup_gap);
		}
	} else {
		if (data->popup) {
			barny_popup_destroy(data->popup);
			data->popup = NULL;
		}
	}
}

static int
weather_init(barny_module_t *self, barny_state_t *state)
{
	weather_data_t *data = self->data;

	data->state          = state;
	data->self           = self;

	data->font_desc      = pango_font_description_from_string(
	        state->config.font ? state->config.font : "Sans 11");
	data->popup_font_desc
	        = barny_popup_font_from(state->config.font, "Sans 11");

	if (!weather_parse_file(data, data->weather_str,
	                        sizeof(data->weather_str))) {
		snprintf(data->weather_str, sizeof(data->weather_str), "--");
	}

	return 0;
}

static void
weather_destroy(barny_module_t *self)
{
	weather_data_t *data = self->data;
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

	free(data);
	self->data = NULL;
}

static void
weather_update(barny_module_t *self)
{
	weather_data_t *data = self->data;
	char            old_bar[128];
	weather_data_t  prev = *data;
	bool            popup_changed;

	memcpy(old_bar, data->weather_str, sizeof(old_bar));

	if (!weather_parse_file(data, data->weather_str,
	                        sizeof(data->weather_str)))
		return;

	if (strcmp(old_bar, data->weather_str) != 0)
		self->dirty = true;

	popup_changed = (prev.have_temp != data->have_temp)
	                || (prev.temperature != data->temperature)
	                || (prev.have_feels_like != data->have_feels_like)
	                || (prev.feels_like != data->feels_like)
	                || (prev.have_humidity != data->have_humidity)
	                || (prev.humidity != data->humidity)
	                || (prev.have_wind != data->have_wind)
	                || (prev.wind_speed != data->wind_speed)
	                || (prev.wind_deg != data->wind_deg)
	                || (prev.have_pressure != data->have_pressure)
	                || (prev.pressure != data->pressure)
	                || (strcmp(prev.location, data->location) != 0)
	                || (strcmp(prev.condition, data->condition) != 0)
	                || (strcmp(prev.description, data->description) != 0)
	                || (strcmp(prev.wind_dir, data->wind_dir) != 0);

	if (popup_changed && barny_popup_visible(data->popup))
		barny_popup_redraw(data->popup);
}

static void
weather_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	weather_data_t *data = self->data;
	int             tw;
	(void)w;

	tw          = barny_module_render_text(cr, data->font_desc, data->weather_str,
	                                       x, y, h, &data->state->config,
	                                       1, 1, 1, 0.9);
	self->width = tw + 8;
}

barny_module_t *
barny_module_weather_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	weather_data_t *data = calloc(1, sizeof(weather_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name               = "weather";
	mod->position           = BARNY_POS_RIGHT;
	mod->init               = weather_init;
	mod->destroy            = weather_destroy;
	mod->update             = weather_update;
	mod->update_interval_ms = 30000;
	mod->render             = weather_render;
	mod->on_hover           = weather_on_hover;
	mod->data               = data;
	mod->width              = 100;
	mod->dirty              = true;

	return mod;
}
