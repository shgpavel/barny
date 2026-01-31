#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[272];  /* title + content */
	char                  content[256];
	time_t                last_mtime;
	PangoFontDescription *font_desc;
} fileread_data_t;

static int
fileread_init(barny_module_t *self, barny_state_t *state)
{
	fileread_data_t *data = self->data;
	data->state           = state;
	data->last_mtime      = 0;

	data->font_desc       = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	data->display_str[0] = '\0';
	data->content[0]     = '\0';

	return 0;
}

static void
fileread_destroy(barny_module_t *self)
{
	fileread_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
fileread_update(barny_module_t *self)
{
	fileread_data_t *data = self->data;
	barny_config_t  *cfg  = &data->state->config;

	/* No file configured */
	if (!cfg->fileread_path || cfg->fileread_path[0] == '\0') {
		if (data->display_str[0] != '\0') {
			data->display_str[0] = '\0';
			self->dirty = true;
		}
		return;
	}

	/* Check mtime to avoid unnecessary reads */
	struct stat st;
	if (stat(cfg->fileread_path, &st) != 0) {
		/* File doesn't exist or can't be accessed */
		if (data->content[0] != '\0') {
			data->content[0]     = '\0';
			data->display_str[0] = '\0';
			self->dirty          = true;
		}
		return;
	}

	/* Only re-read if file has been modified */
	if (st.st_mtime == data->last_mtime)
		return;

	data->last_mtime = st.st_mtime;

	FILE *f = fopen(cfg->fileread_path, "r");
	if (!f)
		return;

	/* Read content up to max_chars */
	int max_chars = cfg->fileread_max_chars;
	if (max_chars <= 0)
		max_chars = 64;
	if (max_chars > 255)
		max_chars = 255;

	char buf[256];
	if (fgets(buf, max_chars + 1, f)) {
		/* Remove trailing newline */
		buf[strcspn(buf, "\n\r")] = '\0';

		/* Check if content changed */
		if (strcmp(buf, data->content) != 0) {
			strncpy(data->content, buf, sizeof(data->content) - 1);
			data->content[sizeof(data->content) - 1] = '\0';

			/* Build display string with optional title */
			if (cfg->fileread_title && cfg->fileread_title[0]) {
				snprintf(data->display_str, sizeof(data->display_str),
				         "%s: %s", cfg->fileread_title, data->content);
			} else {
				snprintf(data->display_str, sizeof(data->display_str),
				         "%s", data->content);
			}

			self->dirty = true;
		}
	}

	fclose(f);
}

static void
fileread_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	fileread_data_t *data = self->data;
	(void)w;

	if (data->display_str[0] == '\0') {
		self->width = 0;
		return;
	}

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
		cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_fileread_create(void)
{
	barny_module_t  *mod  = calloc(1, sizeof(barny_module_t));
	fileread_data_t *data = calloc(1, sizeof(fileread_data_t));

	mod->name             = "fileread";
	mod->position         = BARNY_POS_RIGHT;
	mod->init             = fileread_init;
	mod->destroy          = fileread_destroy;
	mod->update           = fileread_update;
	mod->render           = fileread_render;
	mod->data             = data;
	mod->width            = 100;
	mod->dirty            = true;

	return mod;
}
