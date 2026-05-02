#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[64];
	int                   capacity;
	char                  status[16];
	char                  device_path[256];
	PangoFontDescription *font_desc;
} battery_data_t;

static int
detect_battery(char *path_buf, size_t path_len)
{
	DIR *dir = opendir("/sys/class/power_supply");
	if (!dir)
		return -1;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;

		char type_path[512];
		int  n = snprintf(type_path, sizeof(type_path),
		                  "/sys/class/power_supply/%s/type", ent->d_name);
		if (n < 0 || (size_t)n >= sizeof(type_path))
			continue;

		FILE *f = fopen(type_path, "r");
		if (!f)
			continue;

		char type[32] = { 0 };
		if (fgets(type, sizeof(type), f)) {
			/* Strip trailing newline */
			char *nl = strchr(type, '\n');
			if (nl)
				*nl = '\0';
		}
		fclose(f);

		if (strcmp(type, "Battery") == 0) {
			int r = snprintf(path_buf, path_len,
			                 "/sys/class/power_supply/%s/uevent",
			                 ent->d_name);
			if (r < 0 || (size_t)r >= path_len) {
				closedir(dir);
				return -1;
			}
			closedir(dir);
			return 0;
		}
	}

	closedir(dir);
	return -1;
}

static int
battery_init(barny_module_t *self, barny_state_t *state)
{
	battery_data_t *data = self->data;
	data->state          = state;
	data->capacity       = -1;
	data->status[0]      = '\0';

	data->font_desc      = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	/* Determine uevent path */
	if (state->config.battery_path) {
		snprintf(data->device_path, sizeof(data->device_path), "%s",
		         state->config.battery_path);
	} else if (detect_battery(data->device_path, sizeof(data->device_path))
	           < 0) {
		/* Fallback to BAT0 */
		snprintf(data->device_path, sizeof(data->device_path),
		         "/sys/class/power_supply/BAT0/uevent");
	}

	strcpy(data->display_str, "BAT --");

	return 0;
}

static void
battery_destroy(barny_module_t *self)
{
	battery_data_t *data = self->data;
	if (!data)
		return;

	if (data->font_desc)
		pango_font_description_free(data->font_desc);

	free(data);
	self->data = NULL;
}

static void
battery_update(barny_module_t *self)
{
	battery_data_t *data = self->data;
	barny_config_t *cfg  = &data->state->config;

	FILE           *f    = fopen(data->device_path, "r");
	if (!f)
		return;

	char new_status[16] = { 0 };
	int  new_capacity   = -1;
	char line[256];

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "POWER_SUPPLY_STATUS=", 20) == 0) {
			char *val = line + 20;
			char *nl  = strchr(val, '\n');
			if (nl)
				*nl = '\0';
			strncpy(new_status, val, sizeof(new_status) - 1);
			new_status[sizeof(new_status) - 1] = '\0';
		} else if (strncmp(line, "POWER_SUPPLY_CAPACITY=", 22) == 0) {
			new_capacity = atoi(line + 22);
		}
	}
	fclose(f);

	if (new_capacity < 0)
		return;

	/* Check if values changed */
	if (new_capacity == data->capacity
	    && strcmp(new_status, data->status) == 0)
		return;

	data->capacity = new_capacity;
	snprintf(data->status, sizeof(data->status), "%s", new_status);

	/* Format display string */
	const char *sp = cfg->battery_unit_space ? " " : "";
	char        pct_str[16];
	snprintf(pct_str, sizeof(pct_str), "%d%s%%", data->capacity, sp);

	if (cfg->battery_show_status && data->status[0]) {
		/* Abbreviate status */
		const char *prefix;
		if (strcmp(data->status, "Charging") == 0)
			prefix = "CHG";
		else if (strcmp(data->status, "Discharging") == 0)
			prefix = "BAT";
		else if (strcmp(data->status, "Full") == 0)
			prefix = "FULL";
		else if (strcmp(data->status, "Not charging") == 0)
			prefix = "IDLE";
		else
			prefix = data->status;

		snprintf(data->display_str, sizeof(data->display_str), "%s %s",
		         prefix, pct_str);
	} else {
		snprintf(data->display_str, sizeof(data->display_str), "%s",
		         pct_str);
	}

	self->dirty = true;
}

static void
battery_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	battery_data_t *data = self->data;
	(void)w;

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->display_str, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	/* Shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set)
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
		                      cfg->text_color_b, 0.9);
	else
		cairo_set_source_rgba(cr, 0.8, 1, 0.8, 0.9);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_battery_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	battery_data_t *data = calloc(1, sizeof(battery_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name     = "battery";
	mod->position = BARNY_POS_RIGHT;
	mod->init     = battery_init;
	mod->destroy  = battery_destroy;
	mod->update   = battery_update;
	mod->update_interval_ms = 5000;
	mod->render   = battery_render;
	mod->data     = data;
	mod->width    = 80;
	mod->dirty    = true;

	return mod;
}
