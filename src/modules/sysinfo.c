#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  freq_str[48];
	char                  power_str[32];
	char                  temp_str[32];
	double                p_freq;
	double                e_freq;
	double                power;
	int                   p_core_count;
	int                   e_core_count;
	/* Temperature */
	char                  temp_path[256];
	int                   current_temp;
	bool                  temp_path_found;
	PangoFontDescription *font_desc;
} sysinfo_data_t;

static int
read_int_file(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	int val;
	if (fscanf(f, "%d", &val) != 1)
		val = -1;
	fclose(f);
	return val;
}

static void
detect_core_counts(sysinfo_data_t *data)
{
	DIR *dir = opendir("/sys/devices/system/cpu");
	if (!dir)
		return;

	int max_freqs[256];
	int cpu_count = 0;
	int highest_freq = 0;
	int lowest_freq = 0;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL && cpu_count < 256) {
		if (strncmp(entry->d_name, "cpu", 3) != 0)
			continue;
		if (!isdigit(entry->d_name[3]))
			continue;

		int cpu_id = atoi(entry->d_name + 3);
		char path[256];

		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu_id);
		int max_freq = read_int_file(path);
		if (max_freq < 0)
			continue;

		max_freqs[cpu_count] = max_freq;

		if (max_freq > highest_freq)
			highest_freq = max_freq;
		if (lowest_freq == 0 || max_freq < lowest_freq)
			lowest_freq = max_freq;

		cpu_count++;
	}
	closedir(dir);

	/* Classify P vs E cores based on max frequency gap */
	int gap = highest_freq - lowest_freq;
	int threshold = (gap > 100000) ? lowest_freq + 100000 : 0;

	for (int i = 0; i < cpu_count; i++) {
		if (max_freqs[i] >= threshold)
			data->p_core_count++;
		else
			data->e_core_count++;
	}
}

/* Temperature detection functions */
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
find_temp_path(sysinfo_data_t *data, barny_config_t *cfg)
{
	data->temp_path_found = false;

	/* Priority 1: User-specified path */
	if (cfg->sysinfo_temp_path && cfg->sysinfo_temp_path[0]) {
		strncpy(data->temp_path, cfg->sysinfo_temp_path, sizeof(data->temp_path) - 1);
		data->temp_path[sizeof(data->temp_path) - 1] = '\0';
		data->temp_path_found = true;
		return;
	}

	/* Priority 2: User-specified thermal zone */
	if (cfg->sysinfo_temp_zone >= 0) {
		if (try_thermal_zone(data->temp_path, sizeof(data->temp_path),
		                     cfg->sysinfo_temp_zone)) {
			data->temp_path_found = true;
			return;
		}
	}

	/* Priority 3: Auto-detect thermal zones */
	for (int i = 0; i < 16; i++) {
		if (try_thermal_zone(data->temp_path, sizeof(data->temp_path), i)) {
			data->temp_path_found = true;
			return;
		}
	}

	/* Priority 4: Try hwmon fallback */
	if (try_hwmon(data->temp_path, sizeof(data->temp_path))) {
		data->temp_path_found = true;
		return;
	}

	/* Last resort: try zone 0 anyway */
	snprintf(data->temp_path, sizeof(data->temp_path),
	         "/sys/class/thermal/thermal_zone0/temp");
	data->temp_path_found = true;
}

static int
sysinfo_init(barny_module_t *self, barny_state_t *state)
{
	sysinfo_data_t *data = self->data;
	data->state          = state;

	data->font_desc      = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	strcpy(data->freq_str, "-- GHz");
	strcpy(data->power_str, "-- W");
	strcpy(data->temp_str, "-- C");
	data->current_temp = -1;

	detect_core_counts(data);
	find_temp_path(data, &state->config);

	return 0;
}

