#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "popup.h"
#include "util.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define POPUP_WIDTH_DEFAULT 180
#define POPUP_RADIUS        12

#define POPUP_ANIM_OPEN_MS  170.0
#define POPUP_ANIM_CLOSE_MS 130.0
#define POPUP_ANIM_SCALE    0.90
#define POPUP_ANIM_SLIDE    9.0

enum popup_anim {
	POPUP_ANIM_NONE = 0,
	POPUP_ANIM_OPENING,
	POPUP_ANIM_OPEN,
	POPUP_ANIM_CLOSING,
};

struct barny_popup {
	barny_state_t                *state;
	barny_module_t               *owner;
	barny_popup_callbacks_t       cb;
	int                           gap_px;

	bool                          configured;
	struct wl_surface            *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer             *buffer;
	cairo_surface_t              *cairo_surface;
	cairo_t                      *cr;
	void                         *shm_data;
	int                           shm_size;
	int                           screen_x;
	int                           screen_y;
	int                           current_w;
	int                           current_h;

	enum popup_anim               anim;
	uint64_t                      anim_start_ms;
	struct wl_callback           *frame_cb;
	cairo_surface_t              *snapshot;
};

static int
popup_compute_width(const barny_popup_t *p)
{
	int w = 0;
	if (p->cb.content_width)
		w = p->cb.content_width(p->cb.userdata);
	if (w <= 0)
		w = POPUP_WIDTH_DEFAULT;
	return w;
}

static int
popup_compute_height(const barny_popup_t *p)
{
	int ch = p->cb.content_height ? p->cb.content_height(p->cb.userdata) : 0;
	if (ch < 0)
		ch = 0;
	return BARNY_POPUP_PAD_Y * 2 + ch;
}

static int
popup_create_shm(int size)
{
	static unsigned counter = 0;
	char            name[64];
	struct timespec ts;
	int             fd;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		ts.tv_sec  = 0;
		ts.tv_nsec = 0;
	}

	snprintf(name, sizeof(name), "/barny-popup-%d-%u-%lu",
	         (int)getpid(), (unsigned)counter++,
	         (unsigned long)ts.tv_nsec);

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
popup_teardown_buffer(barny_popup_t *p);

static void
popup_compose(barny_popup_t *p)
{
	barny_state_t   *state = p->state;
	cairo_t         *cr    = p->cr;
	int              pw    = p->current_w;
	int              ph    = p->current_h;
	barny_output_t  *out;
	cairo_surface_t *bg;
	int              out_w;
	int              out_h;
	int              content_w;
	int              content_h;

	out   = state->pointer_output;
	bg    = NULL;
	out_w = 0;
	out_h = 0;
	if (out) {
		bg    = state->displaced_wallpaper ? state->displaced_wallpaper : state->blurred_wallpaper;
		out_w = out->width;
		out_h = out->height;
	}

	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, pw, ph, POPUP_RADIUS);
	cairo_clip(cr);
	barny_paint_glass_bg(cr, bg, out_w, out_h, p->screen_x, p->screen_y, ph,
	                     state->config.position_top);
	cairo_restore(cr);

	barny_draw_glass_frame(cr, pw, ph, POPUP_RADIUS);

	if (p->cb.render) {
		content_w = pw - 2 * BARNY_POPUP_PAD_X;
		content_h = ph - 2 * BARNY_POPUP_PAD_Y;
		cairo_save(cr);
		cairo_translate(cr, BARNY_POPUP_PAD_X, BARNY_POPUP_PAD_Y);
		p->cb.render(p->cb.userdata, cr, content_w, content_h);
		cairo_restore(cr);
	}
}

