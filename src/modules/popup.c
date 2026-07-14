#include <fcntl.h>
#include <math.h>
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
#define POPUP_RADIUS        BARNY_POPUP_RADIUS

/* Liquid morph: the popup is born as a droplet squeezing out of the bar edge
   and springs open into the data window; closing runs the morph backwards. */
#define POPUP_MORPH_K_OPEN     200.0 /* open spring stiffness, s^-2 */
#define POPUP_MORPH_ZETA_OPEN  0.86  /* slight jelly overshoot */
#define POPUP_MORPH_K_CLOSE    430.0
#define POPUP_MORPH_ZETA_CLOSE 1.0
#define POPUP_DT_MAX           0.05

#define POPUP_BLOB_W     96.0 /* droplet size at birth */
#define POPUP_BLOB_H     40.0
#define POPUP_NECK_SMIN  14.0 /* surface-tension fillet toward the bar */
#define POPUP_REFRACT    1.7  /* droplet refraction, fades as it opens */
#define POPUP_CHROMA     7.0
#define POPUP_VEIL       0.08
#define POPUP_VEIL_FADE  7.0
#define POPUP_RIM_W      5.0
#define POPUP_RIM_STR    0.28
#define POPUP_RIM_CORE   0.50
#define POPUP_RIM_CHROMA 0.28
#define POPUP_SPEC_LX    (-0.30)
#define POPUP_SPEC_LY    (-0.954)
#define POPUP_SPEC_W     7.0
#define POPUP_SPEC_P     10.0
#define POPUP_SPEC_GAIN  0.55
#define POPUP_CONTENT_IN 0.68 /* morph point where the data fades in */

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

	int                           body_w; /* the data window inside the surface */
	int                           body_h;
	int                           body_y;
	int                           neck_h; /* rows bridging toward the bar edge */

	enum popup_anim               anim;
	double                        morph; /* 0 = droplet in the bar, 1 = window */
	double                        morph_v;
	uint64_t                      last_ms;
	struct wl_callback           *frame_cb;
	cairo_surface_t              *glass_src;     /* wallpaper + broad lighting */
	cairo_surface_t              *content_cache; /* rendered data rows */
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
popup_panel(const barny_popup_t *p, barny_glass_panel_t *panel)
{
	panel->w            = p->current_w;
	panel->h            = p->current_h;
	panel->body_w       = p->body_w;
	panel->body_h       = p->body_h;
	panel->body_y       = p->body_y;
	panel->anchor_x     = p->current_w / 2; /* the popup hangs centred on its module */
	panel->glass_x      = p->screen_x;
	panel->glass_y      = p->screen_y;
	panel->position_top = p->state->config.position_top;
}

static void
popup_compose(barny_popup_t *p)
{
	barny_glass_panel_t panel;

	popup_panel(p, &panel);
	barny_glass_panel_compose(p->cr, p->state, p->state->pointer_output,
	                          &panel, p->content_cache);
}

static void
popup_build_glass(barny_popup_t *p)
{
	barny_glass_panel_t panel;

	if (p->glass_src) {
		cairo_surface_destroy(p->glass_src);
		p->glass_src = NULL;
	}

	popup_panel(p, &panel);
	p->glass_src = barny_glass_panel_bg(p->state, p->state->pointer_output,
	                                    &panel);
}

static void
popup_build_content(barny_popup_t *p)
{
	cairo_t *cc;

	if (p->content_cache) {
		cairo_surface_destroy(p->content_cache);
		p->content_cache = NULL;
	}

	p->content_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                              p->body_w, p->body_h);
	if (cairo_surface_status(p->content_cache) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(p->content_cache);
		p->content_cache = NULL;
		return;
	}

	if (!p->cb.render)
		return;

	cc = cairo_create(p->content_cache);
	cairo_translate(cc, BARNY_POPUP_PAD_X, BARNY_POPUP_PAD_Y);
	p->cb.render(p->cb.userdata, cc, p->body_w - 2 * BARNY_POPUP_PAD_X,
	             p->body_h - 2 * BARNY_POPUP_PAD_Y);
	cairo_destroy(cc);
}

