/*
 * System tray module
 *
 * Displays StatusNotifierItem icons from applications
 */

#include <stdlib.h>
#include <string.h>

#include "barny.h"

typedef struct {
	barny_state_t *state;
	int icon_size;
	int icon_spacing;
	int item_count;
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
	(void)self;
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
tray_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	tray_data_t *data = self->data;
	(void)w;

	sni_item_t *items = barny_sni_host_get_items(data->state);
	if (!items) {
		return;
	}

	int icon_x = x + 4;
	int icon_y = y + (h - data->icon_size) / 2;

	for (sni_item_t *item = items; item; item = item->next) {
		/* Skip items with no status or hidden status */
		if (item->status && strcmp(item->status, "Passive") != 0 &&
		    strcmp(item->status, "Active") != 0 &&
		    strcmp(item->status, "NeedsAttention") != 0) {
			continue;
		}

		if (item->icon) {
			/* Draw icon with slight transparency */
			cairo_save(cr);

			/* Draw subtle shadow */
			cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
			cairo_arc(cr, icon_x + data->icon_size / 2.0 + 1,
			          icon_y + data->icon_size / 2.0 + 1,
			          data->icon_size / 2.0 - 1, 0, 2 * 3.14159);
			cairo_fill(cr);

			/* Draw the icon */
			cairo_set_source_surface(cr, item->icon, icon_x, icon_y);
			cairo_paint_with_alpha(cr, 0.95);

			cairo_restore(cr);
		}

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

		if (x >= icon_x && x < icon_end) {
			/* Found the clicked icon */
			if (button == 1) {  /* Left click */
				barny_sni_item_activate(data->state, item, x, y);
			} else if (button == 3) {  /* Right click */
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
