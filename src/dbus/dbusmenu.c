#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "barny.h"

#define SNI_ITEM_INTERFACE "org.kde.StatusNotifierItem"
#define DBUSMENU_INTERFACE "com.canonical.dbusmenu"

static char *
strip_mnemonics(const char *in)
{
	char  *out;
	size_t i, j;

	if (!in)
		return NULL;

	out = malloc(strlen(in) + 1);
	if (!out)
		return NULL;

	for (i = 0, j = 0; in[i]; i++) {
		if (in[i] == '_') {
			if (in[i + 1] == '_') {
				out[j++] = '_';
				i++;
			}
		} else {
			out[j++] = in[i];
		}
	}
	out[j] = '\0';
	return out;
}

char *
barny_sni_item_menu_path(barny_state_t *state, sni_item_t *item)
{
	sd_bus_error    error = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	const char     *path  = NULL;
	char           *dup   = NULL;
	int             r;

	if (!state || !state->dbus || !item || !item->service)
		return NULL;

	r = sd_bus_get_property(state->dbus, item->service, item->object_path,
	                        SNI_ITEM_INTERFACE, "Menu", &error, &reply, "o");
	if (r >= 0 && sd_bus_message_read(reply, "o", &path) >= 0 && path
	    && path[0] && strcmp(path, "/") != 0)
		dup = strdup(path);

	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	return dup;
}

bool
barny_sni_item_is_menu(barny_state_t *state, sni_item_t *item)
{
	sd_bus_error error    = SD_BUS_ERROR_NULL;
	int          is_menu  = 0;
	int          r;

	if (!state || !state->dbus || !item || !item->service)
		return false;

	r = sd_bus_get_property_trivial(state->dbus, item->service,
	                                item->object_path, SNI_ITEM_INTERFACE,
	                                "ItemIsMenu", &error, 'b', &is_menu);
	sd_bus_error_free(&error);
	return r >= 0 && is_menu;
}

static void
read_property(sd_bus_message *m, const char *key, barny_menu_item_t *item)
{
	const char *contents = NULL;
	char        type;
	int         r;

	r = sd_bus_message_peek_type(m, &type, &contents);
	if (r < 0 || type != 'v' || !contents) {
		sd_bus_message_skip(m, "v");
		return;
	}

	if (sd_bus_message_enter_container(m, 'v', contents) <= 0)
		return;

	if (strcmp(key, "label") == 0 && contents[0] == 's') {
		const char *s = NULL;
		if (sd_bus_message_read(m, "s", &s) >= 0) {
			free(item->label);
			item->label = strip_mnemonics(s);
		}
	} else if (strcmp(key, "enabled") == 0 && contents[0] == 'b') {
		int b = 1;
		sd_bus_message_read(m, "b", &b);
		item->enabled = b;
	} else if (strcmp(key, "visible") == 0 && contents[0] == 'b') {
		int b = 1;
		sd_bus_message_read(m, "b", &b);
		item->visible = b;
	} else if (strcmp(key, "type") == 0 && contents[0] == 's') {
		const char *s = NULL;
		if (sd_bus_message_read(m, "s", &s) >= 0 && s
		    && strcmp(s, "separator") == 0)
			item->separator = true;
	} else if (strcmp(key, "children-display") == 0 && contents[0] == 's') {
		const char *s = NULL;
		if (sd_bus_message_read(m, "s", &s) >= 0 && s
		    && strcmp(s, "submenu") == 0)
			item->has_submenu = true;
	} else if (strcmp(key, "toggle-state") == 0 && contents[0] == 'i') {
		int v = -1;
		sd_bus_message_read(m, "i", &v);
		item->toggle_state = v;
	} else {
		sd_bus_message_skip(m, contents);
	}

	sd_bus_message_exit_container(m);
}

