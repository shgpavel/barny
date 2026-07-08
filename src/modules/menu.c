#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "barny.h"
#include "popup.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define MENU_RADIUS    12
#define MENU_PAD_X     14
#define MENU_PAD_Y     8
#define MENU_ROW_H     30
#define MENU_SEP_H     11
#define MENU_MIN_W     170
#define MENU_MAX_W     440
#define MENU_ARROW_W   18
#define MENU_MAX_DEPTH 16

#define MENU_BACK_ID   (-100000)

typedef struct {
	barny_menu_item_t *item;
	int                y;
	int                h;
	bool               is_back;
	bool               separator;
	bool               enabled;
} menu_row_t;

struct barny_menu {
	barny_state_t                *state;
	barny_output_t               *out;
	char                         *service;
	char                         *menu_path;
	barny_menu_item_t            *root;

	barny_menu_item_t            *stack[MENU_MAX_DEPTH];
	int                           depth;

	menu_row_t                   *rows;
	int                           row_count;
	int                           hover;

	int                           anchor_x;

	PangoFontDescription         *font;

	struct wl_surface            *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer             *buffer;
	cairo_surface_t              *cairo_surface;
	cairo_t                      *cr;
	void                         *shm_data;
	int                           shm_size;

	int                           surf_w;
	int                           surf_h;
	int                           menu_x;
	int                           menu_y;
	int                           menu_w;
	int                           menu_h;

	bool                          configured;
};

static barny_menu_item_t *
menu_current_parent(const barny_menu_t *m)
{
	return m->stack[m->depth];
}

static int
menu_text_width(barny_menu_t *m, const char *s)
{
	return barny_popup_measure_text(m->font, s ? s : "");
}

static void
menu_build_rows(barny_menu_t *m)
{
	barny_menu_item_t *parent = menu_current_parent(m);
	int                i;
	int                y       = MENU_PAD_Y;
	int                content = 0;
	int                w;
	int                cap;

	free(m->rows);
	m->rows      = NULL;
	m->row_count = 0;

	cap = parent->child_count + 1;
	m->rows = calloc(cap, sizeof(*m->rows));
	if (!m->rows)
		return;

	if (m->depth > 0) {
		menu_row_t *r = &m->rows[m->row_count++];
		r->item       = NULL;
		r->is_back    = true;
		r->enabled    = true;
		r->y          = y;
		r->h          = MENU_ROW_H;
		y += r->h;

		w = menu_text_width(m, "\xE2\x80\xB9 ") + menu_text_width(m, parent->label ? parent->label : "Back");
		if (w > content)
			content = w;
	}

	for (i = 0; i < parent->child_count; i++) {
		barny_menu_item_t *it = &parent->children[i];
		menu_row_t        *r;

		if (!it->visible)
			continue;

		r            = &m->rows[m->row_count++];
		r->item      = it;
		r->separator = it->separator;
		r->enabled   = it->enabled && !it->separator;
		r->y         = y;
		r->h         = it->separator ? MENU_SEP_H : MENU_ROW_H;
		y += r->h;

		if (!it->separator) {
			w = menu_text_width(m, it->label);
			if (it->has_submenu)
				w += MENU_ARROW_W;
			if (it->toggle_state >= 0)
				w += MENU_ARROW_W;
			if (w > content)
				content = w;
		}
	}

	w = content + 2 * MENU_PAD_X;
	if (w < MENU_MIN_W)
		w = MENU_MIN_W;
	if (w > MENU_MAX_W)
		w = MENU_MAX_W;

	m->menu_w = w;
	m->menu_h = y + MENU_PAD_Y;
	m->hover  = -1;
}

static void
menu_compute_rect(barny_menu_t *m)
{
	barny_config_t *cfg = &m->state->config;
	int             x;
	int             y;
	int             reserved;

	x = cfg->margin_left + m->anchor_x;
	if (x + m->menu_w > m->surf_w)
		x = m->surf_w - m->menu_w;
	if (x < 0)
		x = 0;

	reserved = cfg->height + cfg->tray_menu_gap
	           + (cfg->position_top ? cfg->margin_top : cfg->margin_bottom);

	if (cfg->position_top)
		y = reserved;
	else
		y = m->surf_h - reserved - m->menu_h;

	if (y + m->menu_h > m->surf_h)
		y = m->surf_h - m->menu_h;
	if (y < 0)
		y = 0;

	m->menu_x = x;
	m->menu_y = y;
}

