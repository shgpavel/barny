#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[48];
	unsigned long         total_kb;
	unsigned long         used_kb;
	PangoFontDescription *font_desc;
} ram_data_t;

static void
format_size(char *buf, size_t buflen, unsigned long kb,
            int decimals, bool unit_space)
{
	const char *sp = unit_space ? " " : "";
	double gb = kb / 1048576.0;

	if (gb >= 1.0) {
		switch (decimals) {
		case 0:  snprintf(buf, buflen, "%.0f%sG", gb, sp); break;
		case 2:  snprintf(buf, buflen, "%.2f%sG", gb, sp); break;
		default: snprintf(buf, buflen, "%.1f%sG", gb, sp); break;
		}
	} else {
		double mb = kb / 1024.0;
		switch (decimals) {
		case 0:  snprintf(buf, buflen, "%.0f%sM", mb, sp); break;
		case 2:  snprintf(buf, buflen, "%.2f%sM", mb, sp); break;
		default: snprintf(buf, buflen, "%.1f%sM", mb, sp); break;
		}
	}
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
	ram_data_t     *data = self->data;
	barny_config_t *cfg  = &data->state->config;

	FILE *f = fopen("/proc/meminfo", "r");
	if (!f)
		return;

	unsigned long mem_total     = 0;
	unsigned long mem_free      = 0;
	unsigned long mem_available = 0;
	unsigned long buffers       = 0;
	unsigned long cached        = 0;

	char line[256];
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

	/* Calculate used memory based on method */
	unsigned long used;
	const char *method = cfg->ram_used_method;
	if (method && strcmp(method, "free") == 0) {
		/* Simple: total - free */
		used = mem_total - mem_free;
	} else {
		/* Default (available): total - available */
		if (mem_available > 0) {
			used = mem_total - mem_available;
		} else {
			/* Fallback if MemAvailable not present */
			used = mem_total - mem_free - buffers - cached;
		}
	}

	/* Check if values changed */
	if (used != data->used_kb || mem_total != data->total_kb) {
		data->used_kb  = used;
		data->total_kb = mem_total;

		unsigned long free_kb = mem_total - used;
		const char *mode = cfg->ram_mode ? cfg->ram_mode : "used_total";

		if (strcmp(mode, "percentage") == 0) {
			int percent = (int)((used * 100) / mem_total);
			const char *fmt = cfg->ram_unit_space ? "%d %%" : "%d%%";
			snprintf(data->display_str, sizeof(data->display_str),
			         fmt, percent);
		} else if (strcmp(mode, "used") == 0) {
			format_size(data->display_str, sizeof(data->display_str),
			            used, cfg->ram_decimals, cfg->ram_unit_space);
		} else if (strcmp(mode, "free") == 0) {
			format_size(data->display_str, sizeof(data->display_str),
			            free_kb, cfg->ram_decimals, cfg->ram_unit_space);
		} else {
			/* "used_total" (default) */
			char used_str[16];
			char total_str[16];
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
		cairo_set_source_rgba(cr, 0.8, 1, 0.8, 0.9);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

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

	mod->name            = "ram";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = ram_init;
	mod->destroy         = ram_destroy;
	mod->update          = ram_update;
	mod->render          = ram_render;
	mod->data            = data;
	mod->width           = 80;
	mod->dirty           = true;

	return mod;
}