/* Draw the panel as a liquid-glass blob at morph position m: a droplet
   squeezing out of the bar edge (m=0) that stretches through a
   surface-tension neck and opens into the data window (m=1). Shape comes
   from a round-rect SDF smooth-unioned with the bar half-plane; shading
   (refraction, veil, lit rim, specular) matches the bar lens and fades out
   as the window opens, leaving only the frame edge lighting. */
void
barny_glass_panel_morph(cairo_t *cr, const barny_glass_panel_t *panel,
                        cairo_surface_t *bg, cairo_surface_t *content, double m)
{
	bool             top    = panel->position_top;
	int              w      = panel->w;
	int              h      = panel->h;
	double           mg     = m < 0.0 ? 0.0 : (m > 1.02 ? 1.02 : m);
	double           inv    = 1.0 - mg > 0.0 ? 1.0 - mg : 0.0;
	double           bw0    = fmin(w * 0.5, POPUP_BLOB_W);
	double           bh0    = fmin(panel->body_h * 0.8, POPUP_BLOB_H);
	double           hw0    = bw0 / 2.0;
	double           hh0    = bh0 / 2.0;
	double           r0     = fmin(hw0, hh0);
	double           cy0    = top ? -bh0 * 0.15 : (double)h + bh0 * 0.15;
	double           cx0    = fmin(fmax((double)panel->anchor_x, hw0), w - hw0);
	double           hw     = hw0 + (panel->body_w / 2.0 - hw0) * mg;
	double           hh     = hh0 + (panel->body_h / 2.0 - hh0) * mg;
	double           ccx    = cx0 + (panel->body_w / 2.0 - cx0) * mg;
	double           ccy    = cy0 + (panel->body_y + panel->body_h / 2.0 - cy0) * mg;
	double           rr     = r0 + (POPUP_RADIUS - r0) * mg;
	double           smk    = POPUP_NECK_SMIN * (1.0 - 0.6 * mg);
	double           disp   = POPUP_REFRACT * inv;
	double           chroma = POPUP_CHROMA * inv;
	double           edge_h = BARNY_FRAME_EDGE_TOP_STOP * 2.0 * hh;
	double           shad_h = fmin(10.0, 0.28 * 2.0 * hh);
	double           ca;
	cairo_surface_t *patch;
	uint8_t         *gdata;
	uint8_t         *ddata;
	int              gstride;
	int              dstride;
	int              gsw;
	int              gsh;
	int              x;
	int              y;

	if (!bg)
		return;
	if (rr > hw)
		rr = hw;
	if (rr > hh)
		rr = hh;

	patch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(patch) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(patch);
		return;
	}

	cairo_surface_flush(bg);
	gdata   = cairo_image_surface_get_data(bg);
	gstride = cairo_image_surface_get_stride(bg);
	gsw     = cairo_image_surface_get_width(bg);
	gsh     = cairo_image_surface_get_height(bg);
	ddata   = cairo_image_surface_get_data(patch);
	dstride = cairo_image_surface_get_stride(patch);

	for (y = 0; y < h; y++) {
		uint8_t *drow = ddata + y * dstride;
		double   ly   = y + 0.5;

		for (x = 0; x < w; x++) {
			double  lx = x + 0.5;
			double  d;
			double  dl;
			double  dr;
			double  du;
			double  dv;
			double  nnx   = 0.0;
			double  nny   = 0.0;
			double  nlen;
			double  aa;
			double  depth;
			double  pedge = 0.0;
			double  dispx = 0.0;
			double  dispy = 0.0;
			double  fr;
			double  fg;
			double  fb;
			double  fa;
			double  wtop  = 0.0;
			double  wbot  = 0.0;
			double  atop  = 0.0;
			double  abot  = 0.0;
			double  aw;
			double  veil;
			double  ri;
			double  core;
			double  rim;
			double  ty;
			double  facing;
			double  lit;
			double  band;
			double  spec;
			double  cntr;
			double  ap;
			uint8_t p1[4];
			uint8_t p2[4];
			uint8_t p3[4];

#define POPUP_SDF(px, py)                                                     \
	barny_smin(top ? (py) : (double)h - (py),                             \
	           barny_sd_round_rect((px) - ccx, (py) - ccy, hw, hh, rr),  \
	           smk)

			d = POPUP_SDF(lx, ly);
			aa = 0.5 - d;
			if (aa <= 0.0) {
				drow[x * 4 + 0] = 0;
				drow[x * 4 + 1] = 0;
				drow[x * 4 + 2] = 0;
				drow[x * 4 + 3] = 0;
				continue;
			}
			if (aa > 1.0)
				aa = 1.0;

			dl   = POPUP_SDF(lx - 1.0, ly);
			dr   = POPUP_SDF(lx + 1.0, ly);
			du   = POPUP_SDF(lx, ly - 1.0);
			dv   = POPUP_SDF(lx, ly + 1.0);
			nlen = sqrt((dr - dl) * (dr - dl) + (dv - du) * (dv - du));
			if (nlen > 1e-6) {
				nnx = (dr - dl) / nlen;
				nny = (dv - du) / nlen;
			}
			depth = -d;

			if (d < 0.0 && disp > 0.01) {
				double zone = hh;
				double pp   = zone > 0.0 ? depth / zone : 1.0;
				double srcoff;

				if (pp > 1.0)
					pp = 1.0;
				srcoff = zone * (sqrt(2.0 * pp - pp * pp) - pp)
				         * disp
				         + 3.0 * (1.0 - pp) * inv;
				dispx  = -nnx * srcoff;
				dispy  = -nny * srcoff;
				pedge  = (1.0 - pp) * (1.0 - pp);
			}

			if (chroma > 0.01) {
				double cs = chroma * pedge;

				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      lx + dispx - nnx * cs,
				                      ly + dispy - nny * cs, p1);
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      lx + dispx, ly + dispy,
				                      p2);
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      lx + dispx + nnx * cs,
				                      ly + dispy + nny * cs, p3);
				fb = p3[3] > 0 ? p3[0] * 255.0 / p3[3] : 0.0;
				fg = p2[3] > 0 ? p2[1] * 255.0 / p2[3] : 0.0;
				fr = p1[3] > 0 ? p1[2] * 255.0 / p1[3] : 0.0;
				fa = p2[3] / 255.0;
			} else {
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      lx + dispx, ly + dispy,
				                      p2);
				fb = p2[3] > 0 ? p2[0] * 255.0 / p2[3] : 0.0;
				fg = p2[3] > 0 ? p2[1] * 255.0 / p2[3] : 0.0;
				fr = p2[3] > 0 ? p2[2] * 255.0 / p2[3] : 0.0;
				fa = p2[3] / 255.0;
			}

			wtop = nny < 0.0 ? -nny : 0.0;
			wbot = nny > 0.0 ? nny : 0.0;
			if (depth < edge_h && edge_h > 0.0)
				atop = BARNY_FRAME_EDGE_TOP_A
				       * (1.0 - depth / edge_h) * wtop;
			if (depth < shad_h && shad_h > 0.0)
				abot = BARNY_FRAME_EDGE_BOT_A
				       * (1.0 - depth / shad_h) * wbot;

			veil = depth / POPUP_VEIL_FADE;
			if (veil < 0.0)
				veil = 0.0;
			if (veil > 1.0)
				veil = 1.0;
			veil *= POPUP_VEIL * inv;
			aw    = veil + atop - veil * atop;

			facing = nnx * POPUP_SPEC_LX + nny * POPUP_SPEC_LY;
			lit    = 0.45 + 0.55 * (facing > 0.15 ? facing : 0.15);
			ri     = 1.0 - depth / POPUP_RIM_W;
			if (ri < 0.0 || d > 0.0)
				ri = 0.0;
			ri   = ri * ri;
			core = 1.0 - fabs(d) / 1.4;
			if (core < 0.0)
				core = 0.0;
			rim = (POPUP_RIM_STR * ri + POPUP_RIM_CORE * core * lit)
			      * inv;

			spec = 0.0;
			cntr = 0.0;
			if (d < 0.0 && depth < POPUP_SPEC_W && inv > 0.001) {
				band = 1.0 - depth / POPUP_SPEC_W;
				band = band * band * (3.0 - 2.0 * band);
				if (facing > 0.0)
					spec = POPUP_SPEC_GAIN * inv * band
					       * pow(facing, POPUP_SPEC_P);
				else
					cntr = 0.22 * POPUP_SPEC_GAIN * inv
					       * band
					       * pow(-facing, POPUP_SPEC_P);
			}

			ty = hh > 0.0 ? (ly - ccy) / hh : 0.0;
			if (ty < -1.0)
				ty = -1.0;
			if (ty > 1.0)
				ty = 1.0;

			fr  = fr * (1.0 - aw) + 255.0 * aw;
			fg  = fg * (1.0 - aw) + 255.0 * aw;
			fb  = fb * (1.0 - aw) + 255.0 * aw;
			fr *= 1.0 - abot;
			fg *= 1.0 - abot;
			fb *= 1.0 - abot;
			fr += (rim * (1.0 - POPUP_RIM_CHROMA * ty) + spec
			       + cntr * 0.85)
			      * 255.0;
			fg += (rim + spec + cntr * 0.92) * 255.0;
			fb += (rim * (1.0 + POPUP_RIM_CHROMA * ty) + spec
			       + cntr)
			      * 255.0;
			if (fr > 255.0)
				fr = 255.0;
			if (fg > 255.0)
				fg = 255.0;
			if (fb > 255.0)
				fb = 255.0;

			ap              = fa * aa;
			drow[x * 4 + 0] = (uint8_t)(fb * ap);
			drow[x * 4 + 1] = (uint8_t)(fg * ap);
			drow[x * 4 + 2] = (uint8_t)(fr * ap);
			drow[x * 4 + 3] = (uint8_t)(255.0 * ap);
