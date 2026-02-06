/*
 * StatusNotifierHost implementation
 *
 * Registers as a host and manages the list of status notifier items.
 * Fetches icons and properties from registered apps.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>  /* For ntohl */

#include "barny.h"

#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"
#define SNI_ITEM_INTERFACE "org.kde.StatusNotifierItem"

typedef struct {
	barny_state_t *state;
	char *host_name;
	sni_item_t *items;
	sd_bus_slot *watcher_slot;
} sni_host_t;

static sni_host_t *host = NULL;

/*
 * Parse a service string like ":1.234/StatusNotifierItem" into
 * service name and object path components.
 */
static void
parse_service_string(const char *full, char **service, char **path)
{
	*service = NULL;
	*path = NULL;

	const char *slash = strchr(full, '/');
	if (slash) {
		*service = strndup(full, slash - full);
		*path = strdup(slash);
	} else {
		*service = strdup(full);
		*path = strdup("/StatusNotifierItem");
	}
}

/*
 * Convert IconPixmap data (big-endian ARGB) to Cairo surface
 * Format from D-Bus: a(iiay) - array of (width, height, pixel_data)
 */
static cairo_surface_t *
create_icon_from_pixmap(sd_bus_message *m, int target_size)
{
	int r;
	cairo_surface_t *best = NULL;
	int best_size = 0;

	r = sd_bus_message_enter_container(m, 'a', "(iiay)");
	if (r < 0) {
		return NULL;
	}

	while (sd_bus_message_enter_container(m, 'r', "iiay") > 0) {
		int32_t width, height;
		const void *pixels;
		size_t pixel_len;

		r = sd_bus_message_read(m, "ii", &width, &height);
		if (r < 0) {
			sd_bus_message_exit_container(m);
			continue;
		}

		/* Validate dimensions - reject bogus data from misbehaving apps */
		if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
			sd_bus_message_exit_container(m);
			continue;
		}

		/* Check for integer overflow in pixel count */
		size_t expected = (size_t)width * (size_t)height * 4;

		r = sd_bus_message_read_array(m, 'y', &pixels, &pixel_len);
		if (r < 0 || pixel_len != expected) {
			sd_bus_message_exit_container(m);
			continue;
		}

		/* Pick the icon closest to target size */
		int size = (width > height) ? width : height;
		if (!best || (size >= target_size && size < best_size) ||
		    (best_size < target_size && size > best_size)) {
			if (best) {
				cairo_surface_destroy(best);
			}

			/* Create Cairo surface and convert from network byte order ARGB */
			best = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
			if (cairo_surface_status(best) != CAIRO_STATUS_SUCCESS) {
				cairo_surface_destroy(best);
				best = NULL;
				sd_bus_message_exit_container(m);
				continue;
			}

			uint32_t *dst = (uint32_t *)cairo_image_surface_get_data(best);
			const uint32_t *src = pixels;

			for (int i = 0; i < width * height; i++) {
				/* Convert from network (big-endian) ARGB to native ARGB */
				dst[i] = ntohl(src[i]);
			}

			cairo_surface_mark_dirty(best);
			best_size = size;
		}

		sd_bus_message_exit_container(m);
	}

	sd_bus_message_exit_container(m);

	/* Scale if needed */
	if (best && best_size != target_size) {
		int w = cairo_image_surface_get_width(best);
		int h = cairo_image_surface_get_height(best);

		if (w <= 0 || h <= 0 || best_size <= 0) {
			cairo_surface_destroy(best);
			return NULL;
		}

		double scale = (double)target_size / best_size;
		int new_w = (int)(w * scale);
		int new_h = (int)(h * scale);

		if (new_w <= 0 || new_h <= 0) {
			cairo_surface_destroy(best);
			return NULL;
		}

		cairo_surface_t *scaled = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, new_w, new_h);
		if (cairo_surface_status(scaled) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(scaled);
			cairo_surface_destroy(best);
			return NULL;
		}

		cairo_t *cr = cairo_create(scaled);
		cairo_scale(cr, scale, scale);
		cairo_set_source_surface(cr, best, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_destroy(best);
		best = scaled;
	}

	return best;
}

/*
 * Create a placeholder icon with the first letter of the app
 */
