#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include "barny.h"
#include "popup.h"

#define POPUP_LINE_H    24
#define POPUP_MIN_WIDTH 260

typedef struct {
	barny_state_t        *state;
	barny_module_t       *self;
	char                  display_str[128];
	char                  current_iface[32];
	char                  current_ip[64];
	bool                  is_online;
	PangoFontDescription *font_desc;
	PangoFontDescription *popup_font_desc;

	/* popup-extra cached values */
	char                  iface_type[16];   /* "wifi" / "ethernet" / "other" */
	char                  ssid[64];
	char                  ipv4[INET_ADDRSTRLEN];
	char                  ipv6[INET6_ADDRSTRLEN];
	char                  mac[32];
	char                  rx_speed_str[32];
	char                  tx_speed_str[32];

	/* speed sampling */
	unsigned long long    last_rx_bytes;
	unsigned long long    last_tx_bytes;
	struct timespec       last_sample;
	bool                  have_last_sample;

	barny_popup_t        *popup;
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
	char           eth_iface[32]  = "";
	char           wifi_iface[32] = "";

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
		if (strncmp(ent->d_name, "eth", 3) == 0
		    || strncmp(ent->d_name, "en", 2) == 0) {
			memcpy(eth_iface, ent->d_name, namelen + 1);
		} else if (strncmp(ent->d_name, "wlan", 4) == 0
		           || strncmp(ent->d_name, "wl", 2) == 0) {
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

static void
collect_interface_addrs(const char *iface, char *ipv4, size_t ipv4_len,
                        char *ipv6, size_t ipv6_len)
{
	struct ifaddrs *ifaddr, *ifa;
	char            ipv6_global[INET6_ADDRSTRLEN] = "";
	char            ipv6_link[INET6_ADDRSTRLEN]   = "";

	if (ipv4 && ipv4_len)
		ipv4[0] = '\0';
	if (ipv6 && ipv6_len)
		ipv6[0] = '\0';

	if (getifaddrs(&ifaddr) == -1)
		return;

	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		if (strcmp(ifa->ifa_name, iface) != 0)
			continue;

		int family = ifa->ifa_addr->sa_family;

		if (family == AF_INET && ipv4 && ipv4_len) {
			struct sockaddr_in *addr
			        = (struct sockaddr_in *)ifa->ifa_addr;
			char buf[INET_ADDRSTRLEN] = "";
			inet_ntop(AF_INET, &addr->sin_addr, buf, sizeof(buf));
			if (buf[0]) {
				strncpy(ipv4, buf, ipv4_len - 1);
				ipv4[ipv4_len - 1] = '\0';
			}
		} else if (family == AF_INET6) {
			struct sockaddr_in6 *addr
			        = (struct sockaddr_in6 *)ifa->ifa_addr;
			char buf[INET6_ADDRSTRLEN] = "";
			inet_ntop(AF_INET6, &addr->sin6_addr, buf, sizeof(buf));
			if (!buf[0])
				continue;
			if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) {
				if (!ipv6_link[0]) {
					strncpy(ipv6_link, buf,
					        sizeof(ipv6_link) - 1);
					ipv6_link[sizeof(ipv6_link) - 1] = '\0';
				}
			} else {
				if (!ipv6_global[0]) {
					strncpy(ipv6_global, buf,
					        sizeof(ipv6_global) - 1);
					ipv6_global[sizeof(ipv6_global) - 1]
					        = '\0';
				}
			}
		}
	}

	freeifaddrs(ifaddr);

	if (ipv6 && ipv6_len) {
		const char *src = ipv6_global[0] ? ipv6_global : ipv6_link;
		if (src[0]) {
			strncpy(ipv6, src, ipv6_len - 1);
			ipv6[ipv6_len - 1] = '\0';
		}
	}
}