static void
popup_present(barny_popup_t *p, double alpha, double scale)
{
	cairo_t *cr = p->cr;
	int      pw = p->current_w;
	int      ph = p->current_h;
	double   ax;
	double   ay;
	double   slide;

	if (!cr || !p->buffer)
		return;

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	if (alpha > 0.003) {
		ax    = pw / 2.0;
		ay    = p->state->config.position_top ? 0.0 : (double)ph;
		slide = (1.0 - alpha) * POPUP_ANIM_SLIDE
		        * (p->state->config.position_top ? -1.0 : 1.0);

		cairo_save(cr);
		if (scale < 0.999 || alpha < 0.999 || p->snapshot) {
			cairo_push_group(cr);
			cairo_translate(cr, ax, ay + slide);
			cairo_scale(cr, scale, scale);
			cairo_translate(cr, -ax, -ay);
			if (p->snapshot) {
				cairo_set_source_surface(cr, p->snapshot, 0, 0);
				cairo_paint(cr);
			} else {
				popup_compose(p);
			}
			cairo_pop_group_to_source(cr);
			cairo_paint_with_alpha(cr, alpha);
		} else {
			popup_compose(p);
		}
		cairo_restore(cr);
	}

	cairo_surface_flush(p->cairo_surface);
	wl_surface_attach(p->surface, p->buffer, 0, 0);
	wl_surface_damage_buffer(p->surface, 0, 0, pw, ph);
}

static void
popup_finalize_destroy(barny_popup_t *p)
{
	if (p->frame_cb) {
		wl_callback_destroy(p->frame_cb);
		p->frame_cb = NULL;
	}
	if (p->snapshot) {
		cairo_surface_destroy(p->snapshot);
		p->snapshot = NULL;
	}

	popup_teardown_buffer(p);

	if (p->layer_surface) {
		zwlr_layer_surface_v1_destroy(p->layer_surface);
		p->layer_surface = NULL;
	}
	if (p->surface) {
		wl_surface_destroy(p->surface);
		p->surface = NULL;
	}
	p->configured = false;
	free(p);
}

static double
ease_out_cubic(double t)
{
	double u = 1.0 - t;
	return 1.0 - u * u * u;
}

static double
ease_in_cubic(double t)
{
	return t * t * t;
}

static void popup_schedule_frame(barny_popup_t *p);

static void
popup_animate(barny_popup_t *p)
{
	uint64_t now = barny_now_ms();
	double   dur;
	double   t;
	double   e;
	double   alpha;
	double   scale;

	if (p->anim == POPUP_ANIM_CLOSING) {
		dur   = POPUP_ANIM_CLOSE_MS;
		t     = dur > 0 ? (double)(now - p->anim_start_ms) / dur : 1.0;
		if (t < 0)
			t = 0;
		if (t > 1.0)
			t = 1.0;
		e     = ease_in_cubic(t);
		alpha = 1.0 - e;
		scale = POPUP_ANIM_SCALE + (1.0 - POPUP_ANIM_SCALE) * (1.0 - e);
	} else {
		dur   = POPUP_ANIM_OPEN_MS;
		t     = dur > 0 ? (double)(now - p->anim_start_ms) / dur : 1.0;
		if (t < 0)
			t = 0;
		if (t > 1.0)
			t = 1.0;
		e     = ease_out_cubic(t);
		alpha = e;
		scale = POPUP_ANIM_SCALE + (1.0 - POPUP_ANIM_SCALE) * e;
	}

	popup_present(p, alpha, scale);

	if (t >= 1.0) {
		if (p->anim == POPUP_ANIM_CLOSING) {
			popup_finalize_destroy(p);
			return;
		}
		p->anim = POPUP_ANIM_OPEN;
		wl_surface_commit(p->surface);
		return;
	}

	popup_schedule_frame(p);
	wl_surface_commit(p->surface);
}

static void
popup_frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	barny_popup_t *p = data;
	(void)time;

	wl_callback_destroy(cb);
	p->frame_cb = NULL;
	popup_animate(p);
}

static const struct wl_callback_listener popup_frame_listener = {
	.done = popup_frame_done,
};

static void
popup_schedule_frame(barny_popup_t *p)
{
	if (p->frame_cb)
		return;
	p->frame_cb = wl_surface_frame(p->surface);
	wl_callback_add_listener(p->frame_cb, &popup_frame_listener, p);
}

static void
popup_paint(barny_popup_t *p)
{
	if (!p->cr || !p->buffer)
		return;

	popup_present(p, 1.0, 1.0);
	wl_surface_commit(p->surface);
}

static void
popup_teardown_buffer(barny_popup_t *p)
{
	if (p->cr) {
		cairo_destroy(p->cr);
		p->cr = NULL;
	}
	if (p->cairo_surface) {
		cairo_surface_destroy(p->cairo_surface);
		p->cairo_surface = NULL;
	}
	if (p->buffer) {
		wl_buffer_destroy(p->buffer);
		p->buffer = NULL;
	}
	if (p->shm_data) {
		munmap(p->shm_data, (size_t)p->shm_size);
		p->shm_data = NULL;
	}
	p->shm_size = 0;
}

