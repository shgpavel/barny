#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "util.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[48];
	unsigned long         total_kb;
	unsigned long         used_kb;
	PangoFontDescription *font_desc;
} ram_data_t;

static void
format_size(char *buf, size_t buflen, unsigned long kb, int decimals,
            bool unit_space)
{
	barny_format_bytes(buf, buflen, (unsigned long long)kb * 1024ULL,
	                   decimals, unit_space);
}

static int
ram_init(barny_module_t *self, barny_state_t *state)
{
	ram_data_t *data = self->data;

	data->state      = state;
	data->total_kb   = 0;
	data->used_kb    = 0;

	data->font_desc  = pango_font_description_from_string(
	        state->config.font ? state->config.font : "Sans 10");

	strcpy(data->display_str, "-- RAM");

	return 0;
}

static void
ram_destroy(barny_module_t *self)
{
	ram_data_t *data = self->data;

	if (!data)
		return;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
ram_update(barny_module_t *self)
{
	ram_data_t     *data;
	barny_config_t *cfg;
	FILE           *f;
	unsigned long   mem_total;
	unsigned long   mem_free;
	unsigned long   mem_available;
	unsigned long   buffers;
	unsigned long   cached;
	char            line[256];
	unsigned long   used;
	const char     *method;
	unsigned long   free_kb;
	const char     *mode;
	int             percent;
	const char     *fmt;
	char            used_str[16];
	char            total_str[16];

	data          = self->data;
	cfg           = &data->state->config;
	f             = fopen("/proc/meminfo", "r");
	mem_total     = 0;
	mem_free      = 0;
	mem_available = 0;
	buffers       = 0;
	cached        = 0;

	if (!f)
		return;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "MemTotal:", 9) == 0) {
			sscanf(line, "MemTotal: %lu", &mem_total);
		} else if (strncmp(line, "MemFree:", 8) == 0) {
			sscanf(line, "MemFree: %lu", &mem_free);
		} else if (strncmp(line, "MemAvailable:", 13) == 0) {
			sscanf(line, "MemAvailable: %lu", &mem_available);
		} else if (strncmp(line, "Buffers:", 8) == 0) {
			sscanf(line, "Buffers: %lu", &buffers);
		} else if (strncmp(line, "Cached:", 7) == 0) {
			sscanf(line, "Cached: %lu", &cached);
		}
	}
	fclose(f);

	if (mem_total == 0)
		return;

	method = cfg->ram_used_method;
	if (method && strcmp(method, "free") == 0) {
		used = mem_total - mem_free;
	} else {
		if (mem_available > 0) {
			used = mem_total - mem_available;
		} else {
			used = mem_total - mem_free - buffers - cached;
		}
	}

	if (used != data->used_kb || mem_total != data->total_kb) {
		data->used_kb  = used;
		data->total_kb = mem_total;

		free_kb        = mem_total - used;
		mode           = cfg->ram_mode ? cfg->ram_mode : "used_total";

		if (strcmp(mode, "percentage") == 0) {
			percent = (int)((used * 100) / mem_total);
			fmt     = cfg->ram_unit_space ? "%d %%" : "%d%%";
			snprintf(data->display_str, sizeof(data->display_str), fmt,
			         percent);
		} else if (strcmp(mode, "used") == 0) {
			format_size(data->display_str, sizeof(data->display_str),
			            used, cfg->ram_decimals, cfg->ram_unit_space);
		} else if (strcmp(mode, "free") == 0) {
			format_size(data->display_str, sizeof(data->display_str),
			            free_kb, cfg->ram_decimals,
			            cfg->ram_unit_space);
		} else {
			format_size(used_str, sizeof(used_str), used,
			            cfg->ram_decimals, cfg->ram_unit_space);
			format_size(total_str, sizeof(total_str), mem_total,
			            cfg->ram_decimals, cfg->ram_unit_space);
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s/%s", used_str, total_str);
		}

		self->dirty = true;
	}
}

static void
ram_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	ram_data_t *data = self->data;
	int         tw;

	(void)w;

	tw          = barny_module_render_text(cr, data->font_desc, data->display_str,
	                                       x, y, h, &data->state->config,
	                                       0.8, 1, 0.8, 0.9);
	self->width = tw + 8;
}

barny_module_t *
barny_module_ram_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	ram_data_t     *data = calloc(1, sizeof(ram_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name               = "ram";
	mod->position           = BARNY_POS_RIGHT;
	mod->init               = ram_init;
	mod->destroy            = ram_destroy;
	mod->update             = ram_update;
	mod->update_interval_ms = 1000;
	mod->render             = ram_render;
	mod->data               = data;
	mod->width              = 80;
	mod->dirty              = true;

	return mod;
}