static bool
get_interface_ip(const char *iface, char *ip, size_t ip_len, bool prefer_ipv4)
{
	char ipv4[INET_ADDRSTRLEN]  = "";
	char ipv6[INET6_ADDRSTRLEN] = "";

	collect_interface_addrs(iface, ipv4, sizeof(ipv4), ipv6, sizeof(ipv6));

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

static bool
iface_is_wireless(const char *iface)
{
	if (!iface || !iface[0])
		return false;
	char path[256];
	snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", iface);
	DIR *d = opendir(path);
	if (d) {
		closedir(d);
		return true;
	}
	return false;
}

static void
classify_iface(const char *iface, char *out, size_t out_len)
{
	if (!out || out_len == 0)
		return;
	out[0] = '\0';
	if (!iface || !iface[0]) {
		strncpy(out, "none", out_len - 1);
		out[out_len - 1] = '\0';
		return;
	}
	if (iface_is_wireless(iface)) {
		strncpy(out, "wifi", out_len - 1);
	} else if (strncmp(iface, "eth", 3) == 0
	           || strncmp(iface, "en", 2) == 0) {
		strncpy(out, "ethernet", out_len - 1);
	} else {
		strncpy(out, "other", out_len - 1);
	}
	out[out_len - 1] = '\0';
}

static void
read_mac(const char *iface, char *out, size_t out_len)
{
	if (!out || out_len == 0)
		return;
	out[0] = '\0';
	if (!iface || !iface[0])
		return;

	char path[256];
	snprintf(path, sizeof(path), "/sys/class/net/%s/address", iface);
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	if (fgets(out, (int)out_len, f)) {
		out[strcspn(out, "\n")] = '\0';
	}
	fclose(f);
}

static void
read_ssid(const char *iface, char *out, size_t out_len)
{
	if (!out || out_len == 0)
		return;
	out[0] = '\0';
	if (!iface || !iface[0])
		return;
	if (!iface_is_wireless(iface))
		return;

	char cmd[128];
	snprintf(cmd, sizeof(cmd), "iw dev %s link 2>/dev/null", iface);
	FILE *p = popen(cmd, "r");
	if (!p)
		return;

	char line[256];
	while (fgets(line, sizeof(line), p)) {
		/* trim leading whitespace */
		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (strncmp(s, "SSID:", 5) == 0) {
			char *val = s + 5;
			while (*val == ' ' || *val == '\t')
				val++;
			val[strcspn(val, "\r\n")] = '\0';
			strncpy(out, val, out_len - 1);
			out[out_len - 1] = '\0';
			break;
		}
	}
	pclose(p);
}

static bool
read_iface_bytes(const char *iface, unsigned long long *rx,
                 unsigned long long *tx)
{
	if (!iface || !iface[0])
		return false;
	char path[256];
	FILE *f;

	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes",
	         iface);
	f = fopen(path, "r");
	if (!f)
		return false;
	if (fscanf(f, "%llu", rx) != 1) {
		fclose(f);
		return false;
	}
	fclose(f);

	snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes",
	         iface);
	f = fopen(path, "r");
	if (!f)
		return false;
	if (fscanf(f, "%llu", tx) != 1) {
		fclose(f);
		return false;
	}
	fclose(f);
	return true;
}

static void
format_speed(double bps, char *out, size_t out_len)
{
	if (!out || out_len == 0)
		return;
	if (bps < 1024.0) {
		snprintf(out, out_len, "%.0f B/s", bps);
	} else if (bps < 1024.0 * 1024.0) {
		snprintf(out, out_len, "%.1f KB/s", bps / 1024.0);
	} else if (bps < 1024.0 * 1024.0 * 1024.0) {
		snprintf(out, out_len, "%.1f MB/s", bps / (1024.0 * 1024.0));
	} else {
		snprintf(out, out_len, "%.1f GB/s",
		         bps / (1024.0 * 1024.0 * 1024.0));
	}
}