static int
menu_create_shm(int size)
{
	static unsigned counter = 0;
	char            name[64];
	struct timespec ts;
	int             fd;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		ts.tv_sec  = 0;
		ts.tv_nsec = 0;
	}

	snprintf(name, sizeof(name), "/barny-menu-%d-%u-%lu", (int)getpid(),
	         (unsigned)counter++, (unsigned long)ts.tv_nsec);

	fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
		return -1;

	if (ftruncate(fd, size) < 0) {
		shm_unlink(name);
		close(fd);
		return -1;
	}

	shm_unlink(name);
	return fd;
}

static void
menu_draw_label(barny_menu_t *m, PangoLayout *layout, const char *text, int x,
                int y, int row_h, double alpha)
{
	barny_config_t *cfg = &m->state->config;
	double          tr  = cfg->text_color_set ? cfg->text_color_r : 0.95;
	double          tg  = cfg->text_color_set ? cfg->text_color_g : 0.95;
	double          tb  = cfg->text_color_set ? cfg->text_color_b : 0.97;
	int             tw, th, ty;

	pango_layout_set_text(layout, text ? text : "", -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	ty = y + (row_h - th) / 2;

	cairo_set_source_rgba(m->cr, 0, 0, 0, 0.4 * alpha);
	cairo_move_to(m->cr, x + 1, ty + 1);
	pango_cairo_show_layout(m->cr, layout);

	cairo_set_source_rgba(m->cr, tr, tg, tb, alpha);
	cairo_move_to(m->cr, x, ty);
	pango_cairo_show_layout(m->cr, layout);
}

static void
menu_paint(barny_menu_t *m)
{
	barny_state_t   *state = m->state;
	cairo_t         *cr;
	int              mw, mh;
	cairo_surface_t *bg;
	int              out_w = 0, out_h = 0;
	PangoLayout     *layout;
	int              i;

	if (!m->cr || !m->buffer)
		return;

	cr = m->cr;
	mw = m->menu_w;
	mh = m->menu_h;

	cairo_save(cr);
	cairo_rectangle(cr, m->menu_x, m->menu_y, mw, mh);
	cairo_clip(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_translate(cr, m->menu_x, m->menu_y);

	if (m->out) {
		bg    = state->displaced_wallpaper ? state->displaced_wallpaper
		                                   : state->blurred_wallpaper;
		out_w = m->out->width;
		out_h = m->out->height;
	} else {
		bg = NULL;
	}

	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, mw, mh, MENU_RADIUS);
	cairo_clip(cr);
	barny_paint_glass_bg(cr, bg, out_w, out_h, m->menu_x, m->menu_y, mh,
	                     state->config.position_top);
	cairo_restore(cr);

	barny_draw_glass_frame(cr, mw, mh, MENU_RADIUS);

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, m->font);

	for (i = 0; i < m->row_count; i++) {
		menu_row_t *r = &m->rows[i];

		if (r->separator) {
			cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
			cairo_rectangle(cr, MENU_PAD_X, r->y + r->h / 2, mw - 2 * MENU_PAD_X, 1);
			cairo_fill(cr);
			continue;
		}

		if (i == m->hover && r->enabled) {
			barny_rounded_rect_path(cr, 4, r->y + 1, mw - 8, r->h - 2, 7);
			cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
			cairo_fill(cr);
		}

		if (r->is_back) {
			char back[256];
			snprintf(back, sizeof(back), "\xE2\x80\xB9 %s",
			         menu_current_parent(m)->label
			                 ? menu_current_parent(m)->label
			                 : "Back");
			menu_draw_label(m, layout, back, MENU_PAD_X, r->y, r->h, 0.95);
			continue;
		}

		menu_draw_label(m, layout, r->item->label, MENU_PAD_X, r->y, r->h,
		                r->enabled ? 0.95 : 0.4);

		if (r->item->toggle_state == 1) {
			menu_draw_label(m, layout, "\xE2\x9C\x93", MENU_PAD_X, r->y,
			                r->h, 0.9);
		}
		if (r->item->has_submenu) {
			int aw, ah, ay;
			pango_layout_set_text(layout, "\xE2\x80\xBA", -1);
			pango_layout_get_pixel_size(layout, &aw, &ah);
			ay = r->y + (r->h - ah) / 2;
			cairo_set_source_rgba(cr, 0.9, 0.9, 0.92,
			                      r->enabled ? 0.85 : 0.35);
			cairo_move_to(cr, mw - MENU_PAD_X - aw, ay);
			pango_cairo_show_layout(cr, layout);
		}
	}

	g_object_unref(layout);

	cairo_restore(cr);

	cairo_surface_flush(m->cairo_surface);
	wl_surface_attach(m->surface, m->buffer, 0, 0);
	wl_surface_damage_buffer(m->surface, m->menu_x, m->menu_y, mw, mh);
	wl_surface_commit(m->surface);
}

