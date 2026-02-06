#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[48];
	unsigned long long    total_bytes;
	unsigned long long    used_bytes;
	PangoFontDescription *font_desc;
} disk_data_t;

static void
format_bytes(char *buf, size_t buflen, unsigned long long bytes,
             int decimals, bool unit_space)
{
	const char *sp = unit_space ? " " : "";
	double gb = bytes / (1024.0 * 1024.0 * 1024.0);

	if (gb >= 1000.0) {
		double tb = gb / 1024.0;
		switch (decimals) {
		case 0:  snprintf(buf, buflen, "%.0f%sT", tb, sp); break;
		case 2:  snprintf(buf, buflen, "%.2f%sT", tb, sp); break;
		default: snprintf(buf, buflen, "%.1f%sT", tb, sp); break;
		}
	} else if (gb >= 1.0) {
		switch (decimals) {
		case 0:  snprintf(buf, buflen, "%.0f%sG", gb, sp); break;
		case 2:  snprintf(buf, buflen, "%.2f%sG", gb, sp); break;
		default: snprintf(buf, buflen, "%.1f%sG", gb, sp); break;
		}
	} else {
		double mb = bytes / (1024.0 * 1024.0);
		switch (decimals) {
		case 0:  snprintf(buf, buflen, "%.0f%sM", mb, sp); break;
		case 2:  snprintf(buf, buflen, "%.2f%sM", mb, sp); break;
		default: snprintf(buf, buflen, "%.1f%sM", mb, sp); break;
		}
	}
}

static int
disk_init(barny_module_t *self, barny_state_t *state)
{
	disk_data_t *data = self->data;
	data->state       = state;
	data->total_bytes = 0;
	data->used_bytes  = 0;

	data->font_desc   = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	strcpy(data->display_str, "-- DISK");

	return 0;
}

static void
disk_destroy(barny_module_t *self)
{
	disk_data_t *data = self->data;
	if (!data)
		return;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
disk_update(barny_module_t *self)
{
	disk_data_t    *data = self->data;
	barny_config_t *cfg  = &data->state->config;

	const char *path = cfg->disk_path ? cfg->disk_path : "/";

	struct statvfs st;
	if (statvfs(path, &st) != 0)
		return;

	unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
	unsigned long long avail = (unsigned long long)st.f_bavail * st.f_frsize;
	unsigned long long used  = total - avail;

	/* Check if values changed */
	if (used != data->used_bytes || total != data->total_bytes) {
		data->used_bytes  = used;
		data->total_bytes = total;

		const char *mode = cfg->disk_mode ? cfg->disk_mode : "used_total";

		if (strcmp(mode, "percentage") == 0) {
			int percent = 0;
			if (total > 0)
				percent = (int)((used * 100) / total);
			const char *fmt = cfg->disk_unit_space ? "%d %%" : "%d%%";
			snprintf(data->display_str, sizeof(data->display_str),
			         fmt, percent);
		} else if (strcmp(mode, "free") == 0) {
			format_bytes(data->display_str, sizeof(data->display_str),
			             avail, cfg->disk_decimals, cfg->disk_unit_space);
		} else {
			/* "used_total" (default) */
			char used_str[16];
			char total_str[16];
			format_bytes(used_str, sizeof(used_str), used,
			             cfg->disk_decimals, cfg->disk_unit_space);
			format_bytes(total_str, sizeof(total_str), total,
			             cfg->disk_decimals, cfg->disk_unit_space);
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s/%s", used_str, total_str);
		}

		self->dirty = true;
	}
}

static void
disk_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	disk_data_t *data = self->data;
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

	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set)
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g, cfg->text_color_b, 0.9);
	else
		cairo_set_source_rgba(cr, 1, 0.8, 0.9, 0.9);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_disk_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	disk_data_t    *data = calloc(1, sizeof(disk_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name            = "disk";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = disk_init;
	mod->destroy         = disk_destroy;
	mod->update          = disk_update;
	mod->render          = disk_render;
	mod->data            = data;
	mod->width           = 80;
	mod->dirty           = true;

	return mod;
}