/* Refresh popup-cached fields. Returns true if any value changed. */
static bool
refresh_popup_data(network_data_t *data)
{
	bool changed = false;
	char new_type[16] = "";
	char new_ssid[64] = "";
	char new_ipv4[INET_ADDRSTRLEN] = "";
	char new_ipv6[INET6_ADDRSTRLEN] = "";
	char new_mac[32] = "";

	classify_iface(data->current_iface, new_type, sizeof(new_type));
	if (strcmp(new_type, data->iface_type) != 0) {
		strncpy(data->iface_type, new_type, sizeof(data->iface_type) - 1);
		data->iface_type[sizeof(data->iface_type) - 1] = '\0';
		changed = true;
	}

	if (data->is_online && data->current_iface[0]) {
		collect_interface_addrs(data->current_iface, new_ipv4,
		                        sizeof(new_ipv4), new_ipv6,
		                        sizeof(new_ipv6));
		read_mac(data->current_iface, new_mac, sizeof(new_mac));
		read_ssid(data->current_iface, new_ssid, sizeof(new_ssid));
	}

	if (strcmp(new_ipv4, data->ipv4) != 0) {
		strncpy(data->ipv4, new_ipv4, sizeof(data->ipv4) - 1);
		data->ipv4[sizeof(data->ipv4) - 1] = '\0';
		changed = true;
	}
	if (strcmp(new_ipv6, data->ipv6) != 0) {
		strncpy(data->ipv6, new_ipv6, sizeof(data->ipv6) - 1);
		data->ipv6[sizeof(data->ipv6) - 1] = '\0';
		changed = true;
	}
	if (strcmp(new_mac, data->mac) != 0) {
		strncpy(data->mac, new_mac, sizeof(data->mac) - 1);
		data->mac[sizeof(data->mac) - 1] = '\0';
		changed = true;
	}
	if (strcmp(new_ssid, data->ssid) != 0) {
		strncpy(data->ssid, new_ssid, sizeof(data->ssid) - 1);
		data->ssid[sizeof(data->ssid) - 1] = '\0';
		changed = true;
	}

	/* Speed sampling */
	unsigned long long rx = 0, tx = 0;
	struct timespec    now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (data->is_online && data->current_iface[0]
	    && read_iface_bytes(data->current_iface, &rx, &tx)) {
		if (data->have_last_sample) {
			double elapsed
			        = (double)(now.tv_sec - data->last_sample.tv_sec)
			          + (double)(now.tv_nsec
			                     - data->last_sample.tv_nsec)
			                    / 1e9;
			if (elapsed > 0.05) {
				double rx_bps = 0.0;
				double tx_bps = 0.0;
				if (rx >= data->last_rx_bytes)
					rx_bps = (double)(rx - data->last_rx_bytes)
					         / elapsed;
				if (tx >= data->last_tx_bytes)
					tx_bps = (double)(tx - data->last_tx_bytes)
					         / elapsed;
				char rx_str[32];
				char tx_str[32];
				format_speed(rx_bps, rx_str, sizeof(rx_str));
				format_speed(tx_bps, tx_str, sizeof(tx_str));
				if (strcmp(rx_str, data->rx_speed_str) != 0) {
					strncpy(data->rx_speed_str, rx_str,
					        sizeof(data->rx_speed_str) - 1);
					data->rx_speed_str[sizeof(data->rx_speed_str)
					                   - 1] = '\0';
					changed = true;
				}
				if (strcmp(tx_str, data->tx_speed_str) != 0) {
					strncpy(data->tx_speed_str, tx_str,
					        sizeof(data->tx_speed_str) - 1);
					data->tx_speed_str[sizeof(data->tx_speed_str)
					                   - 1] = '\0';
					changed = true;
				}
			}
		}
		data->last_rx_bytes    = rx;
		data->last_tx_bytes    = tx;
		data->last_sample      = now;
		data->have_last_sample = true;
	} else {
		if (data->rx_speed_str[0] != '\0') {
			data->rx_speed_str[0] = '\0';
			changed               = true;
		}
		if (data->tx_speed_str[0] != '\0') {
			data->tx_speed_str[0] = '\0';
			changed               = true;
		}
		data->have_last_sample = false;
	}

	return changed;
}

/* --- popup row collection -------------------------------------------- */

typedef struct {
	const char *label;
	const char *value;
} popup_row_t;

#define POPUP_MAX_ROWS 9