static int
parse_node(sd_bus_message *m, barny_menu_item_t *item)
{
	int r;

	item->enabled      = true;
	item->visible      = true;
	item->toggle_state = -1;

	r = sd_bus_message_enter_container(m, 'r', "ia{sv}av");
	if (r <= 0)
		return -1;

	if (sd_bus_message_read(m, "i", &item->id) < 0)
		goto fail;

	if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0)
		goto fail;
	while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
		const char *key = NULL;
		if (sd_bus_message_read(m, "s", &key) >= 0 && key)
			read_property(m, key, item);
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);

	if (sd_bus_message_enter_container(m, 'a', "v") < 0)
		goto fail;
	while (sd_bus_message_enter_container(m, 'v', "(ia{sv}av)") > 0) {
		barny_menu_item_t *grown;
		grown = realloc(item->children,
		                (item->child_count + 1) * sizeof(*grown));
		if (!grown) {
			sd_bus_message_exit_container(m);
			break;
		}
		item->children = grown;
		memset(&item->children[item->child_count], 0,
		       sizeof(*grown));
		if (parse_node(m, &item->children[item->child_count]) == 0)
			item->child_count++;
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);

	if (item->child_count > 0)
		item->has_submenu = true;

	sd_bus_message_exit_container(m);
	return 0;

fail:
	sd_bus_message_exit_container(m);
	return -1;
}

barny_menu_item_t *
barny_dbusmenu_get_layout(barny_state_t *state, const char *service,
                          const char *menu_path)
{
	sd_bus_error       error = SD_BUS_ERROR_NULL;
	sd_bus_message    *call  = NULL;
	sd_bus_message    *reply = NULL;
	barny_menu_item_t *root  = NULL;
	uint32_t           revision;
	int                r;

	if (!state || !state->dbus || !service || !menu_path)
		return NULL;

	r = sd_bus_message_new_method_call(state->dbus, &call, service, menu_path,
	                                   DBUSMENU_INTERFACE, "GetLayout");
	if (r < 0)
		goto out;

	sd_bus_message_append(call, "ii", 0, -1);
	sd_bus_message_open_container(call, 'a', "s");
	sd_bus_message_close_container(call);

	r = sd_bus_call(state->dbus, call, 0, &error, &reply);
	if (r < 0) {
		fprintf(stderr, "barny: dbusmenu GetLayout failed: %s\n",
		        error.message ? error.message : strerror(-r));
		goto out;
	}

	if (sd_bus_message_read(reply, "u", &revision) < 0)
		goto out;

	root = calloc(1, sizeof(*root));
	if (!root)
		goto out;

	if (parse_node(reply, root) < 0) {
		barny_dbusmenu_free(root);
		root = NULL;
	}

out:
	sd_bus_message_unref(call);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&error);
	return root;
}

static void
free_node_contents(barny_menu_item_t *n)
{
	int i;

	for (i = 0; i < n->child_count; i++)
		free_node_contents(&n->children[i]);

	free(n->children);
	free(n->label);
}

void
barny_dbusmenu_free(barny_menu_item_t *root)
{
	if (!root)
		return;

	free_node_contents(root);
	free(root);
}

void
barny_dbusmenu_about_to_show(barny_state_t *state, const char *service,
                             const char *menu_path, int id)
{
	sd_bus_error error = SD_BUS_ERROR_NULL;

	if (!state || !state->dbus || !service || !menu_path)
		return;

	sd_bus_call_method(state->dbus, service, menu_path, DBUSMENU_INTERFACE,
	                   "AboutToShow", &error, NULL, "i", id);
	sd_bus_error_free(&error);
}

void
barny_dbusmenu_event_clicked(barny_state_t *state, const char *service,
                             const char *menu_path, int id)
{
	sd_bus_error    error = SD_BUS_ERROR_NULL;
	sd_bus_message *msg   = NULL;
	struct timespec ts;
	uint32_t        now;
	int             r;

	if (!state || !state->dbus || !service || !menu_path)
		return;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		ts.tv_sec = 0;
	now = (uint32_t)ts.tv_sec;

	r = sd_bus_message_new_method_call(state->dbus, &msg, service, menu_path,
	                                   DBUSMENU_INTERFACE, "Event");
	if (r < 0)
		goto out;

	sd_bus_message_append(msg, "is", id, "clicked");
	sd_bus_message_append(msg, "v", "i", 0);
	sd_bus_message_append(msg, "u", now);

	sd_bus_call(state->dbus, msg, 0, &error, NULL);

out:
	sd_bus_message_unref(msg);
	sd_bus_error_free(&error);
}