static void
menu_teardown_buffer(barny_menu_t *m)
{
	if (m->cr) {
		cairo_destroy(m->cr);
		m->cr = NULL;
	}
	if (m->cairo_surface) {
		cairo_surface_destroy(m->cairo_surface);
		m->cairo_surface = NULL;
	}
	if (m->buffer) {
		wl_buffer_destroy(m->buffer);
		m->buffer = NULL;
	}
	if (m->shm_data) {
		munmap(m->shm_data, (size_t)m->shm_size);
		m->shm_data = NULL;
	}
	m->shm_size = 0;
}

static void
menu_layer_configure(void *userdata, struct zwlr_layer_surface_v1 *surface,
                     uint32_t serial, uint32_t width, uint32_t height)
{
	barny_menu_t       *m = userdata;
	int                 pw, ph, stride, size, fd;
	struct wl_shm_pool *pool;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	pw = (int)width > 0 ? (int)width : (m->out ? m->out->width : 0);
	ph = (int)height > 0 ? (int)height
	                     : (m->out ? m->out->mode_height : 0);
	if (pw <= 0 || ph <= 0)
		return;

	if (m->buffer && pw == m->surf_w && ph == m->surf_h) {
		menu_compute_rect(m);
		menu_paint(m);
		return;
	}

	if (m->buffer)
		menu_teardown_buffer(m);

	stride = pw * 4;
	size   = stride * ph;

	fd     = menu_create_shm(size);
	if (fd < 0)
		return;

	m->shm_data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
	                   MAP_SHARED, fd, 0);
	if (m->shm_data == MAP_FAILED) {
		close(fd);
		m->shm_data = NULL;
		return;
	}
	m->shm_size = size;

	pool      = wl_shm_create_pool(m->state->shm, fd, size);
	m->buffer = wl_shm_pool_create_buffer(pool, 0, pw, ph, stride,
	                                      WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	m->cairo_surface = cairo_image_surface_create_for_data(
	        m->shm_data, CAIRO_FORMAT_ARGB32, pw, ph, stride);
	m->cr         = cairo_create(m->cairo_surface);
	m->surf_w     = pw;
	m->surf_h     = ph;
	m->configured = true;

	menu_compute_rect(m);
	menu_paint(m);
}

static void
menu_layer_closed(void *userdata, struct zwlr_layer_surface_v1 *surface)
{
	barny_menu_t *m = userdata;
	(void)surface;
	m->configured = false;
	barny_menu_close(m->state);
}

static const struct zwlr_layer_surface_v1_listener menu_layer_listener = {
	.configure = menu_layer_configure,
	.closed    = menu_layer_closed,
};

static void
menu_configure_surface(barny_menu_t *m)
{
	uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
	                  | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
	                  | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
	                  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

	zwlr_layer_surface_v1_set_anchor(m->layer_surface, anchor);
	zwlr_layer_surface_v1_set_size(m->layer_surface, 0, 0);
	zwlr_layer_surface_v1_set_exclusive_zone(m->layer_surface, -1);
	zwlr_layer_surface_v1_set_margin(m->layer_surface, 0, 0, 0, 0);

	wl_surface_commit(m->surface);
}

static void
menu_relayout(barny_menu_t *m)
{
	menu_build_rows(m);
	if (m->configured) {
		menu_compute_rect(m);
		menu_paint(m);
	}
}

