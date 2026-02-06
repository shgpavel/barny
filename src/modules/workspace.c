#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <cjson/cJSON.h>

#include "barny.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_WORKSPACES 10

typedef struct {
	int   num;
	char *name;
	bool  focused;
	bool  visible;
	bool  urgent;
} workspace_info_t;

typedef struct {
	barny_state_t        *state;
	workspace_info_t      workspaces[MAX_WORKSPACES];
	int                   workspace_count;
	PangoFontDescription *font_desc;
	int                   render_x;
} workspace_data_t;

static void
parse_workspaces(workspace_data_t *data, const char *json_str)
{
	/* Clear existing */
	for (int i = 0; i < data->workspace_count; i++) {
		free(data->workspaces[i].name);
		data->workspaces[i].name = NULL;
	}
	data->workspace_count = 0;

	cJSON *json           = cJSON_Parse(json_str);
	if (!json)
		return;

	cJSON *ws;
	cJSON_ArrayForEach(ws, json)
	{
		if (data->workspace_count >= MAX_WORKSPACES)
			break;

		workspace_info_t *info = &data->workspaces[data->workspace_count];

		cJSON            *num  = cJSON_GetObjectItem(ws, "num");
		cJSON            *name = cJSON_GetObjectItem(ws, "name");
		cJSON            *focused = cJSON_GetObjectItem(ws, "focused");
		cJSON            *visible = cJSON_GetObjectItem(ws, "visible");
		cJSON            *urgent  = cJSON_GetObjectItem(ws, "urgent");

		info->num                 = num ? num->valueint : 0;
		info->name    = name ? strdup(name->valuestring) : strdup("?");
		info->focused = focused ? cJSON_IsTrue(focused) : false;
		info->visible = visible ? cJSON_IsTrue(visible) : false;
		info->urgent  = urgent ? cJSON_IsTrue(urgent) : false;

		data->workspace_count++;
	}

	cJSON_Delete(json);
}

static int
workspace_init(barny_module_t *self, barny_state_t *state)
{
	workspace_data_t *data = self->data;
	data->state            = state;

	/* Create font description */
	data->font_desc        = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans Bold 10");

	/* Get initial workspace list (blocking) */
	if (state->sway_ipc_fd >= 0) {
		barny_sway_ipc_send(state, 1, ""); /* GET_WORKSPACES */
		uint32_t type;
		char    *reply = barny_sway_ipc_recv_sync(state, &type, 500);
		if (reply) {
			parse_workspaces(data, reply);
			free(reply);
		}

		/* Subscribe to workspace events */
		barny_sway_ipc_subscribe(state, "[\"workspace\"]");
	}

	return 0;
}