static int
popup_collect_rows(const network_data_t *data, popup_row_t *rows,
                   int max_rows)
{
	const barny_config_t *cfg = &data->state->config;
	int                   n   = 0;

	if (n < max_rows) {
		rows[n].label = "Interface";
		rows[n].value = data->current_iface[0] ? data->current_iface
		                                        : "(none)";
		n++;
	}
	if (n < max_rows) {
		rows[n].label = "Type";
		rows[n].value = data->iface_type[0] ? data->iface_type : "(none)";
		n++;
	}
	if (n < max_rows) {
		rows[n].label = "Status";
		rows[n].value = data->is_online ? "up" : "down";
		n++;
	}
	if (cfg->network_popup_show_ssid
	    && strcmp(data->iface_type, "wifi") == 0 && n < max_rows) {
		rows[n].label = "SSID";
		rows[n].value = data->ssid[0] ? data->ssid : "(none)";
		n++;
	}
	if (n < max_rows) {
		rows[n].label = "IPv4";
		rows[n].value = data->ipv4[0] ? data->ipv4 : "(none)";
		n++;
	}
	if (cfg->network_popup_show_ipv6 && data->ipv6[0] && n < max_rows) {
		rows[n].label = "IPv6";
		rows[n].value = data->ipv6;
		n++;
	}
	if (cfg->network_popup_show_mac && n < max_rows) {
		rows[n].label = "MAC";
		rows[n].value = data->mac[0] ? data->mac : "(none)";
		n++;
	}
	if (n < max_rows) {
		rows[n].label = "RX";
		rows[n].value = data->rx_speed_str[0] ? data->rx_speed_str
		                                       : "(none)";
		n++;
	}
	if (n < max_rows) {
		rows[n].label = "TX";
		rows[n].value = data->tx_speed_str[0] ? data->tx_speed_str
		                                       : "(none)";
		n++;
	}
	return n;
}

static int
network_popup_height(void *ud)
{
	network_data_t *data = ud;
	popup_row_t     rows[POPUP_MAX_ROWS];
	int             n = popup_collect_rows(data, rows, POPUP_MAX_ROWS);
	if (n <= 0)
		return POPUP_LINE_H;
	return n * POPUP_LINE_H;
}

static int
network_popup_width(void *ud)
{
	network_data_t *data = ud;
	popup_row_t     rows[POPUP_MAX_ROWS];
	int             n = popup_collect_rows(data, rows, POPUP_MAX_ROWS);
	int             max_w = POPUP_MIN_WIDTH;

	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1,
	                                                1);
	cairo_t         *cr = cairo_create(s);
	PangoLayout     *layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->popup_font_desc);

	for (int i = 0; i < n; i++) {
		int lw, lh, vw, vh;
		pango_layout_set_text(layout, rows[i].label, -1);
		pango_layout_get_pixel_size(layout, &lw, &lh);
		pango_layout_set_text(layout, rows[i].value, -1);
		pango_layout_get_pixel_size(layout, &vw, &vh);
		int total = lw + vw + 24; /* gap between columns */
		if (total > max_w)
			max_w = total;
	}

	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(s);
	return max_w;
}

static void
network_popup_render(void *ud, cairo_t *cr, int w, int h)
{
	network_data_t *data = ud;
	barny_config_t *cfg  = &data->state->config;
	popup_row_t     rows[POPUP_MAX_ROWS];
	int             n;
	PangoLayout    *layout;

	(void)h;

	n = popup_collect_rows(data, rows, POPUP_MAX_ROWS);
	if (n <= 0)
		return;

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, data->popup_font_desc);

	for (int i = 0; i < n; i++) {
		int line_y = i * POPUP_LINE_H;
		int tw, th;
		int text_y;
		int val_x;

		/* Label (left) */
		pango_layout_set_text(layout, rows[i].label, -1);
		pango_layout_get_pixel_size(layout, &tw, &th);
		text_y = line_y + (POPUP_LINE_H - th) / 2;

		cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
		cairo_move_to(cr, 1, text_y + 1);
		pango_cairo_show_layout(cr, layout);

		cairo_set_source_rgba(cr, 0.6, 0.7, 0.65, 0.9);
		cairo_move_to(cr, 0, text_y);
		pango_cairo_show_layout(cr, layout);

		/* Value (right) */
		pango_layout_set_text(layout, rows[i].value, -1);
		pango_layout_get_pixel_size(layout, &tw, &th);
		val_x  = w - tw;
		text_y = line_y + (POPUP_LINE_H - th) / 2;

		cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
		cairo_move_to(cr, val_x + 1, text_y + 1);
		pango_cairo_show_layout(cr, layout);

		if (cfg->text_color_set) {
			cairo_set_source_rgba(cr, cfg->text_color_r,
			                      cfg->text_color_g,
			                      cfg->text_color_b, 0.9);
		} else {
			cairo_set_source_rgba(cr, 0.7, 1.0, 0.7, 0.9);
		}
		cairo_move_to(cr, val_x, text_y);
		pango_cairo_show_layout(cr, layout);
	}

	g_object_unref(layout);
}