#undef POPUP_SDF
		}
	}

	cairo_surface_mark_dirty(patch);
	cairo_set_source_surface(cr, patch, 0, 0);
	cairo_paint(cr);
	cairo_surface_destroy(patch);

	/* the data window condenses inside the blob near the end of the morph */
	ca = (m - POPUP_CONTENT_IN) / (1.0 - POPUP_CONTENT_IN + 0.02);
	if (ca > 1.0)
		ca = 1.0;
	if (ca > 0.003 && content) {
		cairo_save(cr);
		barny_rounded_rect_path(cr, ccx - hw, ccy - hh, 2.0 * hw,
		                        2.0 * hh, rr);
		cairo_clip(cr);
		cairo_translate(cr, ccx, ccy);
		cairo_scale(cr, 2.0 * hw / panel->body_w, 2.0 * hh / panel->body_h);
		cairo_translate(cr, -panel->body_w / 2.0, -panel->body_h / 2.0);
		cairo_set_source_surface(cr, content, 0, 0);
		cairo_paint_with_alpha(cr, ca);
		cairo_restore(cr);
	}
}

int
barny_glass_panel_glass_x(const barny_config_t *cfg, int output_x)
{
	return output_x - cfg->margin_left;
}

int
barny_glass_panel_glass_y(const barny_config_t *cfg, int bar_h, int panel_h)
{
	int reserved = barny_config_exclusive_zone(cfg);

	if (cfg->position_top)
		return reserved;

	return bar_h - reserved - panel_h;
}

