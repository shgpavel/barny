#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#include "barny.h"
#include "popup.h"

#define LINE_H              26
#define MAX_PER_CORE_ROWS   32

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
	PangoFontDescription *popup_font_desc;

	/* Popup state */
	barny_popup_t        *popup;
	long                  uptime_seconds;
	char                  uptime_str[32];
	double                load_avg[3];
	char                  load_str[48];
	int                   per_core_khz[MAX_PER_CORE_ROWS];
	int                   per_core_count;
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

	int            max_freqs[256];
	int            cpu_count    = 0;
	int            highest_freq = 0;
	int            lowest_freq  = 0;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL && cpu_count < 256) {
		if (strncmp(entry->d_name, "cpu", 3) != 0)
			continue;
		if (!isdigit(entry->d_name[3]))
			continue;

		int  cpu_id = atoi(entry->d_name + 3);
		char path[256];

		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",
		         cpu_id);
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
	int gap       = highest_freq - lowest_freq;
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
	if (strstr(type, "cpu")
	    || strstr(type, "CPU")
	    || strstr(type, "x86_pkg")
	    || strstr(type, "coretemp")
	    || strstr(type, "k10temp")
	    || strstr(type, "acpitz")) {
		snprintf(path, pathlen, "/sys/class/thermal/thermal_zone%d/temp",
		         zone);
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
		snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%s/name",
		         ent->d_name);

		FILE *f = fopen(name_path, "r");
		if (!f)
			continue;

		char name[64] = "";
		if (fgets(name, sizeof(name), f)) {
			name[strcspn(name, "\n")] = '\0';
		}
		fclose(f);

		/* Look for CPU-related hwmon devices */
		if (strstr(name, "coretemp")
		    || strstr(name, "k10temp")
		    || strstr(name, "cpu")
		    || strstr(name, "zenpower")) {
			snprintf(path, pathlen, "/sys/class/hwmon/%s/temp1_input",
			         ent->d_name);

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
		strncpy(data->temp_path, cfg->sysinfo_temp_path,
		        sizeof(data->temp_path) - 1);
		data->temp_path[sizeof(data->temp_path) - 1] = '\0';
		data->temp_path_found                        = true;
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
		if (try_thermal_zone(data->temp_path, sizeof(data->temp_path),
		                     i)) {
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

static long
read_uptime_seconds(void)
{
	FILE *f = fopen("/proc/uptime", "r");
	if (!f)
		return -1;
	double up = 0.0;
	if (fscanf(f, "%lf", &up) != 1) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return (long)up;
}

static bool
read_loadavg(double load[3])
{
	FILE *f = fopen("/proc/loadavg", "r");
	if (!f)
		return false;
	bool ok = (fscanf(f, "%lf %lf %lf", &load[0], &load[1], &load[2]) == 3);
	fclose(f);
	return ok;
}

static int
read_per_core_freqs(int *out_khz, int max)
{
	DIR *dir = opendir("/sys/devices/system/cpu");
	if (!dir)
		return 0;

	int  ids[256];
	int  id_count = 0;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL && id_count < 256) {
		if (strncmp(entry->d_name, "cpu", 3) != 0)
			continue;
		if (!isdigit(entry->d_name[3]))
			continue;
		ids[id_count++] = atoi(entry->d_name + 3);
	}
	closedir(dir);

	/* Simple insertion sort to keep core ids ordered */
	for (int i = 1; i < id_count; i++) {
		int key = ids[i];
		int j   = i - 1;
		while (j >= 0 && ids[j] > key) {
			ids[j + 1] = ids[j];
			j--;
		}
		ids[j + 1] = key;
	}

	int out = 0;
	for (int i = 0; i < id_count && out < max; i++) {
		char path[256];
		snprintf(path, sizeof(path),
		         "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",
		         ids[i]);
		int khz = read_int_file(path);
		if (khz < 0)
			continue;
		out_khz[out++] = khz;
	}
	return out;
}

static void
format_uptime(long secs, char *buf, size_t buf_size)
{
	if (secs < 0) {
		snprintf(buf, buf_size, "--");
		return;
	}
	long days  = secs / 86400;
	long hours = (secs % 86400) / 3600;
	long mins  = (secs % 3600) / 60;
	if (days > 0)
		snprintf(buf, buf_size, "%ldd %ldh %ldm", days, hours, mins);
	else
		snprintf(buf, buf_size, "%ldh %ldm", hours, mins);
}

static void
format_loadavg(const double load[3], char *buf, size_t buf_size)
{
	snprintf(buf, buf_size, "%.2f / %.2f / %.2f", load[0], load[1], load[2]);
}

static int
sysinfo_popup_row_count(const sysinfo_data_t *data)
{
	const barny_config_t *cfg = &data->state->config;
	int rows = 0;

	rows++; /* CPU summary (avg freq + power + temp) */
	if (data->p_core_count > 0 && data->e_core_count > 0)
		rows++; /* P/E avg */
	rows++; /* Power */
	rows++; /* Temperature */
	rows++; /* Uptime */
	rows++; /* Load average */

	if (cfg->sysinfo_popup_per_core) {
		int n = data->per_core_count;
		if (n > MAX_PER_CORE_ROWS)
			n = MAX_PER_CORE_ROWS;
		rows += n;
	}

	return rows;
}

static int
sysinfo_popup_height(void *ud)
{
	const sysinfo_data_t *data = ud;
	return sysinfo_popup_row_count(data) * LINE_H;
}

static void
draw_label_value_row(cairo_t *cr, PangoLayout *layout, int row_y, int w,
                     const char *label, const char *value, double lr,
                     double lg, double lb, double vr, double vg, double vb)
{
	int tw, th, text_y;

	/* Label, left-aligned */
	pango_layout_set_text(layout, label, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	text_y = row_y + (LINE_H - th) / 2;

	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_move_to(cr, 1, text_y + 1);
	pango_cairo_show_layout(cr, layout);

	cairo_set_source_rgba(cr, lr, lg, lb, 0.9);
	cairo_move_to(cr, 0, text_y);
	pango_cairo_show_layout(cr, layout);

	/* Value, right-aligned */
	pango_layout_set_text(layout, value, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	text_y = row_y + (LINE_H - th) / 2;
	int x  = w - tw;

	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_move_to(cr, x + 1, text_y + 1);
	pango_cairo_show_layout(cr, layout);

	cairo_set_source_rgba(cr, vr, vg, vb, 0.9);
	cairo_move_to(cr, x, text_y);
	pango_cairo_show_layout(cr, layout);
}

static void
sysinfo_popup_render(void *ud, cairo_t *cr, int w, int h)
{
	sysinfo_data_t  *data = ud;
	barny_config_t  *cfg  = &data->state->config;
	PangoLayout     *layout;
	int              row    = 0;
	double           lr = 0.6, lg = 0.7, lb = 0.65;
	double           vr = 0.85, vg = 0.95, vb = 0.85;

	(void)h;

	if (cfg->text_color_set) {
		vr = cfg->text_color_r;
		vg = cfg->text_color_g;
		vb = cfg->text_color_b;
	}

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->popup_font_desc);

	/* Row 1: CPU summary */
	{
		double avg = 0.0;
		bool   hybrid = (data->p_core_count > 0 && data->e_core_count > 0);
		if (hybrid) {
			int total = data->p_core_count + data->e_core_count;
			avg = (total > 0) ? (data->p_freq * data->p_core_count
			                     + data->e_freq * data->e_core_count)
			                            / total
			                  : 0.0;
		} else {
			avg = data->p_freq;
		}

		char value[96];
		const char *power_fmt;
		switch (cfg->sysinfo_power_decimals) {
		case 1:  power_fmt = "%.1fW"; break;
		case 2:  power_fmt = "%.2fW"; break;
		default: power_fmt = "%.0fW"; break;
		}

		char power_buf[32];
		snprintf(power_buf, sizeof(power_buf), power_fmt, data->power);

		const char *temp_unit = cfg->sysinfo_temp_show_unit ? "C" : "";
		snprintf(value, sizeof(value), "%.2fGHz  %s  %d%s", avg, power_buf,
		         data->current_temp, temp_unit);

		draw_label_value_row(cr, layout, row * LINE_H, w, "CPU", value,
		                     lr, lg, lb, vr, vg, vb);
		row++;
	}

	/* Row 2: P/E avg if heterogeneous */
	if (data->p_core_count > 0 && data->e_core_count > 0) {
		char value[64];
		snprintf(value, sizeof(value), "P:%.2f  E:%.2f GHz", data->p_freq,
		         data->e_freq);
		draw_label_value_row(cr, layout, row * LINE_H, w, "P/E avg",
		                     value, lr, lg, lb, vr, vg, vb);
		row++;
	}

	/* Row 3: Power */
	{
		const char *fmt;
		switch (cfg->sysinfo_power_decimals) {
		case 1:  fmt = "%.1f W"; break;
		case 2:  fmt = "%.2f W"; break;
		default: fmt = "%.0f W"; break;
		}
		char value[32];
		snprintf(value, sizeof(value), fmt, data->power);
		draw_label_value_row(cr, layout, row * LINE_H, w, "Power",
		                     value, lr, lg, lb, vr, vg, vb);
		row++;
	}

	/* Row 4: Temperature */
	{
		char value[32];
		if (cfg->sysinfo_temp_show_unit) {
			const char *fmt = cfg->sysinfo_temp_unit_space ? "%d C" : "%dC";
			snprintf(value, sizeof(value), fmt, data->current_temp);
		} else {
			snprintf(value, sizeof(value), "%d", data->current_temp);
		}
		draw_label_value_row(cr, layout, row * LINE_H, w, "Temp",
		                     value, lr, lg, lb, vr, vg, vb);
		row++;
	}

	/* Row 5: Uptime */
	draw_label_value_row(cr, layout, row * LINE_H, w, "Uptime",
	                     data->uptime_str, lr, lg, lb, vr, vg, vb);
	row++;

	/* Row 6: Load average */
	draw_label_value_row(cr, layout, row * LINE_H, w, "Load",
	                     data->load_str, lr, lg, lb, vr, vg, vb);
	row++;

	/* Per-core rows */
	if (cfg->sysinfo_popup_per_core) {
		int n = data->per_core_count;
		if (n > MAX_PER_CORE_ROWS)
			n = MAX_PER_CORE_ROWS;
		for (int i = 0; i < n; i++) {
			char label[16];
			char value[24];
			snprintf(label, sizeof(label), "cpu%d", i);
			snprintf(value, sizeof(value), "%.2f GHz",
			         data->per_core_khz[i] / 1000000.0);
			draw_label_value_row(cr, layout, row * LINE_H, w, label,
			                     value, lr, lg, lb, vr, vg, vb);
			row++;
		}
	}

	g_object_unref(layout);
}

static void
sysinfo_on_hover(barny_module_t *self, bool hovering, int x, int y)
{
	sysinfo_data_t *data = self->data;
	(void)x;
	(void)y;

	if (hovering) {
		if (!data->popup) {
			barny_popup_callbacks_t cb = {
				.content_height = sysinfo_popup_height,
				.content_width  = NULL,
				.render         = sysinfo_popup_render,
				.userdata       = data,
			};
			data->popup = barny_popup_create(
			        data->state, self, &cb,
			        data->state->config.sysinfo_popup_gap);
		}
	} else {
		if (data->popup) {
			barny_popup_destroy(data->popup);
			data->popup = NULL;
		}
	}
}

static int
sysinfo_init(barny_module_t *self, barny_state_t *state)
{
	sysinfo_data_t *data = self->data;
	data->state          = state;

	data->font_desc      = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");
	data->popup_font_desc = pango_font_description_from_string(
	        state->config.font ? state->config.font : "Sans 10");

	int base_size = pango_font_description_get_size(data->popup_font_desc);
	if (base_size > 0) {
		pango_font_description_set_size(data->popup_font_desc,
		                                base_size * 85 / 100);
	} else {
		pango_font_description_set_size(data->popup_font_desc,
		                                9 * PANGO_SCALE);
	}

	strcpy(data->freq_str, "-- GHz");
	strcpy(data->power_str, "-- W");
	strcpy(data->temp_str, "-- C");
	data->current_temp   = -1;
	data->uptime_seconds = -1;
	snprintf(data->uptime_str, sizeof(data->uptime_str), "--");
	snprintf(data->load_str, sizeof(data->load_str), "-- / -- / --");

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

	if (data->state && data->state->hover_module == self)
		data->state->hover_module = NULL;

	barny_popup_destroy(data->popup);
	data->popup = NULL;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
	if (data->popup_font_desc) {
		pango_font_description_free(data->popup_font_desc);
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
			bool   hybrid = false;
			bool   parsed = false;

			if (sscanf(line, "P: %lf E: %lf", &p_freq, &e_freq)
			    == 2) {
				hybrid = true;
				parsed = true;
			} else if (sscanf(line, "%lf", &p_freq) == 1) {
				/* Homogeneous CPU: single frequency value */
				e_freq = 0;
				parsed = true;
			}

			if (parsed
			    && (p_freq != data->p_freq
			        || e_freq != data->e_freq)) {
				data->p_freq = p_freq;
				data->e_freq = e_freq;

				if (!hybrid || cfg->sysinfo_freq_combined) {
					double avg;
					if (!hybrid) {
						avg = p_freq;
					} else {
						int total = data->p_core_count
						            + data->e_core_count;
						avg = (total > 0) ?
						              (p_freq * data->p_core_count
						               + e_freq
						                         * data->e_core_count)
						                      / total :
						              0.0;
					}
					if (cfg->sysinfo_freq_show_unit) {
						const char *fmt
						        = cfg->sysinfo_freq_unit_space ?
						                  "%.2f GHz" :
						                  "%.2fGHz";
						snprintf(
						        data->freq_str,
						        sizeof(data->freq_str),
						        fmt, avg);
					} else {
						snprintf(
						        data->freq_str,
						        sizeof(data->freq_str),
						        "%.2f", avg);
					}
				} else {
					/* Build format based on label_space and show_unit */
					const char *label_sep
					        = cfg->sysinfo_freq_label_space ?
					                  " " :
					                  "";
					const char *unit_sep
					        = cfg->sysinfo_freq_unit_space ?
					                  " " :
					                  "";
					const char *unit
					        = cfg->sysinfo_freq_show_unit ?
					                  "GHz" :
					                  "";

					if (cfg->sysinfo_freq_show_unit) {
						snprintf(
						        data->freq_str,
						        sizeof(data->freq_str),
						        "P:%s%.2f%s%s E:%s%.2f%s%s",
						        label_sep, p_freq,
						        unit_sep, unit,
						        label_sep, e_freq,
						        unit_sep, unit);
					} else {
						snprintf(
						        data->freq_str,
						        sizeof(data->freq_str),
						        "P:%s%.2f E:%s%.2f",
						        label_sep, p_freq,
						        label_sep, e_freq);
					}
				}
				self->dirty = true;
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
						case 1:
							fmt = "%.1f W";
							break;
						case 2:
							fmt = "%.2f W";
							break;
						default:
							fmt = "%.0f W";
							break;
						}
					} else {
						switch (cfg->sysinfo_power_decimals) {
						case 1:
							fmt = "%.1fW";
							break;
						case 2:
							fmt = "%.2fW";
							break;
						default:
							fmt = "%.0fW";
							break;
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
						const char *fmt
						        = cfg->sysinfo_temp_unit_space ?
						                  "%d C" :
						                  "%dC";
						snprintf(data->temp_str,
						         sizeof(data->temp_str),
						         fmt, celsius);
					} else {
						snprintf(data->temp_str,
						         sizeof(data->temp_str),
						         "%d", celsius);
					}
					self->dirty = true;
				}
			}
			fclose(f);
		}
	}

	/* Popup-only data: only refresh if popup visible */
	bool popup_changed = false;

	long up = read_uptime_seconds();
	if (up >= 0 && up != data->uptime_seconds) {
		long old_min = data->uptime_seconds / 60;
		long new_min = up / 60;
		data->uptime_seconds = up;
		if (new_min != old_min) {
			format_uptime(up, data->uptime_str,
			              sizeof(data->uptime_str));
			popup_changed = true;
		}
	}

	double load[3];
	if (read_loadavg(load)) {
		if (load[0] != data->load_avg[0]
		    || load[1] != data->load_avg[1]
		    || load[2] != data->load_avg[2]) {
			data->load_avg[0] = load[0];
			data->load_avg[1] = load[1];
			data->load_avg[2] = load[2];
			format_loadavg(load, data->load_str,
			               sizeof(data->load_str));
			popup_changed = true;
		}
	}

	if (cfg->sysinfo_popup_per_core) {
		int  new_freqs[MAX_PER_CORE_ROWS];
		int  n = read_per_core_freqs(new_freqs, MAX_PER_CORE_ROWS);
		bool diff = (n != data->per_core_count);
		for (int i = 0; !diff && i < n; i++)
			if (new_freqs[i] != data->per_core_khz[i])
				diff = true;
		if (diff) {
			data->per_core_count = n;
			for (int i = 0; i < n; i++)
				data->per_core_khz[i] = new_freqs[i];
			popup_changed = true;
		}
	}

	if (popup_changed && barny_popup_visible(data->popup))
		barny_popup_redraw(data->popup);
}

