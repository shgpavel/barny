#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

#include "barny.h"
#include "util.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[48];
	unsigned long long    total_bytes;
	unsigned long long    used_bytes;
	PangoFontDescription *font_desc;
} disk_data_t;

static void
format_bytes(char *buf, size_t buflen, unsigned long long bytes, int decimals,
             bool unit_space)
{
	barny_format_bytes(buf, buflen, bytes, decimals, unit_space);
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
	disk_data_t       *data;
	barny_config_t    *cfg;
	const char        *path;
	struct statvfs     st;
	unsigned long long total;
	unsigned long long avail;
	unsigned long long used;
	const char        *mode;
	int                percent;
	const char        *fmt;
	char               used_str[16];
	char               total_str[16];

	data = self->data;
	cfg  = &data->state->config;
	path = cfg->disk_path ? cfg->disk_path : "/";

	if (statvfs(path, &st) != 0)
		return;

	total = (unsigned long long)st.f_blocks * st.f_frsize;
	avail = (unsigned long long)st.f_bavail * st.f_frsize;
	used  = total - avail;

	if (used != data->used_bytes || total != data->total_bytes) {
		data->used_bytes  = used;
		data->total_bytes = total;

		mode              = cfg->disk_mode ? cfg->disk_mode : "used_total";

		if (strcmp(mode, "percentage") == 0) {
			percent = 0;
			if (total > 0)
				percent = (int)((used * 100) / total);
			fmt = cfg->disk_unit_space ? "%d %%" : "%d%%";
			snprintf(data->display_str, sizeof(data->display_str), fmt,
			         percent);
		} else if (strcmp(mode, "free") == 0) {
			format_bytes(data->display_str, sizeof(data->display_str),
			             avail, cfg->disk_decimals,
			             cfg->disk_unit_space);
		} else {
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
	int          tw;

	(void)w;

	tw          = barny_module_render_text(cr, data->font_desc, data->display_str,
	                                       x, y, h, &data->state->config,
	                                       1, 0.8, 0.9, 0.9);
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

	mod->name               = "disk";
	mod->position           = BARNY_POS_RIGHT;
	mod->init               = disk_init;
	mod->destroy            = disk_destroy;
	mod->update             = disk_update;
	mod->update_interval_ms = 5000;
	mod->render             = disk_render;
	mod->data               = data;
	mod->width              = 80;
	mod->dirty              = true;

	return mod;
}
