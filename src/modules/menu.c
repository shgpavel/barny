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
#include "util.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define MENU_RADIUS    BARNY_POPUP_RADIUS
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

enum menu_anim {
	MENU_ANIM_NONE = 0,
	MENU_ANIM_OPENING,
	MENU_ANIM_OPEN,
	MENU_ANIM_CLOSING,
};

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
	int                           anchor_w;

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

	/* the menu body, and the patch it lives in: body plus the neck rows
	   bridging it to the bar, exactly as a module popup is laid out */
	int                           menu_x;
	int                           menu_y;
	int                           menu_w;
	int                           menu_h;
	int                           neck;
	int                           patch_x;
	int                           patch_y;
	int                           patch_w;
	int                           patch_h;
	int                           damage_x;
	int                           damage_y;
	int                           damage_w;
	int                           damage_h;

	cairo_surface_t              *glass_src;
	cairo_surface_t              *content_cache;

	enum menu_anim                anim;
	double                        morph;
	double                        morph_v;
	uint64_t                      last_ms;
	struct wl_callback           *frame_cb;

	bool                          configured;
};

static void
menu_finalize_destroy(barny_menu_t *m);
static void
menu_schedule_frame(barny_menu_t *m);

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

/* The menu hangs off the bar the way a module popup does: the body sits a gap
   away from the bar edge, and the gap rows are the neck the droplet stretches
   through. The patch is flush against the bar's exclusive edge, so the panel
   geometry -- and with it the glass -- is the popup's. */
static void
menu_compute_rect(barny_menu_t *m)
{
	barny_config_t *cfg      = &m->state->config;
	int             reserved = barny_config_exclusive_zone(cfg);
	int             right    = m->surf_w - cfg->margin_right;
	int             x;
	int             y;

	x = (cfg->margin_left - m->out->pad_left) + m->anchor_x;

	/* stay inside the bar's own margins: run to the screen edge instead and
	   the corner has no room to round, and the glass has no wallpaper left
	   to sample */
	if (x + m->menu_w > right)
		x = right - m->menu_w;
	if (x < cfg->margin_left)
		x = cfg->margin_left;

	m->neck    = cfg->tray_menu_gap;
	m->patch_w = m->menu_w;
	m->patch_h = m->menu_h + m->neck;
	m->patch_x = x;

	if (cfg->position_top)
		y = reserved;
	else
		y = m->surf_h - reserved - m->patch_h;
	if (y + m->patch_h > m->surf_h)
		y = m->surf_h - m->patch_h;
	if (y < 0)
		y = 0;

	m->patch_y = y;
	m->menu_x  = m->patch_x;
	m->menu_y  = m->patch_y + (cfg->position_top ? m->neck : 0);
}