cairo_surface_t *
barny_glass_panel_bg(barny_state_t *state, barny_output_t *out,
                     const barny_glass_panel_t *panel)
{
	cairo_surface_t *bg    = NULL;
	cairo_surface_t *src;
	cairo_t         *sc;
	int              out_w = 0;
	int              out_h = 0;

	if (out) {
		bg    = state->displaced_wallpaper ? state->displaced_wallpaper
		                                   : state->blurred_wallpaper;
		out_w = out->width;
		out_h = out->height;
	}

	src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, panel->w, panel->h);
	if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(src);
		return NULL;
	}

	sc = cairo_create(src);
	barny_paint_glass_bg(sc, bg, out_w, out_h, panel->glass_x, panel->glass_y,
	                     panel->h, panel->position_top);
	cairo_save(sc);
	cairo_rectangle(sc, 0, panel->body_y, panel->body_w, panel->body_h);
	cairo_clip(sc);
	cairo_translate(sc, 0, panel->body_y);
	barny_draw_broad_frame(sc, panel->body_w, panel->body_h);
	cairo_restore(sc);
	cairo_destroy(sc);

	return src;
}

void
barny_glass_panel_compose(cairo_t *cr, barny_state_t *state,
                          barny_output_t *out, const barny_glass_panel_t *panel,
                          cairo_surface_t *content)
{
	cairo_surface_t *bg    = NULL;
	int              out_w = 0;
	int              out_h = 0;

	if (out) {
		bg    = state->displaced_wallpaper ? state->displaced_wallpaper
		                                   : state->blurred_wallpaper;
		out_w = out->width;
		out_h = out->height;
	}

	cairo_save(cr);
	cairo_translate(cr, 0, panel->body_y);
	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, panel->body_w, panel->body_h, POPUP_RADIUS);
	cairo_clip(cr);
	barny_paint_glass_bg(cr, bg, out_w, out_h, panel->glass_x,
	                     panel->glass_y + panel->body_y, panel->body_h,
	                     panel->position_top);
	cairo_restore(cr);

	barny_draw_glass_frame(cr, panel->body_w, panel->body_h, POPUP_RADIUS);

	if (content) {
		cairo_set_source_surface(cr, content, 0, 0);
		cairo_paint(cr);
	}
	cairo_restore(cr);
}

