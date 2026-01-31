#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[32];
	char                  temp_path[256];
	int                   current_temp;
	bool                  path_found;
	PangoFontDescription *font_desc;
} cpu_temp_data_t;

static bool
try_thermal_zone(char *path, size_t pathlen, int zone)
{
	char type_path[256];
	snprintf(type_path, sizeof(type_path),
	         "/sys/class/thermal/thermal_zone%d/type", zone);

	FILE *f = fopen(type_path, "r");
	if (!f)
		return false;

	char type[64] = "";
	if (fgets(type, sizeof(type), f)) {
		/* Remove trailing newline */
		type[strcspn(type, "\n")] = '\0';
	}
	fclose(f);

	/* Look for common CPU thermal zone names */
	if (strstr(type, "cpu") || strstr(type, "CPU") ||
	    strstr(type, "x86_pkg") || strstr(type, "coretemp") ||
	    strstr(type, "k10temp") || strstr(type, "acpitz")) {
		snprintf(path, pathlen,
		         "/sys/class/thermal/thermal_zone%d/temp", zone);
		return true;
	}

	return false;
}

static bool
try_hwmon(char *path, size_t pathlen)
{
	DIR *dir = opendir("/sys/class/hwmon");
	if (!dir)
		return false;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, "hwmon", 5) != 0)
			continue;

		/* Skip entries with overly long names */
		if (strlen(ent->d_name) > 64)
			continue;

		char name_path[256];
		snprintf(name_path, sizeof(name_path),
		         "/sys/class/hwmon/%s/name", ent->d_name);

		FILE *f = fopen(name_path, "r");
		if (!f)
			continue;

		char name[64] = "";
		if (fgets(name, sizeof(name), f)) {
			name[strcspn(name, "\n")] = '\0';
		}
		fclose(f);

		/* Look for CPU-related hwmon devices */
		if (strstr(name, "coretemp") || strstr(name, "k10temp") ||
		    strstr(name, "cpu") || strstr(name, "zenpower")) {
			snprintf(path, pathlen,
			         "/sys/class/hwmon/%s/temp1_input", ent->d_name);

			/* Verify the file exists */
			f = fopen(path, "r");
			if (f) {
				fclose(f);
				closedir(dir);
				return true;
			}
		}
	}

	closedir(dir);
	return false;
}

static void
find_temp_path(cpu_temp_data_t *data, barny_config_t *cfg)
{
	data->path_found = false;

	/* Priority 1: User-specified path */
	if (cfg->cpu_temp_path && cfg->cpu_temp_path[0]) {
		strncpy(data->temp_path, cfg->cpu_temp_path, sizeof(data->temp_path) - 1);
		data->temp_path[sizeof(data->temp_path) - 1] = '\0';
		data->path_found = true;
		return;
	}

	/* Priority 2: User-specified thermal zone */
	if (cfg->cpu_temp_zone >= 0) {
		if (try_thermal_zone(data->temp_path, sizeof(data->temp_path),
		                     cfg->cpu_temp_zone)) {
			data->path_found = true;
			return;
		}
	}

	/* Priority 3: Auto-detect thermal zones */
	for (int i = 0; i < 16; i++) {
		if (try_thermal_zone(data->temp_path, sizeof(data->temp_path), i)) {
			data->path_found = true;
			return;
		}
	}

	/* Priority 4: Try hwmon fallback */
	if (try_hwmon(data->temp_path, sizeof(data->temp_path))) {
		data->path_found = true;
		return;
	}

	/* Last resort: try zone 0 anyway */
	snprintf(data->temp_path, sizeof(data->temp_path),
	         "/sys/class/thermal/thermal_zone0/temp");
	data->path_found = true;
}

static int
cpu_temp_init(barny_module_t *self, barny_state_t *state)
{
	cpu_temp_data_t *data = self->data;
	data->state           = state;
	data->current_temp    = -1;
	data->path_found      = false;

	data->font_desc       = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	strcpy(data->display_str, "-- C");

	find_temp_path(data, &state->config);

	return 0;
}

static void
cpu_temp_destroy(barny_module_t *self)
{
	cpu_temp_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
cpu_temp_update(barny_module_t *self)
{
	cpu_temp_data_t *data = self->data;
	barny_config_t  *cfg  = &data->state->config;

	if (!data->path_found)
		return;

	FILE *f = fopen(data->temp_path, "r");
	if (!f)
		return;

	int millicelsius = 0;
	if (fscanf(f, "%d", &millicelsius) != 1) {
		fclose(f);
		return;
	}
	fclose(f);

	int celsius = millicelsius / 1000;

	if (celsius != data->current_temp) {
		data->current_temp = celsius;

		if (cfg->cpu_temp_show_unit) {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%d C", celsius);
		} else {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%d", celsius);
		}

		self->dirty = true;
	}
}

static void
cpu_temp_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	cpu_temp_data_t *data = self->data;
	(void)w;

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);
	pango_layout_set_text(layout, data->display_str, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	/* Draw with shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	/* Color based on temperature (or custom if set) */
	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set) {
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g, cfg->text_color_b, 0.9);
	} else {
		double r = 0.9, g = 0.9, b = 0.7;
		if (data->current_temp >= 80) {
			r = 1.0; g = 0.4; b = 0.4;  /* Hot - red */
		} else if (data->current_temp >= 60) {
			r = 1.0; g = 0.7; b = 0.4;  /* Warm - orange */
		}
		cairo_set_source_rgba(cr, r, g, b, 0.9);
	}
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_cpu_temp_create(void)
{
	barny_module_t  *mod  = calloc(1, sizeof(barny_module_t));
	cpu_temp_data_t *data = calloc(1, sizeof(cpu_temp_data_t));

	mod->name             = "cpu_temp";
	mod->position         = BARNY_POS_RIGHT;
	mod->init             = cpu_temp_init;
	mod->destroy          = cpu_temp_destroy;
	mod->update           = cpu_temp_update;
	mod->render           = cpu_temp_render;
	mod->data             = data;
	mod->width            = 50;
	mod->dirty            = true;

	return mod;
}
