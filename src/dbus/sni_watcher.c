#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"

#define SNI_WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH      "/StatusNotifierWatcher"

typedef struct {
	barny_state_t *state;
	sd_bus_slot   *slot;
	char         **items;
	int            item_count;
	int            item_capacity;
	bool           host_registered;
} sni_watcher_t;

static sni_watcher_t *watcher = NULL;

static int
emit_item_registered(const char *service)
{
	if (!watcher || !watcher->state->dbus) {
		return -1;
	}

	return sd_bus_emit_signal(watcher->state->dbus, SNI_WATCHER_PATH,
	                          SNI_WATCHER_INTERFACE,
	                          "StatusNotifierItemRegistered", "s", service);
}

static int
emit_item_unregistered(const char *service)
{
	if (!watcher || !watcher->state->dbus) {
		return -1;
	}

	return sd_bus_emit_signal(watcher->state->dbus, SNI_WATCHER_PATH,
	                          SNI_WATCHER_INTERFACE,
	                          "StatusNotifierItemUnregistered", "s", service);
}

static void
add_item(const char *service)
{
	int    i;
	int    new_cap;
	char **new_items;

	if (!watcher) {
		return;
	}

	for (i = 0; i < watcher->item_count; i++) {
		if (strcmp(watcher->items[i], service) == 0) {
			return;
		}
	}

	if (watcher->item_count >= watcher->item_capacity) {
		new_cap   = watcher->item_capacity ? watcher->item_capacity * 2 : 8;
		new_items = (char **)realloc((void *)watcher->items,
		                             new_cap * sizeof(char *));
		if (!new_items) {
			return;
		}
		watcher->items         = new_items;
		watcher->item_capacity = new_cap;
	}

	watcher->items[watcher->item_count++] = strdup(service);
	printf("barny: SNI item registered: %s\n", service);

	emit_item_registered(service);
}

static void
remove_item(const char *service)
{
	int i;
	int j;

	if (!watcher) {
		return;
	}

	for (i = 0; i < watcher->item_count; i++) {
		if (strcmp(watcher->items[i], service) == 0) {
			free(watcher->items[i]);

			for (j = i; j < watcher->item_count - 1; j++) {
				watcher->items[j] = watcher->items[j + 1];
			}
			watcher->item_count--;
			printf("barny: SNI item unregistered: %s\n", service);
			emit_item_unregistered(service);
			return;
		}
	}
}

static int
method_register_item(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
	const char *service;
	int         r;
	const char *sender;
	char       *full_service;
	size_t      len;

	(void)userdata;
	(void)error;

	r = sd_bus_message_read(m, "s", &service);
	if (r < 0) {
		return r;
	}

	sender = sd_bus_message_get_sender(m);

	if (service[0] == '/') {
		len          = strlen(sender) + strlen(service) + 1;
		full_service = malloc(len);
		if (!full_service) {
			return -ENOMEM;
		}
		snprintf(full_service, len, "%s%s", sender, service);
	} else {
		full_service = strdup(service);
		if (!full_service) {
			return -ENOMEM;
		}
	}

	add_item(full_service);
	free(full_service);

	return sd_bus_reply_method_return(m, "");
}

static int
method_register_host(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
	const char *service;
	int         r;

	(void)userdata;
	(void)error;

	r = sd_bus_message_read(m, "s", &service);
	if (r < 0) {
		return r;
	}

	if (watcher) {
		watcher->host_registered = true;
		printf("barny: SNI host registered: %s\n", service);

		sd_bus_emit_signal(watcher->state->dbus,
		                   SNI_WATCHER_PATH,
		                   SNI_WATCHER_INTERFACE,
		                   "StatusNotifierHostRegistered",
		                   "");
	}

	return sd_bus_reply_method_return(m, "");
}

static int
property_get_items(sd_bus *bus, const char *path, const char *interface,
                   const char *property, sd_bus_message *reply, void *userdata,
                   sd_bus_error *error)
{
	int r;
	int i;

	(void)bus;
	(void)path;
	(void)interface;
	(void)property;
	(void)userdata;
	(void)error;

	r = sd_bus_message_open_container(reply, 'a', "s");
	if (r < 0) {
		return r;
	}

	if (watcher) {
		for (i = 0; i < watcher->item_count; i++) {
			r = sd_bus_message_append(reply, "s", watcher->items[i]);
			if (r < 0) {
				return r;
			}
		}
	}

	return sd_bus_message_close_container(reply);
}