static void
sysinfo_destroy(barny_module_t *self)
{
	sysinfo_data_t *data = self->data;
	if (!data)
		return;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
sysinfo_update(barny_module_t *self)
{
	sysinfo_data_t *data = self->data;
	barny_config_t *cfg  = &data->state->config;

	/* Read CPU frequency */
	FILE           *f    = fopen("/opt/barny/modules/cpu_freq", "r");
	if (f) {
		char line[64];
		if (fgets(line, sizeof(line), f)) {
			double p_freq = 0, e_freq = 0;
			if (sscanf(line, "P: %lf E: %lf", &p_freq, &e_freq) == 2) {
				if (p_freq != data->p_freq || e_freq != data->e_freq) {
					data->p_freq = p_freq;
					data->e_freq = e_freq;

					if (cfg->sysinfo_freq_combined) {
						int total = data->p_core_count + data->e_core_count;
						double avg = (total > 0)
						    ? (p_freq * data->p_core_count + e_freq * data->e_core_count) / total
						    : 0.0;
						if (cfg->sysinfo_freq_show_unit) {
							const char *fmt = cfg->sysinfo_freq_unit_space
							    ? "%.2f GHz" : "%.2fGHz";
							snprintf(data->freq_str, sizeof(data->freq_str),
							         fmt, avg);
						} else {
							snprintf(data->freq_str, sizeof(data->freq_str),
							         "%.2f", avg);
						}
					} else {
						/* Build format based on label_space and show_unit */
						const char *label_sep = cfg->sysinfo_freq_label_space ? " " : "";
						const char *unit_sep = cfg->sysinfo_freq_unit_space ? " " : "";
						const char *unit = cfg->sysinfo_freq_show_unit ? "GHz" : "";

						if (cfg->sysinfo_freq_show_unit) {
							snprintf(data->freq_str, sizeof(data->freq_str),
							         "P:%s%.2f%s%s E:%s%.2f%s%s",
							         label_sep, p_freq, unit_sep, unit,
							         label_sep, e_freq, unit_sep, unit);
						} else {
							snprintf(data->freq_str, sizeof(data->freq_str),
							         "P:%s%.2f E:%s%.2f",
							         label_sep, p_freq, label_sep, e_freq);
						}
					}
					self->dirty = true;
				}
			}
		}
		fclose(f);
	}

	/* Read power consumption */
	f = fopen("/opt/barny/modules/cpu_power", "r");
	if (f) {
		char line[64];
		if (fgets(line, sizeof(line), f)) {
			double power;
			if (sscanf(line, "PWR: %lf", &power) == 1) {
				if (power != data->power) {
					data->power = power;

					const char *fmt;
					if (cfg->sysinfo_power_unit_space) {
						switch (cfg->sysinfo_power_decimals) {
						case 1:  fmt = "%.1f W"; break;
						case 2:  fmt = "%.2f W"; break;
						default: fmt = "%.0f W"; break;
						}
					} else {
						switch (cfg->sysinfo_power_decimals) {
						case 1:  fmt = "%.1fW"; break;
						case 2:  fmt = "%.2fW"; break;
						default: fmt = "%.0fW"; break;
						}
					}
					snprintf(data->power_str,
					         sizeof(data->power_str),
					         fmt,
					         power);
					self->dirty = true;
				}
			}
		}
		fclose(f);
	}

	/* Read CPU temperature */
	if (data->temp_path_found) {
		f = fopen(data->temp_path, "r");
		if (f) {
			int millicelsius = 0;
			if (fscanf(f, "%d", &millicelsius) == 1) {
				int celsius = millicelsius / 1000;

				if (celsius != data->current_temp) {
					data->current_temp = celsius;

					if (cfg->sysinfo_temp_show_unit) {
						const char *fmt = cfg->sysinfo_temp_unit_space ? "%d C" : "%dC";
						snprintf(data->temp_str, sizeof(data->temp_str),
						         fmt, celsius);
					} else {
						snprintf(data->temp_str, sizeof(data->temp_str),
						         "%d", celsius);
					}
					self->dirty = true;
				}
			}
			fclose(f);
		}
	}
}

/* Helper to render text with shadow */
static int
render_text(cairo_t *cr, PangoLayout *layout, const char *text,
            int x, int y, int h, double r, double g, double b)
{
	pango_layout_set_text(layout, text, -1);

	int tw, th;
	pango_layout_get_pixel_size(layout, &tw, &th);

	int text_y = y + (h - th) / 2;

	/* Shadow */
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, text_y + 1);
	pango_cairo_show_layout(cr, layout);

	/* Text */
	cairo_set_source_rgba(cr, r, g, b, 0.9);
	cairo_move_to(cr, x, text_y);
	pango_cairo_show_layout(cr, layout);

	return tw;
}

static void
sysinfo_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	sysinfo_data_t *data = self->data;
	barny_config_t *cfg  = &data->state->config;
	(void)w;

	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);

	int total_width = 0;
	int item_spacing = cfg->sysinfo_item_spacing;
	double r, g, b;

	/* Render frequency */
	if (cfg->text_color_set) {
		r = cfg->text_color_r; g = cfg->text_color_g; b = cfg->text_color_b;
	} else {
		r = 0.7; g = 0.9; b = 1.0;
	}
	total_width += render_text(cr, layout, data->freq_str, x + total_width, y, h, r, g, b);
	total_width += item_spacing;

	/* Render power */
	if (cfg->text_color_set) {
		r = cfg->text_color_r; g = cfg->text_color_g; b = cfg->text_color_b;
	} else {
		r = 1.0; g = 0.9; b = 0.7;
	}
	total_width += render_text(cr, layout, data->power_str, x + total_width, y, h, r, g, b);
	total_width += item_spacing;

	/* Render temperature with color coding */
	if (cfg->text_color_set) {
		r = cfg->text_color_r; g = cfg->text_color_g; b = cfg->text_color_b;
	} else {
		r = 0.9; g = 0.9; b = 0.7;
		if (data->current_temp >= 80) {
			r = 1.0; g = 0.4; b = 0.4;  /* Hot - red */
		} else if (data->current_temp >= 60) {
			r = 1.0; g = 0.7; b = 0.4;  /* Warm - orange */
		}
	}
	total_width += render_text(cr, layout, data->temp_str, x + total_width, y, h, r, g, b);

	g_object_unref(layout);

	self->width = total_width + 8;
}

barny_module_t *
barny_module_sysinfo_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	sysinfo_data_t *data = calloc(1, sizeof(sysinfo_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name            = "sysinfo";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = sysinfo_init;
	mod->destroy         = sysinfo_destroy;
	mod->update          = sysinfo_update;
	mod->render          = sysinfo_render;
	mod->data            = data;
	mod->width           = 180;
	mod->dirty           = true;

	return mod;
}