/* Helper to render text with shadow */
static int
render_text(cairo_t *cr, PangoLayout *layout, const char *text, int x, int y,
            int h, double r, double g, double b)
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

	int    total_width  = 0;
	int    item_spacing = cfg->sysinfo_item_spacing;
	double r, g, b;

	/* Render frequency */
	if (cfg->text_color_set) {
		r = cfg->text_color_r;
		g = cfg->text_color_g;
		b = cfg->text_color_b;
	} else {
		r = 0.7;
		g = 0.9;
		b = 1.0;
	}
	total_width += render_text(cr, layout, data->freq_str, x + total_width, y,
	                           h, r, g, b);
	total_width += item_spacing;

	/* Render power */
	if (cfg->text_color_set) {
		r = cfg->text_color_r;
		g = cfg->text_color_g;
		b = cfg->text_color_b;
	} else {
		r = 1.0;
		g = 0.9;
		b = 0.7;
	}
	total_width += render_text(cr, layout, data->power_str, x + total_width, y,
	                           h, r, g, b);
	total_width += item_spacing;

	/* Render temperature with color coding */
	if (cfg->text_color_set) {
		r = cfg->text_color_r;
		g = cfg->text_color_g;
		b = cfg->text_color_b;
	} else {
		r = 0.9;
		g = 0.9;
		b = 0.7;
		if (data->current_temp >= 80) {
			r = 1.0;
			g = 0.4;
			b = 0.4; /* Hot - red */
		} else if (data->current_temp >= 60) {
			r = 1.0;
			g = 0.7;
			b = 0.4; /* Warm - orange */
		}
	}
	total_width += render_text(cr, layout, data->temp_str, x + total_width, y,
	                           h, r, g, b);

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

	mod->name     = "sysinfo";
	mod->position = BARNY_POS_RIGHT;
	mod->init     = sysinfo_init;
	mod->destroy  = sysinfo_destroy;
	mod->update   = sysinfo_update;
	mod->render   = sysinfo_render;
	mod->on_hover = sysinfo_on_hover;
	mod->data     = data;
	mod->width    = 180;
	mod->dirty    = true;

	return mod;
}