static void
workspace_destroy(barny_module_t *self)
{
	workspace_data_t *data = self->data;
	if (!data)
		return;

	for (int i = 0; i < data->workspace_count; i++) {
		free(data->workspaces[i].name);
	}

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
workspace_update(barny_module_t *self)
{
	/* Workspace updates come from IPC events in the main loop */
	(void)self;
}

/* Helper to get display label for a workspace */
static const char *
get_workspace_label(workspace_data_t *data, workspace_info_t *ws, char *buf, size_t buf_size)
{
	barny_config_t *config = &data->state->config;

	/* If we have configured names and workspace number is within range */
	if (config->workspace_names && ws->num >= 1 && ws->num <= config->workspace_name_count) {
		return config->workspace_names[ws->num - 1];
	}

	/* Fallback to workspace number */
	snprintf(buf, buf_size, "%d", ws->num);
	return buf;
}

/* Helper to check if shape is square */
static bool
is_square_shape(barny_config_t *config)
{
	return config->workspace_shape && strcmp(config->workspace_shape, "square") == 0;
}

/* Helper to draw shape (circle or square) */
static void
draw_shape(cairo_t *cr, int cx, int cy, int size, bool square, int corner_radius, bool fill)
{
	int half = size / 2 - 2;
	if (half < 1)
		half = 1;

	if (square) {
		int left = cx - half;
		int top = cy - half;
		int w = half * 2;
		int h = half * 2;

		if (corner_radius > 0) {
			double r = corner_radius;
			if (r > w / 2.0) r = w / 2.0;
			if (r > h / 2.0) r = h / 2.0;

			cairo_new_path(cr);
			cairo_arc(cr, left + r, top + r, r, M_PI, 3 * M_PI / 2);
			cairo_arc(cr, left + w - r, top + r, r, 3 * M_PI / 2, 0);
			cairo_arc(cr, left + w - r, top + h - r, r, 0, M_PI / 2);
			cairo_arc(cr, left + r, top + h - r, r, M_PI / 2, M_PI);
			cairo_close_path(cr);
		} else {
			cairo_rectangle(cr, left, top, w, h);
		}
	} else {
		cairo_arc(cr, cx, cy, half, 0, 2 * M_PI);
	}

	if (fill)
		cairo_fill(cr);
	else
		cairo_stroke(cr);
}

static void
workspace_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	workspace_data_t *data = self->data;
	(void)w;

	data->render_x = x;

	int          indicator_size = data->state->config.workspace_indicator_size;
	int          spacing        = data->state->config.workspace_spacing;
	int          total_width    = 0;
	bool         square         = is_square_shape(&data->state->config);
	int          corner_radius  = square ? data->state->config.workspace_corner_radius : 0;

	PangoLayout *layout         = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);

	for (int i = 0; i < data->workspace_count; i++) {
		workspace_info_t *ws = &data->workspaces[i];
		int               cx = x + total_width + indicator_size / 2;
		int               cy = y + h / 2;

		/* Get workspace label (custom name or number) */
		char label_buf[16];
		const char *label = get_workspace_label(data, ws, label_buf, sizeof(label_buf));

		/* Determine colors and style based on workspace state */
		double bg_r, bg_g, bg_b, bg_a;
		double fg_r, fg_g, fg_b, fg_a;
		bool fill = true;

		if (ws->focused) {
			bg_r = bg_g = bg_b = 1.0; bg_a = 0.95;
			fg_r = fg_g = fg_b = 0.1; fg_a = 1.0;
		} else if (ws->urgent) {
			bg_r = 0.9; bg_g = 0.2; bg_b = 0.2; bg_a = 0.95;
			fg_r = fg_g = fg_b = 1.0; fg_a = 1.0;
		} else if (ws->visible) {
			bg_r = bg_g = bg_b = 1.0; bg_a = 0.75;
			fg_r = fg_g = fg_b = 1.0; fg_a = 0.9;
			fill = false;
			cairo_set_line_width(cr, 2);
		} else {
			bg_r = bg_g = bg_b = 1.0; bg_a = 0.5;
			fg_r = fg_g = fg_b = 0.2; fg_a = 0.9;
		}

		/* Draw shape */
		cairo_set_source_rgba(cr, bg_r, bg_g, bg_b, bg_a);
		draw_shape(cr, cx, cy, indicator_size, square, corner_radius, fill);

		/* Draw label */
		pango_layout_set_text(layout, label, -1);
		int tw, th;
		pango_layout_get_pixel_size(layout, &tw, &th);
		cairo_set_source_rgba(cr, fg_r, fg_g, fg_b, fg_a);
		cairo_move_to(cr, cx - tw / 2.0, cy - th / 2.0);
		pango_cairo_show_layout(cr, layout);

		total_width += indicator_size + spacing;
	}

	g_object_unref(layout);

	/* Update module width */
	self->width = total_width > 0 ? total_width - spacing : 0;
}

static void
workspace_click(barny_module_t *self, int button, int click_x, int click_y)
{
	workspace_data_t *data = self->data;
	(void)click_y;

	if (button != 272)
		return; /* BTN_LEFT */

	int indicator_size = data->state->config.workspace_indicator_size;
	int spacing        = data->state->config.workspace_spacing;

	/* Convert absolute click_x to position relative to module */
	int rel_x          = click_x - data->render_x;

	/* Find which workspace was clicked */
	int x              = 0;
	for (int i = 0; i < data->workspace_count; i++) {
		int cx   = x + indicator_size / 2;
		int dist = abs(rel_x - cx);

		if (dist < indicator_size / 2) {
			/* Switch to this workspace */
			char cmd[256];
			snprintf(cmd, sizeof(cmd), "workspace number %d",
			         data->workspaces[i].num);
			barny_sway_ipc_send(data->state, 0, cmd); /* RUN_COMMAND */

			uint32_t type;
			char    *reply = barny_sway_ipc_recv(data->state, &type);
			free(reply);
			break;
		}

		x += indicator_size + spacing;
	}
}

barny_module_t *
barny_module_workspace_create(void)
{
	barny_module_t   *mod  = calloc(1, sizeof(barny_module_t));
	workspace_data_t *data = calloc(1, sizeof(workspace_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name              = "workspace";
	mod->position          = BARNY_POS_LEFT;
	mod->init              = workspace_init;
	mod->destroy           = workspace_destroy;
	mod->update            = workspace_update;
	mod->render            = workspace_render;
	mod->on_click          = workspace_click;
	mod->data              = data;
	mod->width             = 200;
	mod->dirty             = true;

	return mod;
}

/* Called from main loop when workspace events arrive */
void
barny_workspace_refresh(barny_module_t *mod)
{
	if (!mod || strcmp(mod->name, "workspace") != 0)
		return;

	workspace_data_t *data = mod->data;
	if (data->state->sway_ipc_fd < 0)
		return;

	barny_sway_ipc_send(data->state, 1, ""); /* GET_WORKSPACES */
	uint32_t type;
	/* Very short timeout - Sway responds almost instantly */
	char    *reply = barny_sway_ipc_recv_sync(data->state, &type, 10);
	if (reply) {
		parse_workspaces(data, reply);
		free(reply);
		mod->dirty = true;
	}
}
