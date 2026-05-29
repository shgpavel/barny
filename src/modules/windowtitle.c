#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[512];
	char                  raw_title[512];
	PangoFontDescription *font_desc;
} windowtitle_data_t;

static const char *
find_focused_title(cJSON *node)
{
	if (!node || !cJSON_IsObject(node))
		return NULL;

	cJSON *focused = cJSON_GetObjectItem(node, "focused");
	cJSON *type    = cJSON_GetObjectItem(node, "type");
	if (focused && cJSON_IsTrue(focused)) {
		const char *t = type ? type->valuestring : NULL;
		if (!t || (strcmp(t, "con") == 0 || strcmp(t, "floating_con") == 0)) {
			cJSON *name = cJSON_GetObjectItem(node, "name");
			if (name && cJSON_IsString(name))
				return name->valuestring;
		}
	}

	const char *arr_keys[] = { "nodes", "floating_nodes" };
	for (int k = 0; k < 2; k++) {
		cJSON *arr = cJSON_GetObjectItem(node, arr_keys[k]);
		if (!arr || !cJSON_IsArray(arr))
			continue;

		cJSON *child;
		cJSON_ArrayForEach(child, arr)
		{
			const char *title = find_focused_title(child);
			if (title)
				return title;
		}
	}

	return NULL;
}

static void
truncate_with_ellipsis(char *dst, size_t dst_size, const char *src, int max_len)
{
	if (max_len <= 0 || !src) {
		snprintf(dst, dst_size, "%s", src ? src : "");
		return;
	}

	/* Count UTF-8 characters */
	int   char_count = 0;
	const char *p    = src;
	while (*p) {
		if ((*p & 0xC0) != 0x80)
			char_count++;
		p++;
	}

	if (char_count <= max_len) {
		snprintf(dst, dst_size, "%s", src);
		return;
	}

	/* Find byte offset of max_len-th character */
	int   chars     = 0;
	size_t byte_off = 0;
	while (src[byte_off] && chars < max_len) {
		if ((src[byte_off] & 0xC0) != 0x80)
			chars++;
		if (chars > max_len)
			break;
		byte_off++;
	}
	/* Walk forward to include any continuation bytes of the last char */
	while (src[byte_off] && (src[byte_off] & 0xC0) == 0x80)
		byte_off++;

	if (byte_off >= dst_size)
		byte_off = dst_size - 4;

	memcpy(dst, src, byte_off);
	dst[byte_off]     = '\0';
	strncat(dst, "...", dst_size - strlen(dst) - 1);
}

static void
build_display_string(windowtitle_data_t *data)
{
	barny_config_t *cfg = &data->state->config;

	if (data->raw_title[0] == '\0') {
		const char *empty = cfg->windowtitle_empty_text;
		snprintf(data->display_str, sizeof(data->display_str), "%s",
		         empty ? empty : "");
		return;
	}

	int max_len = cfg->windowtitle_max_length;
	truncate_with_ellipsis(data->display_str, sizeof(data->display_str),
	                       data->raw_title, max_len);
}

static int
windowtitle_init(barny_module_t *self, barny_state_t *state)
{
	windowtitle_data_t *data = self->data;
	data->state              = state;

	data->font_desc          = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 11");

	if (state->sway_ipc_fd >= 0) {
		barny_sway_ipc_send(state, 4, ""); /* GET_TREE */
		uint32_t type;
		char    *reply = barny_sway_ipc_recv_sync(state, &type, 500);
		if (reply) {
			cJSON *tree = cJSON_Parse(reply);
			if (tree) {
				const char *title = find_focused_title(tree);
				if (title) {
					strncpy(data->raw_title, title,
					        sizeof(data->raw_title) - 1);
					data->raw_title[sizeof(data->raw_title) - 1]
					        = '\0';
				}
				cJSON_Delete(tree);
			}
			free(reply);
		}

		barny_sway_ipc_subscribe(state, "[\"window\",\"workspace\"]");
	}

	build_display_string(data);
	return 0;
}

static void
windowtitle_destroy(barny_module_t *self)
{
	windowtitle_data_t *data = self->data;
	if (!data)
		return;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
windowtitle_update(barny_module_t *self)
{
	/* Updates come from IPC events in the main loop */
	(void)self;
}

static void
windowtitle_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	windowtitle_data_t *data = self->data;
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

	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, y + (h - th) / 2 + 1);
	pango_cairo_show_layout(cr, layout);

	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set)
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
		                      cfg->text_color_b, 1);
	else
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_windowtitle_create(void)
{
	barny_module_t     *mod  = calloc(1, sizeof(barny_module_t));
	windowtitle_data_t *data = calloc(1, sizeof(windowtitle_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name     = "windowtitle";
	mod->position = BARNY_POS_CENTER;
	mod->init     = windowtitle_init;
	mod->destroy  = windowtitle_destroy;
	mod->update   = windowtitle_update;
	mod->render   = windowtitle_render;
	mod->data     = data;
	mod->width    = 200;
	mod->dirty    = true;

	return mod;
}

void
barny_windowtitle_refresh(barny_module_t *mod)
{
	if (!mod || strcmp(mod->name, "windowtitle") != 0)
		return;

	windowtitle_data_t *data = mod->data;
	if (data->state->sway_ipc_fd < 0)
		return;

	barny_sway_ipc_send(data->state, 4, ""); /* GET_TREE */
	uint32_t type;
	char    *reply = barny_sway_ipc_recv_sync(data->state, &type, 50);
	if (!reply)
		return;

	cJSON *tree = cJSON_Parse(reply);
	if (tree) {
		const char *title = find_focused_title(tree);
		const char *new_title = title ? title : "";
		if (strcmp(new_title, data->raw_title) != 0) {
			strncpy(data->raw_title, new_title,
			        sizeof(data->raw_title) - 1);
			data->raw_title[sizeof(data->raw_title) - 1] = '\0';
			build_display_string(data);
			mod->dirty = true;
		}
		cJSON_Delete(tree);
	}
	free(reply);
}
