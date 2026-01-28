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

	for (int i = 0; i < data->workspace_count; i++) {
		free(data->workspaces[i].name);
	}

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
workspace_update(barny_module_t *self)
{
	/* Workspace updates come from IPC events in the main loop */
	(void)self;
}

static void
workspace_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	workspace_data_t *data = self->data;
	(void)w;

	int          indicator_size = data->state->config.workspace_indicator_size;
	int          spacing        = data->state->config.workspace_spacing;
	int          total_width    = 0;

	PangoLayout *layout         = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->font_desc);

	for (int i = 0; i < data->workspace_count; i++) {
		workspace_info_t *ws = &data->workspaces[i];
		int               cx = x + total_width + indicator_size / 2;
		int               cy = y + h / 2;

		/* Draw indicator background */
		if (ws->focused) {
			/* Focused: bright filled circle */
			cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
			cairo_arc(cr, cx, cy, indicator_size / 2 - 2, 0, 2 * M_PI);
			cairo_fill(cr);

			/* Draw number in dark color */
			char num_str[8];
			snprintf(num_str, sizeof(num_str), "%d", ws->num);
			pango_layout_set_text(layout, num_str, -1);

			int tw, th;
			pango_layout_get_pixel_size(layout, &tw, &th);

			cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1);
			cairo_move_to(cr, cx - tw / 2, cy - th / 2);
			pango_cairo_show_layout(cr, layout);
		} else if (ws->visible) {
			/* Visible but not focused: outlined circle */
			cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
			cairo_set_line_width(cr, 2);
			cairo_arc(cr, cx, cy, indicator_size / 2 - 2, 0, 2 * M_PI);
			cairo_stroke(cr);

			/* Draw number */
			char num_str[8];
			snprintf(num_str, sizeof(num_str), "%d", ws->num);
			pango_layout_set_text(layout, num_str, -1);

			int tw, th;
			pango_layout_get_pixel_size(layout, &tw, &th);

			cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
			cairo_move_to(cr, cx - tw / 2, cy - th / 2);
			pango_cairo_show_layout(cr, layout);
		} else if (ws->urgent) {
			/* Urgent: red filled circle */
			cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 0.9);
			cairo_arc(cr, cx, cy, indicator_size / 2 - 2, 0, 2 * M_PI);
			cairo_fill(cr);

			/* Draw number */
			char num_str[8];
			snprintf(num_str, sizeof(num_str), "%d", ws->num);
			pango_layout_set_text(layout, num_str, -1);

			int tw, th;
			pango_layout_get_pixel_size(layout, &tw, &th);

			cairo_set_source_rgba(cr, 1, 1, 1, 1);
			cairo_move_to(cr, cx - tw / 2, cy - th / 2);
			pango_cairo_show_layout(cr, layout);
		} else {
			/* Normal: small dot */
			cairo_set_source_rgba(cr, 1, 1, 1, 0.4);
			cairo_arc(cr, cx, cy, 4, 0, 2 * M_PI);
			cairo_fill(cr);
		}

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

	/* Find which workspace was clicked */
	int x              = 0;
	for (int i = 0; i < data->workspace_count; i++) {
		int cx   = x + indicator_size / 2;
		int dist = abs(click_x - cx);

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
