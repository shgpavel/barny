#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include "barny.h"

typedef struct {
	barny_state_t        *state;
	char                  display_str[128];
	char                  current_iface[32];
	char                  current_ip[64];
	bool                  is_online;
	PangoFontDescription *font_desc;
} network_data_t;

static bool
is_interface_up(const char *iface)
{
	char path[256];
	snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface);

	FILE *f = fopen(path, "r");
	if (!f)
		return false;

	char state[32] = "";
	if (fgets(state, sizeof(state), f)) {
		state[strcspn(state, "\n")] = '\0';
	}
	fclose(f);

	return strcmp(state, "up") == 0;
}

static bool
is_physical_interface(const char *iface)
{
	/* Skip loopback and virtual interfaces */
	if (strcmp(iface, "lo") == 0)
		return false;
	if (strncmp(iface, "veth", 4) == 0)
		return false;
	if (strncmp(iface, "docker", 6) == 0)
		return false;
	if (strncmp(iface, "br-", 3) == 0)
		return false;
	if (strncmp(iface, "virbr", 5) == 0)
		return false;

	return true;
}

static bool
find_active_interface(char *iface, size_t iface_len)
{
	DIR *dir = opendir("/sys/class/net");
	if (!dir)
		return false;

	/* Priority order: ethernet first, then wifi */
	char eth_iface[32] = "";
	char wifi_iface[32] = "";

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.')
			continue;
		if (!is_physical_interface(ent->d_name))
			continue;
		if (!is_interface_up(ent->d_name))
			continue;

		size_t namelen = strlen(ent->d_name);
		if (namelen >= sizeof(eth_iface))
			continue;

		/* Categorize by name */
		if (strncmp(ent->d_name, "eth", 3) == 0 ||
		    strncmp(ent->d_name, "en", 2) == 0) {
			memcpy(eth_iface, ent->d_name, namelen + 1);
		} else if (strncmp(ent->d_name, "wlan", 4) == 0 ||
		           strncmp(ent->d_name, "wl", 2) == 0) {
			memcpy(wifi_iface, ent->d_name, namelen + 1);
		}
	}
	closedir(dir);

	/* Prefer ethernet over wifi */
	if (eth_iface[0]) {
		size_t len = strlen(eth_iface);
		if (len < iface_len) {
			memcpy(iface, eth_iface, len + 1);
			return true;
		}
	}
	if (wifi_iface[0]) {
		size_t len = strlen(wifi_iface);
		if (len < iface_len) {
			memcpy(iface, wifi_iface, len + 1);
			return true;
		}
	}

	return false;
}

static bool
get_interface_ip(const char *iface, char *ip, size_t ip_len, bool prefer_ipv4)
{
	struct ifaddrs *ifaddr, *ifa;

	if (getifaddrs(&ifaddr) == -1)
		return false;

	char ipv4[INET_ADDRSTRLEN] = "";
	char ipv6[INET6_ADDRSTRLEN] = "";

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		if (strcmp(ifa->ifa_name, iface) != 0)
			continue;

		int family = ifa->ifa_addr->sa_family;

		if (family == AF_INET) {
			struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
			inet_ntop(AF_INET, &addr->sin_addr, ipv4, sizeof(ipv4));
		} else if (family == AF_INET6) {
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
			/* Skip link-local addresses */
			if (!IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) {
				inet_ntop(AF_INET6, &addr->sin6_addr, ipv6, sizeof(ipv6));
			}
		}
	}

	freeifaddrs(ifaddr);

	/* Return based on preference */
	if (prefer_ipv4) {
		if (ipv4[0]) {
			strncpy(ip, ipv4, ip_len - 1);
			ip[ip_len - 1] = '\0';
			return true;
		}
		if (ipv6[0]) {
			strncpy(ip, ipv6, ip_len - 1);
			ip[ip_len - 1] = '\0';
			return true;
		}
	} else {
		if (ipv6[0]) {
			strncpy(ip, ipv6, ip_len - 1);
			ip[ip_len - 1] = '\0';
			return true;
		}
		if (ipv4[0]) {
			strncpy(ip, ipv4, ip_len - 1);
			ip[ip_len - 1] = '\0';
			return true;
		}
	}

	return false;
}