static cairo_surface_t *
create_placeholder_icon(const char *id, int size)
{
	if (size <= 0 || size > 256)
		size = 24;

	cairo_surface_t *surface = cairo_image_surface_create(
	        CAIRO_FORMAT_ARGB32, size, size);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return NULL;
	}
	cairo_t *cr = cairo_create(surface);

	/* Draw colored circle based on id hash */
	unsigned int hash = 0;
	if (id) {
		for (const char *p = id; *p; p++) {
			hash = hash * 31 + *p;
		}
	}

	double r = ((hash >> 16) & 0xFF) / 255.0 * 0.5 + 0.3;
	double g = ((hash >> 8) & 0xFF) / 255.0 * 0.5 + 0.3;
	double b = (hash & 0xFF) / 255.0 * 0.5 + 0.3;

	cairo_arc(cr, size / 2.0, size / 2.0, size / 2.0 - 2, 0, 2 * 3.14159);
	cairo_set_source_rgba(cr, r, g, b, 0.8);
	cairo_fill(cr);

	/* Draw first letter */
	if (id && id[0]) {
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, size * 0.5);

		char letter[2] = { id[0], '\0' };
		if (letter[0] >= 'a' && letter[0] <= 'z') {
			letter[0] -= 32;  /* Uppercase */
		}

		cairo_text_extents_t ext;
		cairo_text_extents(cr, letter, &ext);
		cairo_move_to(cr, (size - ext.width) / 2 - ext.x_bearing,
		              (size - ext.height) / 2 - ext.y_bearing);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
		cairo_show_text(cr, letter);
	}

	cairo_destroy(cr);
	return surface;
}

/*
 * Fetch icon for an SNI item
 */
static void
fetch_item_icon(sni_item_t *item, int icon_size)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int r;

	if (!host || !host->state->dbus || !item->service) {
		return;
	}

	/* First try IconPixmap */
	r = sd_bus_get_property(
	        host->state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "IconPixmap",
	        &error,
	        &reply,
	        "a(iiay)");

	if (r >= 0) {
		item->icon = create_icon_from_pixmap(reply, icon_size);
		sd_bus_message_unref(reply);
		if (item->icon) {
			sd_bus_error_free(&error);
			return;
		}
	}
	sd_bus_error_free(&error);
	sd_bus_message_unref(reply);
	reply = NULL;

	/* Fallback to IconName (would need icon theme lookup - skip for now) */

	/* Create placeholder */
	item->icon = create_placeholder_icon(item->id, icon_size);
}

/*
 * Fetch all properties for an SNI item
 */
static void
fetch_item_properties(sni_item_t *item, int icon_size)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	char *str = NULL;
	int r;

	if (!host || !host->state->dbus || !item->service) {
		return;
	}

	/* Get Id */
	r = sd_bus_get_property_string(
	        host->state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "Id",
	        &error,
	        &str);
	if (r >= 0 && str) {
		free(item->id);
		item->id = str;
		str = NULL;
	}
	sd_bus_error_free(&error);

	/* Get Title */
	r = sd_bus_get_property_string(
	        host->state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "Title",
	        &error,
	        &str);
	if (r >= 0 && str) {
		free(item->title);
		item->title = str;
		str = NULL;
	}
	sd_bus_error_free(&error);

	/* Get Status */
	r = sd_bus_get_property_string(
	        host->state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "Status",
	        &error,
	        &str);
	if (r >= 0 && str) {
		free(item->status);
		item->status = str;
		str = NULL;
	}
	sd_bus_error_free(&error);

	/* Get IconName */
	r = sd_bus_get_property_string(
	        host->state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "IconName",
	        &error,
	        &str);
	if (r >= 0 && str) {
		free(item->icon_name);
		item->icon_name = str;
		str = NULL;
	}
	sd_bus_error_free(&error);

	/* Fetch icon */
	fetch_item_icon(item, icon_size);
}

/*
 * Add a new SNI item
 */
static sni_item_t *
add_item(const char *service_string, int icon_size)
{
	if (!host || !service_string || !service_string[0]) {
		return NULL;
	}

	/* Check for duplicates */
	for (sni_item_t *item = host->items; item; item = item->next) {
		if (item->service && strcmp(item->service, service_string) == 0) {
			return item;
		}
	}

	sni_item_t *item = calloc(1, sizeof(sni_item_t));
	if (!item) {
		return NULL;
	}

	parse_service_string(service_string, &item->service, &item->object_path);
	if (!item->service || !item->object_path) {
		free(item->service);
		free(item->object_path);
		free(item);
		return NULL;
	}

	/* Fetch properties */
	fetch_item_properties(item, icon_size);

	/* Add to list */
	item->next = host->items;
	host->items = item;

	printf("barny: SNI host added item: %s (%s)\n",
	       item->id ? item->id : "unknown",
	       item->service);

	return item;
}

