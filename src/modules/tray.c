/*
 * System tray module
 *
 * Displays StatusNotifierItem icons from applications
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
	barny_state_t *state;
	int icon_size;
	int icon_spacing;
	int item_count;
	int render_x;  /* X position where module was last rendered */
} tray_data_t;

static int
tray_init(barny_module_t *self, barny_state_t *state)
{
	tray_data_t *data = self->data;
	data->state = state;
	data->icon_size = state->config.tray_icon_size;
	data->icon_spacing = state->config.tray_icon_spacing;
	data->item_count = 0;

	return 0;
}

static void
tray_destroy(barny_module_t *self)
{
	tray_data_t *data = self->data;
	if (!data)
		return;

	free(data);
	self->data = NULL;
}

static void
tray_update(barny_module_t *self)
{
	tray_data_t *data = self->data;

	/* Count items and check for changes */
	int count = 0;
	sni_item_t *items = barny_sni_host_get_items(data->state);
	for (sni_item_t *item = items; item; item = item->next) {
		/* Only count active/passive items, not hidden */
		if (!item->status || strcmp(item->status, "Passive") == 0 ||
		    strcmp(item->status, "Active") == 0 ||
		    strcmp(item->status, "NeedsAttention") == 0) {
			count++;
		}
	}

	if (count != data->item_count) {
		data->item_count = count;
		self->dirty = true;
	}

	/* Update width based on item count */
	if (count > 0) {
		self->width = count * data->icon_size +
		              (count - 1) * data->icon_spacing + 8;
	} else {
		self->width = 0;
	}
}

static void
draw_icon_bg(cairo_t *cr, double cx, double cy, double size,
             bool square, int corner_radius,
             double r, double g, double b, double a)
{
	double half = size / 2.0 - 1;
	if (half < 1)
		half = 1;

	cairo_set_source_rgba(cr, r, g, b, a);

	if (square) {
		double left = cx - half;
		double top = cy - half;
		double w = half * 2;
		double h = half * 2;

		if (corner_radius > 0) {
			double cr_r = corner_radius;
			if (cr_r > w / 2.0) cr_r = w / 2.0;
			if (cr_r > h / 2.0) cr_r = h / 2.0;

			cairo_new_path(cr);
			cairo_arc(cr, left + cr_r, top + cr_r, cr_r,
			          M_PI, 3 * M_PI / 2);
			cairo_arc(cr, left + w - cr_r, top + cr_r, cr_r,
			          3 * M_PI / 2, 0);
			cairo_arc(cr, left + w - cr_r, top + h - cr_r, cr_r,
			          0, M_PI / 2);
			cairo_arc(cr, left + cr_r, top + h - cr_r, cr_r,
			          M_PI / 2, M_PI);
			cairo_close_path(cr);
		} else {
			cairo_rectangle(cr, left, top, w, h);
		}
		cairo_fill(cr);
	} else {
		cairo_arc(cr, cx, cy, half, 0, 2 * M_PI);
		cairo_fill(cr);
	}
}

static void
tray_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	tray_data_t *data = self->data;
	barny_config_t *cfg = &data->state->config;
	(void)w;

	/* Store render position for click handling */
	data->render_x = x;

	sni_item_t *items = barny_sni_host_get_items(data->state);
	if (!items) {
		return;
	}

	bool square = cfg->tray_icon_shape &&
	              strcmp(cfg->tray_icon_shape, "square") == 0;
	int corner_radius = cfg->tray_icon_corner_radius;

	int icon_x = x + 4;
	int icon_y = y + (h - data->icon_size) / 2;

	for (sni_item_t *item = items; item; item = item->next) {
		/* Skip items with no status or hidden status */
		if (item->status && strcmp(item->status, "Passive") != 0 &&
		    strcmp(item->status, "Active") != 0 &&
		    strcmp(item->status, "NeedsAttention") != 0) {
			continue;
		}

		double cx = icon_x + data->icon_size / 2.0;
		double cy = icon_y + data->icon_size / 2.0;

		cairo_save(cr);

		/* Draw background shape */
		draw_icon_bg(cr, cx + 1, cy + 1, data->icon_size,
		             square, corner_radius,
		             0, 0, 0, cfg->tray_icon_bg_opacity * 0.5);
		draw_icon_bg(cr, cx, cy, data->icon_size,
		             square, corner_radius,
		             cfg->tray_icon_bg_r,
		             cfg->tray_icon_bg_g,
		             cfg->tray_icon_bg_b,
		             cfg->tray_icon_bg_opacity);

		/* Draw the icon (or fallback) */
		if (item->icon &&
		    cairo_surface_status(item->icon) == CAIRO_STATUS_SUCCESS) {
			int iw = cairo_image_surface_get_width(item->icon);
			int ih = cairo_image_surface_get_height(item->icon);
			if (iw > 0 && ih > 0) {
				/* Scale icon to fit inside the background with padding */
				int pad = 4;
				int target = data->icon_size - pad * 2;
				if (target < 1) target = 1;
				double scale = (double)target / (iw > ih ? iw : ih);

				double dx = icon_x + pad + (target - iw * scale) / 2.0;
				double dy = icon_y + pad + (target - ih * scale) / 2.0;

				cairo_save(cr);
				cairo_translate(cr, dx, dy);
				cairo_scale(cr, scale, scale);
				cairo_set_source_surface(cr, item->icon, 0, 0);
				cairo_paint_with_alpha(cr, 0.95);
				cairo_restore(cr);
			}
		}

		cairo_restore(cr);

		icon_x += data->icon_size + data->icon_spacing;
	}
}

static void
tray_on_click(barny_module_t *self, int button, int x, int y)
{
	tray_data_t *data = self->data;

	sni_item_t *items = barny_sni_host_get_items(data->state);
	if (!items) {
		return;
	}

	/* Convert absolute x to position relative to module */
	int rel_x = x - data->render_x;

	/* Determine which icon was clicked */
	int icon_x = 4;  /* Starting offset */

	for (sni_item_t *item = items; item; item = item->next) {
		/* Skip non-visible items */
		if (item->status && strcmp(item->status, "Passive") != 0 &&
		    strcmp(item->status, "Active") != 0 &&
		    strcmp(item->status, "NeedsAttention") != 0) {
			continue;
		}

		int icon_end = icon_x + data->icon_size;

		if (rel_x >= icon_x && rel_x < icon_end) {
			/* Found the clicked icon */
			/* Button codes are evdev: BTN_LEFT=272, BTN_RIGHT=273 */
			if (button == 272) {  /* Left click */
				barny_sni_item_activate(data->state, item, x, y);
			} else if (button == 273) {  /* Right click */
				barny_sni_item_secondary_activate(data->state, item, x, y);
			}
			return;
		}

		icon_x = icon_end + data->icon_spacing;
	}
}

barny_module_t *
barny_module_tray_create(void)
{
	barny_module_t *mod = calloc(1, sizeof(barny_module_t));
	tray_data_t *data = calloc(1, sizeof(tray_data_t));

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name = "tray";
	mod->position = BARNY_POS_RIGHT;
	mod->init = tray_init;
	mod->destroy = tray_destroy;
	mod->update = tray_update;
	mod->render = tray_render;
	mod->on_click = tray_on_click;
	mod->data = data;
	mod->width = 0;  /* Dynamic based on items */
	mod->dirty = true;

	return mod;
}