static int
network_init(barny_module_t *self, barny_state_t *state)
{
	network_data_t *data = self->data;
	data->state          = state;
	data->is_online      = false;
	data->current_iface[0] = '\0';
	data->current_ip[0]    = '\0';

	data->font_desc      = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	strcpy(data->display_str, "offline");

	return 0;
}

static void
network_destroy(barny_module_t *self)
{
	network_data_t *data = self->data;
	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
}

static void
network_update(barny_module_t *self)
{
	network_data_t *data = self->data;
	barny_config_t *cfg  = &data->state->config;

	char iface[32] = "";
	char ip[64]    = "";
	bool online    = false;

	/* Determine which interface to use */
	const char *cfg_iface = cfg->network_interface;
	if (cfg_iface && cfg_iface[0] && strcmp(cfg_iface, "auto") != 0) {
		/* User-specified interface */
		strncpy(iface, cfg_iface, sizeof(iface) - 1);
		iface[sizeof(iface) - 1] = '\0';

		if (is_interface_up(iface)) {
			online = true;
		}
	} else {
		/* Auto-detect */
		if (find_active_interface(iface, sizeof(iface))) {
			online = true;
		}
	}

	/* Get IP address if online */
	if (online && cfg->network_show_ip) {
		get_interface_ip(iface, ip, sizeof(ip), cfg->network_prefer_ipv4);
	}

	/* Check if anything changed */
	bool changed = false;
	if (online != data->is_online) {
		changed = true;
	} else if (strcmp(iface, data->current_iface) != 0) {
		changed = true;
	} else if (strcmp(ip, data->current_ip) != 0) {
		changed = true;
	}

	if (!changed)
		return;

	data->is_online = online;
	size_t iface_len = strlen(iface);
	size_t ip_len = strlen(ip);
	if (iface_len < sizeof(data->current_iface)) {
		memcpy(data->current_iface, iface, iface_len + 1);
	}
	if (ip_len < sizeof(data->current_ip)) {
		memcpy(data->current_ip, ip, ip_len + 1);
	}

	/* Build display string */
	if (!online) {
		strcpy(data->display_str, "offline");
	} else if (cfg->network_show_ip && ip[0]) {
		if (cfg->network_show_interface) {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s: %s", iface, ip);
		} else {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s", ip);
		}
	} else if (cfg->network_show_interface) {
		snprintf(data->display_str, sizeof(data->display_str),
		         "%s", iface);
	} else {
		strcpy(data->display_str, "online");
	}

	self->dirty = true;
}

static void
network_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	network_data_t *data = self->data;
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

	/* Color based on status (or custom if set) */
	barny_config_t *cfg = &data->state->config;
	if (cfg->text_color_set) {
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g, cfg->text_color_b, 0.9);
	} else if (data->is_online) {
		cairo_set_source_rgba(cr, 0.7, 1, 0.7, 0.9);
	} else {
		cairo_set_source_rgba(cr, 1, 0.6, 0.6, 0.9);
	}
	cairo_move_to(cr, x, y + (h - th) / 2);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	self->width = tw + 8;
}

barny_module_t *
barny_module_network_create(void)
{
	barny_module_t *mod  = calloc(1, sizeof(barny_module_t));
	network_data_t *data = calloc(1, sizeof(network_data_t));

	mod->name            = "network";
	mod->position        = BARNY_POS_RIGHT;
	mod->init            = network_init;
	mod->destroy         = network_destroy;
	mod->update          = network_update;
	mod->render          = network_render;
	mod->data            = data;
	mod->width           = 120;
	mod->dirty           = true;

	return mod;
}