/* One spring drives every panel's open and close morph. */
bool
barny_glass_panel_step(double *morph, double *vel, uint64_t *last_ms,
                       bool closing)
{
	uint64_t now     = barny_now_ms();
	double   dt      = (double)(now - *last_ms) / 1000.0;
	double   target  = closing ? 0.0 : 1.0;
	double   k       = closing ? POPUP_MORPH_K_CLOSE : POPUP_MORPH_K_OPEN;
	double   z       = closing ? POPUP_MORPH_ZETA_CLOSE
	                           : POPUP_MORPH_ZETA_OPEN;
	double   c       = 2.0 * z * sqrt(k);
	bool     settled;

	*last_ms = now;
	if (dt < 0.0)
		dt = 0.0;
	if (dt > POPUP_DT_MAX)
		dt = POPUP_DT_MAX;

	*vel   += (k * (target - *morph) - c * *vel) * dt;
	*morph += *vel * dt;

	settled = fabs(target - *morph) < 0.004 && fabs(*vel) < 0.08;
	if (settled) {
		*morph = target;
		*vel   = 0.0;
	}

	return settled;
}

static void
popup_present(barny_popup_t *p, double m)
{
	cairo_t            *cr = p->cr;
	barny_glass_panel_t panel;

	if (!cr || !p->buffer)
		return;

	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	if (p->state->config.popup_animations) {
		popup_panel(p, &panel);
		barny_glass_panel_morph(cr, &panel, p->glass_src,
		                        p->content_cache, m);
	} else {
		popup_compose(p);
	}

	cairo_surface_flush(p->cairo_surface);
	wl_surface_attach(p->surface, p->buffer, 0, 0);
	wl_surface_damage_buffer(p->surface, 0, 0, p->current_w, p->current_h);
}