static void
menu_panel(const barny_menu_t *m, barny_glass_panel_t *panel)
{
	const barny_config_t *cfg = &m->state->config;
	int                   icon_cx;

	/* the droplet is born under the icon that was clicked, then slides to
	   the centre of the body as it opens */
	icon_cx = (cfg->margin_left - m->out->pad_left) + m->anchor_x
	          + m->anchor_w / 2 - m->patch_x;

	panel->w            = m->patch_w;
	panel->h            = m->patch_h;
	panel->body_w       = m->menu_w;
	panel->body_h       = m->menu_h;
	panel->body_y       = cfg->position_top ? m->neck : 0;
	panel->anchor_x     = icon_cx;
	panel->glass_x      = barny_glass_panel_glass_x(cfg, m->patch_x);
	panel->glass_y      = barny_glass_panel_glass_y(cfg, m->out->height,
	                                                m->patch_h);
	panel->position_top = cfg->position_top;
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
menu_draw_label(barny_menu_t *m, cairo_t *cr, PangoLayout *layout,
                const char *text, int x, int y, int row_h, double alpha)
{
	barny_config_t *cfg = &m->state->config;
	double          tr  = cfg->text_color_set ? cfg->text_color_r : 0.95;
	double          tg  = cfg->text_color_set ? cfg->text_color_g : 0.95;
	double          tb  = cfg->text_color_set ? cfg->text_color_b : 0.97;
	int             tw, th, ty;

	pango_layout_set_text(layout, text ? text : "", -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	ty = y + (row_h - th) / 2;

	cairo_set_source_rgba(cr, 0, 0, 0, 0.4 * alpha);
	cairo_move_to(cr, x + 1, ty + 1);
	pango_cairo_show_layout(cr, layout);

	cairo_set_source_rgba(cr, tr, tg, tb, alpha);
	cairo_move_to(cr, x, ty);
	pango_cairo_show_layout(cr, layout);
}

static void
menu_render_rows(barny_menu_t *m, cairo_t *cr)
{
	int          mw = m->menu_w;
	PangoLayout *layout;
	int          i;

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
			menu_draw_label(m, cr, layout, back, MENU_PAD_X, r->y,
			                r->h, 0.95);
			continue;
		}

		menu_draw_label(m, cr, layout, r->item->label, MENU_PAD_X, r->y,
		                r->h, r->enabled ? 0.95 : 0.4);

		if (r->item->toggle_state == 1) {
			menu_draw_label(m, cr, layout, "\xE2\x9C\x93", MENU_PAD_X,
			                r->y, r->h, 0.9);
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
}

/* The rows are cached like a popup's content: the morph scales them into the
   droplet, and a hover only has to redraw this one small surface. */
static void
menu_build_content(barny_menu_t *m)
{
	cairo_t *cc;

	if (m->content_cache) {
		cairo_surface_destroy(m->content_cache);
		m->content_cache = NULL;
	}

	m->content_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                              m->menu_w, m->menu_h);
	if (cairo_surface_status(m->content_cache) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(m->content_cache);
		m->content_cache = NULL;
		return;
	}

	cc = cairo_create(m->content_cache);
	menu_render_rows(m, cc);
	cairo_destroy(cc);
}

static void
menu_build_glass(barny_menu_t *m)
{
	barny_glass_panel_t panel;

	if (m->glass_src) {
		cairo_surface_destroy(m->glass_src);
		m->glass_src = NULL;
	}

	menu_panel(m, &panel);
	m->glass_src = barny_glass_panel_bg(m->state, m->out, &panel);
}

static void
menu_present(barny_menu_t *m, double morph)
{
	barny_glass_panel_t panel;
	cairo_t            *cr = m->cr;
	int                 x0, y0, x1, y1;

	if (!cr || !m->buffer)
		return;

	menu_panel(m, &panel);

	/* a submenu resizes the panel, so wipe and repost the union of the old
	   rect and the new one or the wider layout leaves a ghost behind */
	x0 = m->patch_x;
	y0 = m->patch_y;
	x1 = m->patch_x + m->patch_w;
	y1 = m->patch_y + m->patch_h;
	if (m->damage_w > 0 && m->damage_h > 0) {
		if (m->damage_x < x0)
			x0 = m->damage_x;
		if (m->damage_y < y0)
			y0 = m->damage_y;
		if (m->damage_x + m->damage_w > x1)
			x1 = m->damage_x + m->damage_w;
		if (m->damage_y + m->damage_h > y1)
			y1 = m->damage_y + m->damage_h;
	}

	cairo_save(cr);
	cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
	cairo_clip(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_translate(cr, m->patch_x, m->patch_y);
	if (m->state->config.popup_animations) {
		barny_glass_panel_morph(cr, &panel, m->glass_src,
		                        m->content_cache, morph);
	} else {
		barny_glass_panel_compose(cr, m->state, m->out, &panel,
		                          m->content_cache);
	}
	cairo_restore(cr);

	cairo_surface_flush(m->cairo_surface);
	wl_surface_attach(m->surface, m->buffer, 0, 0);
	wl_surface_damage_buffer(m->surface, x0, y0, x1 - x0, y1 - y0);

	m->damage_x = m->patch_x;
	m->damage_y = m->patch_y;
	m->damage_w = m->patch_w;
	m->damage_h = m->patch_h;
}

static void
menu_animate(barny_menu_t *m)
{
	bool closing = m->anim == MENU_ANIM_CLOSING;
	bool settled;

	settled = barny_glass_panel_step(&m->morph, &m->morph_v, &m->last_ms,
	                                 closing);

	menu_present(m, m->morph);

	if (settled) {
		if (closing) {
			wl_surface_commit(m->surface);
			menu_finalize_destroy(m);
			return;
		}
		m->anim = MENU_ANIM_OPEN;
		wl_surface_commit(m->surface);
		return;
	}

	menu_schedule_frame(m);
	wl_surface_commit(m->surface);
}

static void
menu_frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	barny_menu_t *m = data;
	(void)time;

	wl_callback_destroy(cb);
	m->frame_cb = NULL;
	menu_animate(m);
}

static const struct wl_callback_listener menu_frame_listener = {
	.done = menu_frame_done,
};

static void
menu_schedule_frame(barny_menu_t *m)
{
	if (m->frame_cb)
		return;
	m->frame_cb = wl_surface_frame(m->surface);
	wl_callback_add_listener(m->frame_cb, &menu_frame_listener, m);
}

static void
menu_paint(barny_menu_t *m)
{
	if (!m->cr || !m->buffer)
		return;

	menu_present(m, m->anim == MENU_ANIM_NONE ? 1.0 : m->morph);
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
	menu_build_glass(m);
	menu_build_content(m);

	if (!m->state->config.popup_animations) {
		menu_paint(m);
		return;
	}

	if (m->anim == MENU_ANIM_NONE) {
		m->anim    = MENU_ANIM_OPENING;
		m->morph   = 0.0;
		m->morph_v = 0.0;
		m->last_ms = barny_now_ms();
	}

	menu_animate(m);
}

static void
menu_layer_closed(void *userdata, struct zwlr_layer_surface_v1 *surface)
{
	barny_menu_t *m = userdata;
	(void)surface;

	m->configured = false;

	/* a menu already riding its close morph has no frame callbacks left to
	   come, so it has to be torn down here or it never is */
	if (m->state->menu != m) {
		menu_finalize_destroy(m);
		return;
	}

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

/* A submenu is a new body: new rows, new size, so the glass under it and the
   cached rows both have to be rebuilt. The panel does not re-morph -- it is the
   same droplet, just holding different rows. */
static void
menu_relayout(barny_menu_t *m)
{
	menu_build_rows(m);
	if (!m->configured)
		return;

	menu_compute_rect(m);
	menu_build_glass(m);
	menu_build_content(m);
	menu_paint(m);
}

void
barny_menu_open(barny_state_t *state, sni_item_t *item, int anchor_x,
                int anchor_w)
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
	m->anchor_w  = anchor_w > 0 ? anchor_w : 1;
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

static void
menu_finalize_destroy(barny_menu_t *m)
{
	barny_state_t *state = m->state;

	if (m->frame_cb) {
		wl_callback_destroy(m->frame_cb);
		m->frame_cb = NULL;
	}
	if (m->glass_src) {
		cairo_surface_destroy(m->glass_src);
		m->glass_src = NULL;
	}
	if (m->content_cache) {
		cairo_surface_destroy(m->content_cache);
		m->content_cache = NULL;
	}

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

void
barny_menu_close(barny_state_t *state)
{
	barny_menu_t     *m;
	struct wl_region *empty;

	if (!state || !state->menu)
		return;

	m           = state->menu;
	state->menu = NULL;

	/* the caches carry the menu through the close morph; nothing else may
	   reach it once it is off the state, so it also stops swallowing input */
	if (!state->config.popup_animations || !m->configured || !m->buffer
	    || !m->glass_src) {
		menu_finalize_destroy(m);
		return;
	}

	empty = wl_compositor_create_region(state->compositor);
	wl_surface_set_input_region(m->surface, empty);
	wl_region_destroy(empty);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
	        m->layer_surface,
	        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

	m->anim    = MENU_ANIM_CLOSING;
	m->hover   = -1;
	m->last_ms = barny_now_ms();
	if (!m->frame_cb)
		menu_animate(m);

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
		menu_build_content(m);
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