void
barny_menu_open(barny_state_t *state, sni_item_t *item, int anchor_x)
{
	barny_menu_t *m;
	char         *path;

	if (!state || !state->compositor || !state->layer_shell || !item)
		return;

	barny_menu_close(state);

	path = barny_sni_item_menu_path(state, item);
	if (!path)
		return;

	m = calloc(1, sizeof(*m));
	if (!m) {
		free(path);
		return;
	}

	m->state     = state;
	m->out       = state->pointer_output;
	m->service   = strdup(item->service);
	m->menu_path = path;
	m->anchor_x  = anchor_x;
	m->hover     = -1;

	if (!m->out)
		m->out = state->outputs;
	if (!m->out || !m->service) {
		free(m->service);
		free(m->menu_path);
		free(m);
		return;
	}

	m->root = barny_dbusmenu_get_layout(state, m->service, m->menu_path);
	if (!m->root || m->root->child_count == 0) {
		barny_dbusmenu_free(m->root);
		free(m->service);
		free(m->menu_path);
		free(m);
		return;
	}

	barny_dbusmenu_about_to_show(state, m->service, m->menu_path,
	                             m->root->id);

	m->stack[0] = m->root;
	m->depth    = 0;
	m->font     = barny_popup_font_from(state->config.font, "Sans 11");

	m->surface = wl_compositor_create_surface(state->compositor);
	if (!m->surface) {
		barny_dbusmenu_free(m->root);
		if (m->font)
			pango_font_description_free(m->font);
		free(m->service);
		free(m->menu_path);
		free(m);
		return;
	}

	m->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
	        state->layer_shell, m->surface, m->out->wl_output,
	        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "barny-menu");
	if (!m->layer_surface) {
		wl_surface_destroy(m->surface);
		barny_dbusmenu_free(m->root);
		if (m->font)
			pango_font_description_free(m->font);
		free(m->service);
		free(m->menu_path);
		free(m);
		return;
	}

	zwlr_layer_surface_v1_add_listener(m->layer_surface,
	                                   &menu_layer_listener, m);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
	        m->layer_surface,
	        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

	state->menu = m;

	menu_build_rows(m);
	menu_configure_surface(m);
}

void
barny_menu_close(barny_state_t *state)
{
	barny_menu_t *m;

	if (!state || !state->menu)
		return;

	m           = state->menu;
	state->menu = NULL;

	menu_teardown_buffer(m);

	if (m->layer_surface)
		zwlr_layer_surface_v1_destroy(m->layer_surface);
	if (m->surface)
		wl_surface_destroy(m->surface);

	if (m->font)
		pango_font_description_free(m->font);

	barny_dbusmenu_free(m->root);
	free(m->rows);
	free(m->service);
	free(m->menu_path);
	free(m);

	wl_display_flush(state->display);
}

bool
barny_menu_is_open(barny_state_t *state)
{
	return state && state->menu != NULL;
}

bool
barny_menu_owns_surface(barny_state_t *state, struct wl_surface *surface)
{
	return state && state->menu && surface
	       && state->menu->surface == surface;
}

static bool
menu_point_inside(const barny_menu_t *m, double sx, double sy)
{
	return sx >= m->menu_x && sx < m->menu_x + m->menu_w
	       && sy >= m->menu_y && sy < m->menu_y + m->menu_h;
}

static int
menu_row_at(barny_menu_t *m, double ly)
{
	int i;

	for (i = 0; i < m->row_count; i++) {
		menu_row_t *r = &m->rows[i];
		if (ly >= r->y && ly < r->y + r->h)
			return i;
	}
	return -1;
}

void
barny_menu_pointer_motion(barny_state_t *state, double sx, double sy)
{
	barny_menu_t *m = state ? state->menu : NULL;
	int           row;

	if (!m || !m->configured)
		return;

	row = -1;
	if (menu_point_inside(m, sx, sy))
		row = menu_row_at(m, sy - m->menu_y);
	if (row >= 0 && (m->rows[row].separator || !m->rows[row].enabled))
		row = -1;

	if (row != m->hover) {
		m->hover = row;
		menu_paint(m);
	}
}

void
barny_menu_pointer_button(barny_state_t *state, uint32_t button,
                          uint32_t button_state)
{
	barny_menu_t      *m = state ? state->menu : NULL;
	menu_row_t        *r;
	barny_menu_item_t *it;
	int                row;

	if (!m || button_state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;

	if (!menu_point_inside(m, state->pointer_x, state->pointer_y)) {
		barny_menu_close(state);
		return;
	}

	if (button != BTN_LEFT)
		return;

	row = menu_row_at(m, state->pointer_y - m->menu_y);
	if (row < 0 || row >= m->row_count)
		return;

	r = &m->rows[row];

	if (r->is_back) {
		if (m->depth > 0)
			m->depth--;
		menu_relayout(m);
		return;
	}

	it = r->item;
	if (!it || !r->enabled)
		return;

	if (it->has_submenu && it->child_count > 0) {
		if (m->depth + 1 < MENU_MAX_DEPTH) {
			barny_dbusmenu_about_to_show(state, m->service,
			                             m->menu_path, it->id);
			m->stack[++m->depth] = it;
			menu_relayout(m);
		}
		return;
	}

	barny_dbusmenu_event_clicked(state, m->service, m->menu_path, it->id);
	barny_menu_close(state);
}

void
barny_menu_key_escape(barny_state_t *state)
{
	barny_menu_close(state);
}