/*
 * Remove an SNI item
 */
static void
remove_item(const char *service)
{
	if (!host) {
		return;
	}

	sni_item_t **pp = &host->items;
	while (*pp) {
		sni_item_t *item = *pp;
		/* Match against service name (without path) */
		char *item_service, *item_path;
		parse_service_string(service, &item_service, &item_path);
		free(item_path);

		bool match = (strcmp(item->service, service) == 0 ||
		              strcmp(item->service, item_service) == 0);
		free(item_service);

		if (match) {
			*pp = item->next;
			printf("barny: SNI host removed item: %s\n",
			       item->id ? item->id : item->service);

			free(item->service);
			free(item->object_path);
			free(item->id);
			free(item->title);
			free(item->status);
			free(item->icon_name);
			if (item->icon) {
				cairo_surface_destroy(item->icon);
			}
			free(item);
			return;
		}
		pp = &item->next;
	}
}

/*
 * Handle NewIcon signal from an item
 */
static int
handle_new_icon(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
	(void)userdata;
	(void)error;

	if (!host || !host->state) {
		return 0;
	}

	const char *sender = sd_bus_message_get_sender(m);
	if (!sender)
		return 0;

	int icon_size = host->state->config.tray_icon_size;

	/* Find the item and refresh its icon */
	for (sni_item_t *item = host->items; item; item = item->next) {
		if (item->service && strstr(item->service, sender)) {
			if (item->icon) {
				cairo_surface_destroy(item->icon);
				item->icon = NULL;
			}
			fetch_item_icon(item, icon_size);

			/* Invalidate the tray module so the new icon gets rendered */
			for (int i = 0; i < host->state->module_count; i++) {
				barny_module_t *mod = host->state->modules[i];
				if (mod && mod->name && strcmp(mod->name, "tray") == 0) {
					mod->dirty = true;
					break;
				}
			}
			break;
		}
	}

	return 0;
}

/*
 * Handle StatusNotifierItemRegistered signal
 */
static int
handle_item_registered(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
	(void)userdata;
	(void)error;

	const char *service;
	int r = sd_bus_message_read(m, "s", &service);
	if (r < 0) {
		return r;
	}

	if (host && host->state) {
		add_item(service, host->state->config.tray_icon_size);
	}

	return 0;
}

/*
 * Handle StatusNotifierItemUnregistered signal
 */
static int
handle_item_unregistered(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
	(void)userdata;
	(void)error;

	const char *service;
	int r = sd_bus_message_read(m, "s", &service);
	if (r < 0) {
		return r;
	}

	remove_item(service);

	return 0;
}

/*
 * Fetch existing items from the watcher
 */
static void
fetch_existing_items(int icon_size)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	int r;

	if (!host || !host->state->dbus) {
		return;
	}

	r = sd_bus_get_property(
	        host->state->dbus,
	        SNI_WATCHER_INTERFACE,
	        SNI_WATCHER_PATH,
	        SNI_WATCHER_INTERFACE,
	        "RegisteredStatusNotifierItems",
	        &error,
	        &reply,
	        "as");

	if (r < 0) {
		sd_bus_error_free(&error);
		return;
	}

	r = sd_bus_message_enter_container(reply, 'a', "s");
	if (r < 0) {
		sd_bus_message_unref(reply);
		sd_bus_error_free(&error);
		return;
	}

	const char *service;
	while (sd_bus_message_read(reply, "s", &service) > 0) {
		add_item(service, icon_size);
	}

	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
}

