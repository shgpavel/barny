#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "popup.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define POPUP_WIDTH_DEFAULT 180
#define POPUP_RADIUS        12

struct barny_popup {
	barny_state_t          *state;
	barny_module_t         *owner;
	barny_popup_callbacks_t cb;
	int                     gap_px;

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

static void
rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
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
popup_render_frame(barny_popup_t *p)
{
	if (!p->cr)
		return;

	barny_state_t *state = p->state;
	cairo_t       *cr    = p->cr;
	int            pw    = p->current_w;
	int            ph    = p->current_h;

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_save(cr);
	rounded_rect(cr, 0, 0, pw, ph, POPUP_RADIUS);
	cairo_clip(cr);

	cairo_surface_t *bg_surface = state->displaced_wallpaper
	                                      ? state->displaced_wallpaper
	                                      : state->blurred_wallpaper;

	if (bg_surface && state->pointer_output) {
		int out_w = state->pointer_output->width;
		int out_h = state->pointer_output->height;
		int wp_w  = cairo_image_surface_get_width(bg_surface);
		int wp_h  = cairo_image_surface_get_height(bg_surface);

		double scale_x = (double)wp_w / out_w;
		double scale_y = (double)wp_h / out_h;
		double scale   = scale_x < scale_y ? scale_x : scale_y;
		int    src_y_off
		        = state->config.position_top
		                  ? 0
		                  : (wp_h - (int)(out_h * scale));

		cairo_scale(cr, 1.0 / scale, 1.0 / scale);
		cairo_set_source_surface(
		        cr, bg_surface,
		        -p->screen_x * scale,
		        -(p->screen_y * scale + src_y_off));
		cairo_paint(cr);
	} else {
		cairo_pattern_t *bg = cairo_pattern_create_linear(0, 0, 0, ph);
		cairo_pattern_add_color_stop_rgba(bg, 0, 0.15, 0.15, 0.18, 0.85);
		cairo_pattern_add_color_stop_rgba(bg, 1, 0.08, 0.08, 0.10, 0.85);
		cairo_set_source(cr, bg);
		cairo_paint(cr);
		cairo_pattern_destroy(bg);
	}

	cairo_restore(cr);

	rounded_rect(cr, 0.5, 0.5, pw - 1, ph - 1, POPUP_RADIUS);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	rounded_rect(cr, 1.5, 1.5, pw - 3, ph - 3, POPUP_RADIUS - 1);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	cairo_save(cr);
	rounded_rect(cr, 0, 0, pw, ph, POPUP_RADIUS);
	cairo_clip(cr);
	{
		cairo_pattern_t *hl
		        = cairo_pattern_create_linear(0, 0, pw * 0.7, ph * 0.7);
		cairo_pattern_add_color_stop_rgba(hl, 0.0, 1, 1, 1, 0.15);
		cairo_pattern_add_color_stop_rgba(hl, 0.3, 1, 1, 1, 0.04);
		cairo_pattern_add_color_stop_rgba(hl, 1.0, 1, 1, 1, 0);
		cairo_set_source(cr, hl);
		cairo_paint(cr);
		cairo_pattern_destroy(hl);
	}
	{
		cairo_pattern_t *sh = cairo_pattern_create_linear(
		        pw * 0.3, ph * 0.3, pw, ph);
		cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(sh, 0.7, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.15);
		cairo_set_source(cr, sh);
		cairo_paint(cr);
		cairo_pattern_destroy(sh);
	}
	{
		cairo_pattern_t *top_r = cairo_pattern_create_linear(0, 0, 0, 8);
		cairo_pattern_add_color_stop_rgba(top_r, 0.0, 1, 1, 1, 0.08);
		cairo_pattern_add_color_stop_rgba(top_r, 1.0, 1, 1, 1, 0);
		cairo_set_source(cr, top_r);
		cairo_rectangle(cr, 0, 0, pw, 8);
		cairo_fill(cr);
		cairo_pattern_destroy(top_r);

		cairo_pattern_t *left_r = cairo_pattern_create_linear(0, 0, 8, 0);
		cairo_pattern_add_color_stop_rgba(left_r, 0.0, 1, 1, 1, 0.06);
		cairo_pattern_add_color_stop_rgba(left_r, 1.0, 1, 1, 1, 0);
		cairo_set_source(cr, left_r);
		cairo_rectangle(cr, 0, 0, 8, ph);
		cairo_fill(cr);
		cairo_pattern_destroy(left_r);
	}
	cairo_restore(cr);
}

static void
popup_paint(barny_popup_t *p)
{
	if (!p->cr || !p->buffer)
		return;

	popup_render_frame(p);

	if (p->cb.render) {
		int content_w = p->current_w - 2 * BARNY_POPUP_PAD_X;
		int content_h = p->current_h - 2 * BARNY_POPUP_PAD_Y;
		cairo_save(p->cr);
		cairo_translate(p->cr, BARNY_POPUP_PAD_X, BARNY_POPUP_PAD_Y);
		p->cb.render(p->cb.userdata, p->cr, content_w, content_h);
		cairo_restore(p->cr);
	}

	cairo_surface_flush(p->cairo_surface);
	wl_surface_attach(p->surface, p->buffer, 0, 0);
	wl_surface_damage_buffer(p->surface, 0, 0, p->current_w, p->current_h);
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
	barny_popup_t *p = userdata;
	int            pw, ph, stride, size, fd;
	struct wl_shm_pool *pool;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	/* Tear down only the buffer chain (not the layer surface) before
	 * re-allocating; compositor may send configure multiple times. */
	if (p->buffer)
		popup_teardown_buffer(p);

	pw     = (int)width  > 0 ? (int)width  : popup_compute_width(p);
	ph     = (int)height > 0 ? (int)height : popup_compute_height(p);
	stride = pw * 4;
	size   = stride * ph;

	fd = popup_create_shm(size);
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

	pool = wl_shm_create_pool(p->state->shm, fd, size);
	p->buffer = wl_shm_pool_create_buffer(pool, 0, pw, ph, stride,
	                                      WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	p->cairo_surface = cairo_image_surface_create_for_data(
	        p->shm_data, CAIRO_FORMAT_ARGB32, pw, ph, stride);
	p->cr            = cairo_create(p->cairo_surface);
	p->current_w     = pw;
	p->current_h     = ph;
	p->configured    = true;

	popup_paint(p);
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
	barny_popup_t  *p;
	barny_output_t *out;
	uint32_t        anchor;
	int             pw, ph;
	int             left_margin;
	int             center_off;
	int             top_margin    = 0;
	int             bottom_margin = 0;

	if (!state || !owner || !cb)
		return NULL;

	out = state->pointer_output;
	if (!out)
		return NULL;

	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	p->state  = state;
	p->owner  = owner;
	p->cb     = *cb;
	p->gap_px = gap_px;

	pw = popup_compute_width(p);
	ph = popup_compute_height(p);

	p->surface = wl_compositor_create_surface(state->compositor);
	if (!p->surface) {
		free(p);
		return NULL;
	}

	struct wl_region *empty
	        = wl_compositor_create_region(state->compositor);
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

	left_margin = state->config.margin_left + owner->render_x;
	center_off  = (pw - owner->width) / 2;
	left_margin -= center_off;
	if (left_margin < 0)
		left_margin = 0;

	/* Bar has exclusive_zone = height, so the compositor already pushes
	 * non-exclusive surfaces past the bar. Margin here is just the
	 * additional gap between popup and bar edge. */
	if (state->config.position_top)
		top_margin = gap_px;
	else
		bottom_margin = gap_px;

	zwlr_layer_surface_v1_set_margin(p->layer_surface, top_margin, 0,
	                                 bottom_margin, left_margin);

	p->screen_x = left_margin;
	/* Compositor reserves bar+margin via exclusive_zone, then pushes
	 * popup further by gap_px. Mirror that for wallpaper sampling. */
	int reserved = state->config.height
	             + (state->config.position_top
	                        ? state->config.margin_top
	                        : state->config.margin_bottom);
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
	if (!p)
		return;

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

void
barny_popup_redraw(barny_popup_t *p)
{
	if (!p || !p->configured)
		return;
	popup_paint(p);
}

bool
barny_popup_visible(const barny_popup_t *p)
{
	return p && p->configured && p->buffer != NULL;
}

int
barny_popup_measure_text(PangoFontDescription *font_desc, const char *text)
{
	if (!font_desc || !text || !*text)
		return 0;

	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1,
	                                                   1);
	cairo_t         *cr   = cairo_create(surf);
	PangoLayout     *layout = pango_cairo_create_layout(cr);
	int              w = 0;
	int              h = 0;

	pango_layout_set_font_description(layout, font_desc);
	pango_layout_set_text(layout, text, -1);
	pango_layout_get_pixel_size(layout, &w, &h);

	g_object_unref(layout);
	cairo_destroy(cr);
	cairo_surface_destroy(surf);
	return w;
}