static void
popup_finalize_destroy(barny_popup_t *p)
{
	if (p->frame_cb) {
		wl_callback_destroy(p->frame_cb);
		p->frame_cb = NULL;
	}
	if (p->glass_src) {
		cairo_surface_destroy(p->glass_src);
		p->glass_src = NULL;
	}
	if (p->content_cache) {
		cairo_surface_destroy(p->content_cache);
		p->content_cache = NULL;
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

static void popup_schedule_frame(barny_popup_t *p);

static void
popup_animate(barny_popup_t *p)
{
	bool closing = p->anim == POPUP_ANIM_CLOSING;
	bool settled;

	settled = barny_glass_panel_step(&p->morph, &p->morph_v, &p->last_ms,
	                                 closing);

	popup_present(p, p->morph);

	if (settled) {
		if (closing) {
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

	popup_present(p, 1.0);
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
	ph     = (int)height > 0 ? (int)height
	                         : popup_compute_height(p) + p->neck_h;
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
	p->cr        = cairo_create(p->cairo_surface);
	p->current_w = pw;
	p->current_h = ph;
	p->body_w    = pw;
	p->body_h    = ph - p->neck_h;
	p->body_y    = p->state->config.position_top ? p->neck_h : 0;
	if (p->body_h < 1) {
		p->body_h = ph;
		p->body_y = 0;
	}
	p->configured = true;

	popup_build_glass(p);
	if (p->anim != POPUP_ANIM_CLOSING)
		popup_build_content(p);

	if (!p->state->config.popup_animations || p->anim == POPUP_ANIM_OPEN) {
		popup_paint(p);
		return;
	}

	if (p->anim == POPUP_ANIM_NONE) {
		p->anim    = POPUP_ANIM_OPENING;
		p->morph   = 0.0;
		p->morph_v = 0.0;
		p->last_ms = barny_now_ms();
	}

	popup_animate(p);
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
	int               owner_x;
	int               owner_w;
	int               left_margin;
	int               center_off;
	struct wl_region *empty;

	if (!state || !owner || !cb)
		return NULL;

	out = state->pointer_output;
	if (!out)
		return NULL;

	/* the popup hangs under the module as it is laid out on the pointer's
	   output; without a rect there the module is not on screen here */
	if (!barny_output_module_rect(out, owner, &owner_x, &owner_w))
		return NULL;

	p = calloc(1, sizeof(*p));
	if (!p)
		return NULL;

	p->state   = state;
	p->owner   = owner;
	p->cb      = *cb;
	p->gap_px  = gap_px;
	p->neck_h  = gap_px > 0 ? gap_px : 0;

	pw         = popup_compute_width(p);
	ph         = popup_compute_height(p) + p->neck_h;

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

	left_margin  = (state->config.margin_left - out->pad_left) + owner_x;
	center_off   = (pw - owner_w) / 2;
	left_margin -= center_off;
	if (left_margin < 0)
		left_margin = 0;

	/* flush against the bar edge: the neck rows at the bar side of the
	   surface carry the liquid bridge, so no gap margin is needed */
	zwlr_layer_surface_v1_set_margin(p->layer_surface, 0, 0, 0,
	                                 left_margin);

	p->screen_x = barny_glass_panel_glass_x(&state->config, left_margin);
	p->screen_y = barny_glass_panel_glass_y(&state->config, out->height, ph);

	wl_surface_commit(p->surface);

	return p;
}

void
barny_popup_destroy(barny_popup_t *p)
{
	if (!p)
		return;

	/* the caches carry the visuals through the close morph, so the owner's
	   callbacks are never touched after this point */
	if (!p->state->config.popup_animations || !p->configured || !p->buffer
	    || !p->glass_src || p->anim == POPUP_ANIM_CLOSING) {
		popup_finalize_destroy(p);
		return;
	}

	p->anim    = POPUP_ANIM_CLOSING;
	p->last_ms = barny_now_ms();
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
	popup_build_content(p);
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