static void
network_on_hover(barny_module_t *self, bool hovering, int x, int y)
{
	network_data_t *data = self->data;
	(void)x;
	(void)y;

	if (hovering) {
		if (!data->popup) {
			refresh_popup_data(data);
			barny_popup_callbacks_t cb = {
				.content_height = network_popup_height,
				.content_width  = network_popup_width,
				.render         = network_popup_render,
				.userdata       = data,
			};
			data->popup = barny_popup_create(
			        data->state, self, &cb,
			        data->state->config.network_popup_gap);
		}
	} else {
		if (data->popup) {
			barny_popup_destroy(data->popup);
			data->popup = NULL;
		}
	}
}

static int
network_init(barny_module_t *self, barny_state_t *state)
{
	network_data_t *data   = self->data;
	data->state            = state;
	data->self             = self;
	data->is_online        = false;
	data->current_iface[0] = '\0';
	data->current_ip[0]    = '\0';
	data->iface_type[0]    = '\0';
	data->ssid[0]          = '\0';
	data->ipv4[0]          = '\0';
	data->ipv6[0]          = '\0';
	data->mac[0]           = '\0';
	data->rx_speed_str[0]  = '\0';
	data->tx_speed_str[0]  = '\0';
	data->have_last_sample = false;

	data->font_desc        = pango_font_description_from_string(
                state->config.font ? state->config.font : "Sans 10");

	data->popup_font_desc = pango_font_description_from_string(
	        state->config.font ? state->config.font : "Sans 10");
	int base_size = pango_font_description_get_size(data->popup_font_desc);
	if (base_size > 0) {
		pango_font_description_set_size(data->popup_font_desc,
		                                base_size * 85 / 100);
	} else {
		pango_font_description_set_size(data->popup_font_desc,
		                                9 * PANGO_SCALE);
	}

	strcpy(data->display_str, "offline");

	return 0;
}

static void
network_destroy(barny_module_t *self)
{
	network_data_t *data = self->data;
	if (!data)
		return;

	if (data->state && data->state->hover_module == self)
		data->state->hover_module = NULL;

	barny_popup_destroy(data->popup);
	data->popup = NULL;

	if (data->font_desc) {
		pango_font_description_free(data->font_desc);
	}
	if (data->popup_font_desc) {
		pango_font_description_free(data->popup_font_desc);
	}

	free(data);
	self->data = NULL;
}

static void
network_update(barny_module_t *self)
{
	network_data_t *data      = self->data;
	barny_config_t *cfg       = &data->state->config;

	char            iface[32] = "";
	char            ip[64]    = "";
	bool            online    = false;

	/* Determine which interface to use */
	const char     *cfg_iface = cfg->network_interface;
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
	bool changed = (online != data->is_online)
	               || strcmp(iface, data->current_iface) != 0
	               || strcmp(ip, data->current_ip) != 0;

	if (changed) {
		data->is_online  = online;
		size_t iface_len = strlen(iface);
		size_t ip_len    = strlen(ip);
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
				snprintf(data->display_str,
				         sizeof(data->display_str), "%s: %s",
				         iface, ip);
			} else {
				snprintf(data->display_str,
				         sizeof(data->display_str), "%s", ip);
			}
		} else if (cfg->network_show_interface) {
			snprintf(data->display_str, sizeof(data->display_str),
			         "%s", iface);
		} else {
			strcpy(data->display_str, "online");
		}

		self->dirty = true;

		/* Reset speed counters when iface changes */
		data->have_last_sample = false;
	}

	/* Always refresh popup-cached data; redraw if visible & changed. */
	bool popup_changed = refresh_popup_data(data);
	if (popup_changed && barny_popup_visible(data->popup))
		barny_popup_redraw(data->popup);
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
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
		                      cfg->text_color_b, 0.9);
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

	if (!mod || !data) {
		free(mod);
		free(data);
		return NULL;
	}

	mod->name     = "network";
	mod->position = BARNY_POS_RIGHT;
	mod->init     = network_init;
	mod->destroy  = network_destroy;
	mod->update   = network_update;
	mod->render   = network_render;
	mod->on_hover = network_on_hover;
	mod->data     = data;
	mod->width    = 120;
	mod->dirty    = true;

	return mod;
}