static int
property_get_host_registered(sd_bus *bus, const char *path, const char *interface,
                             const char *property, sd_bus_message *reply,
                             void *userdata, sd_bus_error *error)
{
	(void)bus;
	(void)path;
	(void)interface;
	(void)property;
	(void)userdata;
	(void)error;

	return sd_bus_message_append(reply, "b",
	                             watcher ? watcher->host_registered : false);
}

static int
property_get_protocol_version(sd_bus *bus, const char *path, const char *interface,
                              const char *property, sd_bus_message *reply,
                              void *userdata, sd_bus_error *error)
{
	(void)bus;
	(void)path;
	(void)interface;
	(void)property;
	(void)userdata;
	(void)error;

	return sd_bus_message_append(reply, "i", 0);
}

static const sd_bus_vtable watcher_vtable[]
        = { SD_BUS_VTABLE_START(0),
	    SD_BUS_METHOD("RegisterStatusNotifierItem", "s", "",
	                  method_register_item, SD_BUS_VTABLE_UNPRIVILEGED),
	    SD_BUS_METHOD("RegisterStatusNotifierHost", "s", "",
	                  method_register_host, SD_BUS_VTABLE_UNPRIVILEGED),
	    SD_BUS_PROPERTY("RegisteredStatusNotifierItems", "as",
	                    property_get_items, 0,
	                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	    SD_BUS_PROPERTY("IsStatusNotifierHostRegistered", "b",
	                    property_get_host_registered, 0,
	                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	    SD_BUS_PROPERTY("ProtocolVersion", "i", property_get_protocol_version,
	                    0, SD_BUS_VTABLE_PROPERTY_CONST),
	    SD_BUS_SIGNAL("StatusNotifierItemRegistered", "s", 0),
	    SD_BUS_SIGNAL("StatusNotifierItemUnregistered", "s", 0),
	    SD_BUS_SIGNAL("StatusNotifierHostRegistered", "", 0),
	    SD_BUS_VTABLE_END };

static int
name_owner_changed(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
	const char *name, *old_owner, *new_owner;
	int         r;

	(void)userdata;
	(void)error;

	r = sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner);
	if (r < 0) {
		return r;
	}

	if (new_owner[0] == '\0') {
		remove_item(name);
	}

	return 0;
}

int
barny_sni_watcher_init(barny_state_t *state)
{
	int r;

	if (!state->dbus) {
		return -1;
	}

	watcher = calloc(1, sizeof(sni_watcher_t));
	if (!watcher) {
		return -1;
	}
	watcher->state = state;

	r              = sd_bus_add_object_vtable(state->dbus,
	                                          &watcher->slot,
	                                          SNI_WATCHER_PATH,
	                                          SNI_WATCHER_INTERFACE,
	                                          watcher_vtable,
	                                          NULL);
	if (r < 0) {
		fprintf(stderr, "barny: failed to add watcher vtable: %s\n",
		        strerror(-r));
		free(watcher);
		watcher = NULL;
		return -1;
	}

	r = sd_bus_request_name(state->dbus, SNI_WATCHER_INTERFACE, 0);
	if (r < 0) {
		fprintf(stderr,
		        "barny: failed to acquire %s: %s (another watcher may be running)\n",
		        SNI_WATCHER_INTERFACE, strerror(-r));

	} else {
		printf("barny: registered as %s\n", SNI_WATCHER_INTERFACE);
	}

	r = sd_bus_match_signal(state->dbus,
	                        NULL,
	                        "org.freedesktop.DBus",
	                        "/org/freedesktop/DBus",
	                        "org.freedesktop.DBus",
	                        "NameOwnerChanged",
	                        name_owner_changed,
	                        NULL);
	if (r < 0) {
		fprintf(stderr, "barny: failed to add name owner match: %s\n",
		        strerror(-r));
	}

	return 0;
}

void
barny_sni_watcher_set_host_registered(bool registered)
{
	if (watcher) {
		watcher->host_registered = registered;

		if (registered) {
			sd_bus_emit_signal(watcher->state->dbus,
			                   SNI_WATCHER_PATH,
			                   SNI_WATCHER_INTERFACE,
			                   "StatusNotifierHostRegistered",
			                   "");
		}
	}
}

void
barny_sni_watcher_cleanup(barny_state_t *state)
{
	int i;

	if (!watcher) {
		return;
	}

	if (state->dbus) {
		sd_bus_release_name(state->dbus, SNI_WATCHER_INTERFACE);
	}

	if (watcher->slot) {
		sd_bus_slot_unref(watcher->slot);
	}

	for (i = 0; i < watcher->item_count; i++) {
		free(watcher->items[i]);
	}
	free((void *)watcher->items);
	free(watcher);
	watcher = NULL;
}