static void
popup_layer_configure(void *userdata, struct zwlr_layer_surface_v1 *surface,
                      uint32_t serial, uint32_t width, uint32_t height)
{
	barny_popup_t      *p = userdata;
	int                 pw, ph, stride, size, fd;
	struct wl_shm_pool *pool;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	if (p->buffer)
		popup_teardown_buffer(p);

	pw     = (int)width > 0 ? (int)width : popup_compute_width(p);
	ph     = (int)height > 0 ? (int)height : popup_compute_height(p);
	stride = pw * 4;
	size   = stride * ph;

	fd     = popup_create_shm(size);
	if (fd < 0)
		return;

	p->shm_data = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
	                   MAP_SHARED, fd, 0);
	if (p->shm_data == MAP_FAILED) {
		close(fd);
		p->shm_data = NULL;
		return;
	}
	p->shm_size = size;

	pool        = wl_shm_create_pool(p->state->shm, fd, size);
	p->buffer   = wl_shm_pool_create_buffer(pool, 0, pw, ph, stride,
	                                        WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	p->cairo_surface = cairo_image_surface_create_for_data(
	        p->shm_data, CAIRO_FORMAT_ARGB32, pw, ph, stride);
	p->cr         = cairo_create(p->cairo_surface);
	p->current_w  = pw;
	p->current_h  = ph;
	p->configured = true;

	if (!p->state->config.popup_animations) {
		popup_paint(p);
	} else if (p->anim == POPUP_ANIM_NONE) {
		p->anim          = POPUP_ANIM_OPENING;
		p->anim_start_ms = barny_now_ms();
		popup_animate(p);
	} else if (p->anim == POPUP_ANIM_OPEN) {
		popup_paint(p);
	} else {
		popup_animate(p);
	}
}

static void
popup_layer_closed(void *userdata, struct zwlr_layer_surface_v1 *surface)
{
	barny_popup_t *p = userdata;
	(void)surface;
	p->configured = false;
}

static const struct zwlr_layer_surface_v1_listener popup_layer_listener = {
	.configure = popup_layer_configure,
	.closed    = popup_layer_closed,
};

barny_popup_t *
barny_popup_create(barny_state_t *state, barny_module_t *owner,
                   const barny_popup_callbacks_t *cb, int gap_px)
{
	barny_popup_t    *p;
	barny_output_t   *out;
	uint32_t          anchor;
	int               pw, ph;
	int               left_margin;
	int               center_off;
	int               top_margin    = 0;
	int               bottom_margin = 0;
	int               reserved;
	struct wl_region *empty;

	if (!state || !owner || !cb)
		return NULL;

	out = state->pointer_output;
	if (!out)
		return NULL;

	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	p->state   = state;
	p->owner   = owner;
	p->cb      = *cb;
	p->gap_px  = gap_px;

	pw         = popup_compute_width(p);
	ph         = popup_compute_height(p);

	p->surface = wl_compositor_create_surface(state->compositor);
	if (!p->surface) {
		free(p);
		return NULL;
	}

	empty = wl_compositor_create_region(state->compositor);
	wl_surface_set_input_region(p->surface, empty);
	wl_region_destroy(empty);

	p->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
	        state->layer_shell, p->surface, out->wl_output,
	        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "barny-popup");
	if (!p->layer_surface) {
		wl_surface_destroy(p->surface);
		p->surface = NULL;
		free(p);
		return NULL;
	}

	zwlr_layer_surface_v1_add_listener(p->layer_surface,
	                                   &popup_layer_listener, p);

	anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
	if (state->config.position_top)
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	else
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

	zwlr_layer_surface_v1_set_anchor(p->layer_surface, anchor);
	zwlr_layer_surface_v1_set_size(p->layer_surface, pw, ph);
	zwlr_layer_surface_v1_set_exclusive_zone(p->layer_surface, 0);

	left_margin  = (state->config.margin_left - out->pad_left) + owner->render_x;
	center_off   = (pw - owner->width) / 2;
	left_margin -= center_off;
	if (left_margin < 0)
		left_margin = 0;

	if (state->config.position_top)
		top_margin = gap_px;
	else
		bottom_margin = gap_px;

	zwlr_layer_surface_v1_set_margin(p->layer_surface, top_margin, 0,
	                                 bottom_margin, left_margin);

	p->screen_x = left_margin;

	reserved    = state->config.height
	              + (state->config.position_top ? state->config.margin_top : state->config.margin_bottom);
	if (state->config.position_top)
		p->screen_y = reserved + gap_px;
	else
		p->screen_y = out->height - reserved - gap_px - ph;

	wl_surface_commit(p->surface);

	return p;
}