int
barny_sni_host_init(barny_state_t *state)
{
	int r;

	if (!state->dbus) {
		return -1;
	}

	host = calloc(1, sizeof(sni_host_t));
	if (!host) {
		return -1;
	}
	host->state = state;

	/* Create unique host name */
	if (asprintf(&host->host_name, "org.kde.StatusNotifierHost-%d", getpid()) < 0) {
		free(host);
		host = NULL;
		return -1;
	}

	/* Request our host name */
	r = sd_bus_request_name(state->dbus, host->host_name, 0);
	if (r < 0) {
		fprintf(stderr, "barny: failed to request host name %s: %s\n",
		        host->host_name, strerror(-r));
	} else {
		printf("barny: registered as %s\n", host->host_name);
	}

	/* Register with the watcher */
	sd_bus_error error = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(
	        state->dbus,
	        SNI_WATCHER_INTERFACE,
	        SNI_WATCHER_PATH,
	        SNI_WATCHER_INTERFACE,
	        "RegisterStatusNotifierHost",
	        &error,
	        NULL,
	        "s", host->host_name);

	if (r < 0) {
		fprintf(stderr, "barny: failed to register as SNI host: %s\n",
		        error.message ? error.message : strerror(-r));
	}
	sd_bus_error_free(&error);

	/* Subscribe to item registered/unregistered signals */
	r = sd_bus_match_signal(
	        state->dbus,
	        &host->watcher_slot,
	        SNI_WATCHER_INTERFACE,
	        SNI_WATCHER_PATH,
	        SNI_WATCHER_INTERFACE,
	        "StatusNotifierItemRegistered",
	        handle_item_registered,
	        NULL);
	if (r < 0) {
		fprintf(stderr, "barny: failed to subscribe to item registered: %s\n",
		        strerror(-r));
	}

	r = sd_bus_match_signal(
	        state->dbus,
	        NULL,
	        SNI_WATCHER_INTERFACE,
	        SNI_WATCHER_PATH,
	        SNI_WATCHER_INTERFACE,
	        "StatusNotifierItemUnregistered",
	        handle_item_unregistered,
	        NULL);
	if (r < 0) {
		fprintf(stderr, "barny: failed to subscribe to item unregistered: %s\n",
		        strerror(-r));
	}

	/* Subscribe to NewIcon signals from all items */
	r = sd_bus_match_signal(
	        state->dbus,
	        NULL,
	        NULL,  /* Any sender */
	        NULL,  /* Any path */
	        SNI_ITEM_INTERFACE,
	        "NewIcon",
	        handle_new_icon,
	        NULL);
	if (r < 0) {
		fprintf(stderr, "barny: failed to subscribe to NewIcon: %s\n",
		        strerror(-r));
	}

	/* Fetch existing items */
	fetch_existing_items(state->config.tray_icon_size);

	return 0;
}

void
barny_sni_host_cleanup(barny_state_t *state)
{
	if (!host) {
		return;
	}

	/* Free all items */
	sni_item_t *item = host->items;
	while (item) {
		sni_item_t *next = item->next;
		free(item->service);
		free(item->object_path);
		free(item->id);
		free(item->title);
		free(item->status);
		free(item->icon_name);
		if (item->icon) {
			cairo_surface_destroy(item->icon);
		}
		free(item);
		item = next;
	}

	if (host->watcher_slot) {
		sd_bus_slot_unref(host->watcher_slot);
	}

	if (state->dbus && host->host_name) {
		sd_bus_release_name(state->dbus, host->host_name);
	}

	free(host->host_name);
	free(host);
	host = NULL;
}

sni_item_t *
barny_sni_host_get_items(barny_state_t *state)
{
	(void)state;
	return host ? host->items : NULL;
}

void
barny_sni_item_activate(barny_state_t *state, sni_item_t *item, int x, int y)
{
	if (!state->dbus || !item || !item->service) {
		return;
	}

	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(
	        state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "Activate",
	        &error,
	        NULL,
	        "ii", x, y);

	if (r < 0) {
		/* Some apps don't implement Activate, try ContextMenu instead */
		sd_bus_error_free(&error);
	}
	sd_bus_error_free(&error);
}

void
barny_sni_item_secondary_activate(barny_state_t *state, sni_item_t *item, int x, int y)
{
	if (!state->dbus || !item || !item->service) {
		return;
	}

	sd_bus_error error = SD_BUS_ERROR_NULL;

	/* Try ContextMenu first - most apps implement this for right-click */
	int r = sd_bus_call_method(
	        state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "ContextMenu",
	        &error,
	        NULL,
	        "ii", x, y);

	if (r >= 0) {
		sd_bus_error_free(&error);
		return;
	}

	/* Fall back to SecondaryActivate */
	sd_bus_error_free(&error);
	error = SD_BUS_ERROR_NULL;

	sd_bus_call_method(
	        state->dbus,
	        item->service,
	        item->object_path,
	        SNI_ITEM_INTERFACE,
	        "SecondaryActivate",
	        &error,
	        NULL,
	        "ii", x, y);

	sd_bus_error_free(&error);
}