void
barny_popup_destroy(barny_popup_t *p)
{
	cairo_t *sc;

	if (!p)
		return;

	if (!p->state->config.popup_animations || !p->configured || !p->buffer
	    || p->anim == POPUP_ANIM_CLOSING) {
		popup_finalize_destroy(p);
		return;
	}

	if (!p->snapshot && p->cairo_surface) {
		p->snapshot = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, p->current_w, p->current_h);
		if (cairo_surface_status(p->snapshot) == CAIRO_STATUS_SUCCESS) {
			sc = cairo_create(p->snapshot);
			cairo_set_source_surface(sc, p->cairo_surface, 0, 0);
			cairo_paint(sc);
			cairo_destroy(sc);
		} else {
			cairo_surface_destroy(p->snapshot);
			p->snapshot = NULL;
		}
	}

	if (!p->snapshot) {
		popup_finalize_destroy(p);
		return;
	}

	p->anim          = POPUP_ANIM_CLOSING;
	p->anim_start_ms = barny_now_ms();
	if (!p->frame_cb)
		popup_animate(p);
}

void
barny_popup_redraw(barny_popup_t *p)
{
	if (!p || !p->configured)
		return;
	if (p->anim == POPUP_ANIM_OPENING || p->anim == POPUP_ANIM_CLOSING)
		return;
	popup_paint(p);
}

bool
barny_popup_visible(const barny_popup_t *p)
{
	return p && p->configured && p->buffer != NULL;
}

void
barny_popup_draw_row(cairo_t *cr, PangoLayout *layout, int row_y, int line_h,
                     int width, const char *label, const char *value,
                     double lr, double lg, double lb, double vr, double vg,
                     double vb, double val_alpha)
{
	int tw;
	int th;
	int ty;
	int vx;

	pango_layout_set_text(layout, label, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	ty = row_y + (line_h - th) / 2;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_move_to(cr, 1, ty + 1);
	pango_cairo_show_layout(cr, layout);
	cairo_set_source_rgba(cr, lr, lg, lb, 0.9);
	cairo_move_to(cr, 0, ty);
	pango_cairo_show_layout(cr, layout);

	pango_layout_set_text(layout, value, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	ty = row_y + (line_h - th) / 2;
	vx = width - tw;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
	cairo_move_to(cr, vx + 1, ty + 1);
	pango_cairo_show_layout(cr, layout);
	cairo_set_source_rgba(cr, vr, vg, vb, val_alpha);
	cairo_move_to(cr, vx, ty);
	pango_cairo_show_layout(cr, layout);
}

PangoFontDescription *
barny_popup_font_from(const char *font, const char *fallback)
{
	PangoFontDescription *fd;
	int                   base;

	fd   = pango_font_description_from_string(font ? font : fallback);
	base = pango_font_description_get_size(fd);
	if (base > 0)
		pango_font_description_set_size(fd, base * 85 / 100);
	else
		pango_font_description_set_size(fd, 9 * PANGO_SCALE);

	return fd;
}

int
barny_popup_measure_text(PangoFontDescription *font_desc, const char *text)
{
	static cairo_surface_t *s_surf   = NULL;
	static cairo_t         *s_cr     = NULL;
	static PangoLayout     *s_layout = NULL;
	int                     w        = 0;
	int                     h        = 0;

	if (!font_desc || !text || !*text)
		return 0;

	if (!s_layout) {
		s_surf   = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
		s_cr     = cairo_create(s_surf);
		s_layout = pango_cairo_create_layout(s_cr);
	}
	pango_layout_set_font_description(s_layout, font_desc);
	pango_layout_set_text(s_layout, text, -1);
	pango_layout_get_pixel_size(s_layout, &w, &h);
	return w;
}
