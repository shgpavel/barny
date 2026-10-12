#include <fcntl.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "glass_shade.h"
#include "popup.h"
#include "util.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define POPUP_WIDTH_DEFAULT 180
#define POPUP_RADIUS        BARNY_POPUP_RADIUS

/* Liquid morph: the popup is born as a droplet squeezing out of the bar edge
   and springs open into the data window; closing runs the morph backwards. */
#define POPUP_MORPH_K_OPEN     150.0 /* responsive without flashing content */
#define POPUP_MORPH_ZETA_OPEN  0.86  /* soft landing, restrained overshoot */
#define POPUP_MORPH_K_CLOSE    340.0 /* quicker than open, still readable */
#define POPUP_MORPH_ZETA_CLOSE 1.0

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
#define POPUP_SPEC_W     7.0
#define POPUP_SPEC_GAIN  0.55
#define POPUP_CONTENT_IN 0.68 /* panel forms before the data condenses in */

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
	uint64_t                      last_us;
	struct wl_callback           *frame_cb;
	cairo_surface_t              *glass_src;     /* wallpaper + broad lighting */
	cairo_surface_t              *content_cache; /* rendered data rows */
	cairo_surface_t              *rest_glass;
	barny_output_t               *rest_out;
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
popup_build_glass(barny_popup_t *p)
{
	barny_glass_panel_t panel;

	if (p->glass_src) {
		cairo_surface_destroy(p->glass_src);
		p->glass_src = NULL;
	}
	if (p->rest_glass) {
		cairo_surface_destroy(p->rest_glass);
		p->rest_glass = NULL;
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

/* One scratch patch serves every panel: only one is ever being rasterised at a
   time, and every pixel of it is written each pass, so it never needs clearing.
   A fresh surface per frame was the morph's only allocation. */
static cairo_surface_t *s_patch;
static int              s_patch_w;
static int              s_patch_h;

static double
popup_phase(double p, double start, double end)
{
	double t = (p - start) / (end - start);

	if (t < 0.0)
		t = 0.0;
	if (t > 1.0)
		t = 1.0;
	return glass_smoothstep01(t);
}

static double
popup_phase_out(double p, double start, double end)
{
	double t = (p - start) / (end - start);

	if (t < 0.0)
		t = 0.0;
	if (t > 1.0)
		t = 1.0;
	return t * (2.0 - t);
}

static cairo_surface_t *
panel_patch(int w, int h)
{
	if (s_patch && s_patch_w == w && s_patch_h == h)
		return s_patch;

	if (s_patch)
		cairo_surface_destroy(s_patch);

	s_patch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(s_patch) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s_patch);
		s_patch = NULL;
		return NULL;
	}

	s_patch_w = w;
	s_patch_h = h;

	return s_patch;
}

typedef struct {
	double p;
	double inv;
	double hw;
	double hh;
	double ccx;
	double ccy;
	double rr;
	double smk;
} panel_shape_t;

static void
panel_shape(const barny_glass_panel_t *panel, double m, panel_shape_t *s)
{
	bool   top    = panel->position_top;
	int    w      = panel->w;
	int    h      = panel->h;
	double p      = m < 0.0 ? 0.0 : (m > 1.0 ? 1.0 : m);
	double over   = m > 1.0 ? fmin(m - 1.0, 0.02) : 0.0;
	/* Birth leads, then travel and panel growth overlap it. Keeping these
	   phases separate makes the glass visibly emerge from the bar instead of
	   replacing one prebuilt silhouette with another. */
	double birth  = popup_phase_out(p, 0.0, 0.22);
	double travel = popup_phase_out(p, 0.08, 0.94) + over;
	double grow_h = popup_phase_out(p, 0.14, 0.90) + over;
	double grow_w = popup_phase(p, 0.20, 1.0) + over;
	double finish = popup_phase(p, 0.0, 1.0) + over;
	double bw0    = fmin(w * 0.5, POPUP_BLOB_W);
	double bh0    = fmin(panel->body_h * 0.8, POPUP_BLOB_H);
	double hw0    = bw0 / 2.0;
	double hh0    = bh0 / 2.0;
	double r0     = fmin(hw0, hh0);
	double edge_y = top ? 0.0 : (double)h;
	double seed_x = fmin(fmax((double)panel->anchor_x, 0.0), (double)w);
	double cy0    = top ? -bh0 * 0.15 : (double)h + bh0 * 0.15;
	double cx0    = fmin(fmax((double)panel->anchor_x, hw0), w - hw0);

	s->p   = p;
	s->inv = 1.0 - finish > 0.0 ? 1.0 - finish : 0.0;
	s->hw  = hw0 * birth + (panel->body_w / 2.0 - hw0) * grow_w;
	s->hh  = hh0 * birth + (panel->body_h / 2.0 - hh0) * grow_h;
	s->ccx = seed_x + (cx0 - seed_x) * birth
	         + (panel->body_w / 2.0 - cx0) * travel;
	s->ccy = edge_y + (cy0 - edge_y) * birth
	         + (panel->body_y + panel->body_h / 2.0 - cy0) * travel;
	s->rr  = r0 * birth + (POPUP_RADIUS - r0) * grow_w;
	s->smk = POPUP_NECK_SMIN * birth * (1.0 - 0.6 * travel);
	if (s->rr > s->hw)
		s->rr = s->hw;
	if (s->rr > s->hh)
		s->rr = s->hh;
}

static double                   *s_field;
static int                       s_field_n;
static const cairo_user_data_key_t s_opaque_key;
static const int                 s_opaque_yes = 1;
static const int                 s_opaque_no  = 0;

static double *
field_rows(int n)
{
	double *f;

	if (s_field && s_field_n >= n)
		return s_field;

	f = realloc(s_field, (size_t)n * 3 * sizeof(*f));
	if (!f)
		return NULL;
	memset(f, 0, (size_t)n * 3 * sizeof(*f));
	s_field   = f;
	s_field_n = n;

	return f;
}

static bool
src_opaque(cairo_surface_t *src)
{
	const int *tag;
	uint8_t   *data;
	int        width;
	int        height;
	int        stride;
	int        x;
	int        y;
	bool       opaque = true;

	tag = cairo_surface_get_user_data(src, &s_opaque_key);
	if (tag)
		return *tag;

	data   = cairo_image_surface_get_data(src);
	width  = cairo_image_surface_get_width(src);
	height = cairo_image_surface_get_height(src);
	stride = cairo_image_surface_get_stride(src);
	if (!data || cairo_image_surface_get_format(src) != CAIRO_FORMAT_ARGB32)
		opaque = false;
	for (y = 0; opaque && y < height; y++) {
		const uint8_t *row = data + (ptrdiff_t)y * stride;

		for (x = 0; x < width; x++) {
			if (row[x * 4 + 3] != 255) {
				opaque = false;
				break;
			}
		}
	}

	cairo_surface_set_user_data(src, &s_opaque_key,
	                            (void *)(opaque ? &s_opaque_yes
	                                            : &s_opaque_no),
	                            NULL);

	return opaque;
}

static void
panel_morph_fill(cairo_surface_t *patch, cairo_surface_t *bg,
                 const barny_glass_panel_t *panel, const panel_shape_t *s)
{
	bool     top    = panel->position_top;
	int      w      = panel->w;
	int      h      = panel->h;
	double   inv    = s->inv;
	double   hw     = s->hw;
	double   hh     = s->hh;
	double   ccx    = s->ccx;
	double   ccy    = s->ccy;
	double   rr     = s->rr;
	double   smk    = s->smk;
	double   disp   = POPUP_REFRACT * inv;
	double   chroma = POPUP_CHROMA * inv;
	double   edge_h = BARNY_FRAME_EDGE_TOP_STOP * 2.0 * hh;
	double   ext    = 1.5 + 1.25 * (smk > 0.0 ? smk : 0.0);
	double  *field;
	double  *fprev;
	double  *fcur;
	double  *fnext;
	double  *ftmp;
	uint8_t *gdata;
	uint8_t *ddata;
	bool     opaque;
	int      gstride;
	int      dstride;
	int      gsw;
	int      gsh;
	int      bx0;
	int      bx1;
	int      by0;
	int      by1;
	int      n;
	int      c;
	int      x;
	int      y;

	cairo_surface_flush(bg);
	gdata   = cairo_image_surface_get_data(bg);
	gstride = cairo_image_surface_get_stride(bg);
	gsw     = cairo_image_surface_get_width(bg);
	gsh     = cairo_image_surface_get_height(bg);
	ddata   = cairo_image_surface_get_data(patch);
	dstride = cairo_image_surface_get_stride(patch);
	opaque  = src_opaque(bg);

	bx0 = (int)fmax(0.0, fmin((double)w, floor(ccx - hw - ext)));
	bx1 = (int)fmax(0.0, fmin((double)w, ceil(ccx + hw + ext)));
	by0 = (int)fmax(0.0, fmin((double)h, floor(ccy - hh - ext)));
	by1 = (int)fmax(0.0, fmin((double)h, ceil(ccy + hh + ext)));
	n   = bx1 - bx0 + 2;
	field = bx1 > bx0 && by1 > by0 ? field_rows(n) : NULL;
	if (!field) {
		bx0 = 0;
		bx1 = 0;
		by0 = 0;
		by1 = 0;
	}

	for (y = 0; y < h; y++) {
		if (y < by0 || y >= by1) {
			memset(ddata + (ptrdiff_t)y * dstride, 0, (size_t)w * 4);
			continue;
		}
		memset(ddata + (ptrdiff_t)y * dstride, 0, (size_t)bx0 * 4);
		memset(ddata + (ptrdiff_t)y * dstride + bx1 * 4, 0,
		       (size_t)(w - bx1) * 4);
	}
	if (!field) {
		cairo_surface_mark_dirty(patch);
		return;
	}

#define POPUP_SDF(px, py)                                                     \
	barny_smin(top ? (py) : (double)h - (py),                             \
	           barny_sd_round_rect((px) - ccx, (py) - ccy, hw, hh, rr),  \
	           smk)
#define POPUP_ROW(f, yy)                                                      \
	for (c = 0; c < n; c++)                                               \
		(f)[c] = POPUP_SDF((bx0 - 1 + c) + 0.5, (yy) + 0.5)

	fprev = field;
	fcur  = field + n;
	fnext = field + 2 * n;
	POPUP_ROW(fprev, by0 - 1);
	POPUP_ROW(fcur, by0);

	for (y = by0; y < by1; y++) {
		uint8_t *drow = ddata + y * dstride;
		double   ly   = y + 0.5;

		POPUP_ROW(fnext, y + 1);

		for (x = bx0; x < bx1; x++) {
			int     ci = x - bx0 + 1;
			double  d;
			double  dl;
			double  dr;
			double  du;
			double  dv;
			double  nnx   = 0.0;
			double  nny   = 0.0;
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
			double  atop  = 0.0;
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

			/* The smooth union flattens the field where the neck
			   meets the bar, so the raw distance there covers far
			   more than a pixel: shading it by d alone smeared that
			   edge. Normalising by the local gradient puts a true
			   one-pixel ramp on the whole silhouette. |grad| <= 1
			   for this field, so d >= 0.5 is still safely outside. */
			d = fcur[ci];
			if (d >= 0.5) {
				drow[x * 4 + 0] = 0;
				drow[x * 4 + 1] = 0;
				drow[x * 4 + 2] = 0;
				drow[x * 4 + 3] = 0;
				continue;
			}

			dl = fcur[ci - 1];
			dr = fcur[ci + 1];
			du = fprev[ci];
			dv = fnext[ci];
			if (!glass_sdf_normal_aa(d, dl, dr, du, dv, &nnx, &nny,
			                         &aa)) {
				drow[x * 4 + 0] = 0;
				drow[x * 4 + 1] = 0;
				drow[x * 4 + 2] = 0;
				drow[x * 4 + 3] = 0;
				continue;
			}
			depth = -d;

			if (d < 0.0 && disp > 0.01) {
				double zone = hh;
				double pp   = zone > 0.0 ? depth / zone : 1.0;
				double srcoff;

				if (pp > 1.0)
					pp = 1.0;
				srcoff = glass_dome(pp, zone) * disp
				         + 3.0 * (1.0 - pp) * inv;
				dispx  = -nnx * srcoff;
				dispy  = -nny * srcoff;
				pedge  = (1.0 - pp) * (1.0 - pp);
			}

			if (chroma > 0.01 && opaque) {
				double cs = chroma * pedge;

				fb = barny_sample_bilinear_ch(gdata, gstride, gsw,
				                              gsh,
				                              x + dispx + nnx * cs,
				                              y + dispy + nny * cs,
				                              0);
				fg = barny_sample_bilinear_ch(gdata, gstride, gsw,
				                              gsh, x + dispx,
				                              y + dispy, 1);
				fr = barny_sample_bilinear_ch(gdata, gstride, gsw,
				                              gsh,
				                              x + dispx - nnx * cs,
				                              y + dispy - nny * cs,
				                              2);
				fa = 1.0;
			} else if (chroma > 0.01) {
				double cs = chroma * pedge;

				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      x + dispx - nnx * cs,
				                      y + dispy - nny * cs, p1);
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      x + dispx, y + dispy,
				                      p2);
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      x + dispx + nnx * cs,
				                      y + dispy + nny * cs, p3);
				fb = p3[3] > 0 ? p3[0] * 255.0 / p3[3] : 0.0;
				fg = p2[3] > 0 ? p2[1] * 255.0 / p2[3] : 0.0;
				fr = p1[3] > 0 ? p1[2] * 255.0 / p1[3] : 0.0;
				fa = p2[3] / 255.0;
			} else if (opaque) {
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      x + dispx, y + dispy,
				                      p2);
				fb = p2[0];
				fg = p2[1];
				fr = p2[2];
				fa = 1.0;
			} else {
				barny_sample_bilinear(gdata, gstride, gsw, gsh,
				                      x + dispx, y + dispy,
				                      p2);
				fb = p2[3] > 0 ? p2[0] * 255.0 / p2[3] : 0.0;
				fg = p2[3] > 0 ? p2[1] * 255.0 / p2[3] : 0.0;
				fr = p2[3] > 0 ? p2[2] * 255.0 / p2[3] : 0.0;
				fa = p2[3] / 255.0;
			}

			wtop = nny < 0.0 ? -nny : 0.0;
			if (depth < edge_h && edge_h > 0.0)
				atop = BARNY_FRAME_EDGE_TOP_A
				       * (1.0 - depth / edge_h) * wtop;

			veil = depth / POPUP_VEIL_FADE;
			if (veil < 0.0)
				veil = 0.0;
			if (veil > 1.0)
				veil = 1.0;
			veil *= POPUP_VEIL * inv;
			aw    = veil + atop - veil * atop;

			facing = nnx * GLASS_SPEC_LX + nny * GLASS_SPEC_LY;
			lit    = glass_lit(facing);
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
				band = glass_smoothstep01(band);
				if (facing > 0.0)
					spec = POPUP_SPEC_GAIN * inv * band
					       * glass_pow10(facing);
				else
					cntr = 0.22 * POPUP_SPEC_GAIN * inv
					       * band * glass_pow10(-facing);
			}

			ty = hh > 0.0 ? (ly - ccy) / hh : 0.0;
			if (ty < -1.0)
				ty = -1.0;
			if (ty > 1.0)
				ty = 1.0;

			glass_tint(&fr, &fg, &fb, aw, rim, ty, spec, cntr);
			ap = fa * aa;
			glass_put(drow + x * 4, fr, fg, fb, ap);
		}

		ftmp  = fprev;
		fprev = fcur;
		fcur  = fnext;
		fnext = ftmp;
	}
#undef POPUP_ROW
#undef POPUP_SDF

	cairo_surface_mark_dirty(patch);
}

static void
panel_morph_content(cairo_t *cr, const barny_glass_panel_t *panel,
                    const panel_shape_t *s, cairo_surface_t *content)
{
	double ca;

	/* the data window condenses inside the blob near the end of the morph */
	ca = popup_phase(s->p, POPUP_CONTENT_IN, 1.0);
	if (ca > 0.003 && content) {
		cairo_save(cr);
		barny_rounded_rect_path(cr, s->ccx - s->hw, s->ccy - s->hh,
		                        2.0 * s->hw, 2.0 * s->hh, s->rr);
		cairo_clip(cr);
		cairo_translate(cr, s->ccx, s->ccy);
		cairo_scale(cr, 2.0 * s->hw / panel->body_w,
		            2.0 * s->hh / panel->body_h);
		cairo_translate(cr, -panel->body_w / 2.0, -panel->body_h / 2.0);
		cairo_set_source_surface(cr, content, 0, 0);
		cairo_paint_with_alpha(cr, ca);
		cairo_restore(cr);
	}
}

/* Draw the panel as a liquid-glass blob at morph position m: a zero-size seed
   at the bar edge (m=0) inflates into an attached droplet, stretches through a
   surface-tension neck, then opens into the data window (m=1). Shape comes
   from a round-rect SDF smooth-unioned with the bar half-plane; shading
   (refraction, veil, lit rim, specular) matches the bar lens and fades out
   as the window opens, leaving only the frame edge lighting. */
void
barny_glass_panel_morph(cairo_t *cr, const barny_glass_panel_t *panel,
                        cairo_surface_t *bg, cairo_surface_t *content, double m)
{
	panel_shape_t    s;
	cairo_surface_t *patch;

	if (!bg)
		return;

	patch = panel_patch(panel->w, panel->h);
	if (!patch)
		return;

	panel_shape(panel, m, &s);
	panel_morph_fill(patch, bg, panel, &s);
	cairo_set_source_surface(cr, patch, 0, 0);
	cairo_paint(cr);
	/* let go of the shared scratch: the next panel may need it at a
	   different size */
	cairo_set_source_rgba(cr, 0, 0, 0, 0);

	panel_morph_content(cr, panel, &s, content);
}

/* The bead is a shallower droplet than the panel: it sits on glass that is
   already lit, so it refracts gently and leans on its rim to read as raised.
   The dome is kept to a narrow band inside the edge -- a bead that bends light
   all the way through its middle embosses the very label it is meant to
   highlight, and text is far too fine to survive that. */
#define BUBBLE_REFRACT   0.7
#define BUBBLE_ZONE      6.0 /* px the dome reaches in from the edge */
#define BUBBLE_CHROMA    1.0 /* a lens over a glyph fringes it; keep it faint */
#define BUBBLE_VEIL      0.12 /* the wash that marks the row as hovered */
#define BUBBLE_VEIL_FADE 6.0
#define BUBBLE_PAD       6 /* px of slack around the bead for rim and edge */

static cairo_surface_t *s_bub;
static int              s_bub_w;
static int              s_bub_h;

static cairo_surface_t *
bubble_patch(int w, int h)
{
	if (s_bub && s_bub_w == w && s_bub_h == h)
		return s_bub;

	if (s_bub)
		cairo_surface_destroy(s_bub);

	s_bub = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(s_bub) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s_bub);
		s_bub = NULL;
		return NULL;
	}

	s_bub_w = w;
	s_bub_h = h;

	return s_bub;
}

bool
barny_glass_bubble_bounds(const barny_glass_bubble_t *bub, int sw, int sh,
                          int *x0, int *y0, int *x1, int *y1)
{
	if (bub->alpha <= 0.004 || bub->hw < 1.0 || bub->hh < 1.0)
		return false;

	*x0 = (int)floor(bub->cx - bub->hw) - BUBBLE_PAD;
	*y0 = (int)floor(bub->cy - bub->hh) - BUBBLE_PAD;
	*x1 = (int)ceil(bub->cx + bub->hw) + BUBBLE_PAD;
	*y1 = (int)ceil(bub->cy + bub->hh) + BUBBLE_PAD;
	if (*x0 < 0)
		*x0 = 0;
	if (*y0 < 0)
		*y0 = 0;
	if (*x1 > sw)
		*x1 = sw;
	if (*y1 > sh)
		*y1 = sh;

	return *x1 > *x0 && *y1 > *y0;
}

void
barny_glass_bubble_draw(cairo_t *cr, cairo_surface_t *src,
                        const barny_glass_bubble_t *bub)
{
	double           hw = bub->hw;
	double           hh = bub->hh;
	double           rr = bub->radius;
	double           a  = bub->alpha;
	cairo_surface_t *patch;
	uint8_t         *sdata;
	uint8_t         *ddata;
	int              sstride;
	int              dstride;
	int              sw;
	int              sh;
	int              x0;
	int              y0;
	int              x1;
	int              y1;
	int              pw;
	int              ph;
	int              n;
	int              c;
	int              x;
	int              y;
	double          *field;
	double          *fprev;
	double          *fcur;
	double          *fnext;
	double          *ftmp;

	if (!src || a <= 0.004 || hw < 1.0 || hh < 1.0)
		return;
	if (a > 1.0)
		a = 1.0;
	if (rr > hw)
		rr = hw;
	if (rr > hh)
		rr = hh;

	cairo_surface_flush(src);
	sdata   = cairo_image_surface_get_data(src);
	sstride = cairo_image_surface_get_stride(src);
	sw      = cairo_image_surface_get_width(src);
	sh      = cairo_image_surface_get_height(src);
	if (!sdata)
		return;

	if (!barny_glass_bubble_bounds(bub, sw, sh, &x0, &y0, &x1, &y1))
		return;

	pw = x1 - x0;
	ph = y1 - y0;

	patch = bubble_patch(pw, ph);
	if (!patch)
		return;

	n     = pw + 2;
	field = field_rows(n);
	if (!field)
		return;

	ddata   = cairo_image_surface_get_data(patch);
	dstride = cairo_image_surface_get_stride(patch);

#define BUBBLE_SDF(px, py) \
	barny_sd_round_rect((px) - bub->cx, (py) - bub->cy, hw, hh, rr)
#define BUBBLE_ROW(f, yy)                                                     \
	for (c = 0; c < n; c++)                                               \
		(f)[c] = BUBBLE_SDF((x0 - 1 + c) + 0.5, (yy) + 0.5)

	fprev = field;
	fcur  = field + n;
	fnext = field + 2 * n;
	BUBBLE_ROW(fprev, y0 - 1);
	BUBBLE_ROW(fcur, y0);

	for (y = 0; y < ph; y++) {
		uint8_t *drow = ddata + (ptrdiff_t)y * dstride;
		double   ly   = y0 + y + 0.5;

		BUBBLE_ROW(fnext, y0 + y + 1);

		for (x = 0; x < pw; x++) {
			double  d;
			double  dl;
			double  dr;
			double  du;
			double  dv;
			double  nnx = 0.0;
			double  nny = 0.0;
			double  aa;
			double  depth;
			double  zone;
			double  pp;
			double  srcoff;
			double  pedge;
			double  dispx;
			double  dispy;
			double  cs;
			double  fr;
			double  fg;
			double  fb;
			double  veil;
			double  ri;
			double  core;
			double  rim;
			double  ty;
			double  facing;
			double  lit;
			double  band;
			double  spec = 0.0;
			double  ap;
			uint8_t a1;
			uint8_t a2;
			uint8_t a3;
			uint8_t c1;
			uint8_t c2;
			uint8_t c3;

			d = fcur[x + 1];
			if (d >= 0.5) {
				drow[x * 4 + 0] = 0;
				drow[x * 4 + 1] = 0;
				drow[x * 4 + 2] = 0;
				drow[x * 4 + 3] = 0;
				continue;
			}

			dl = fcur[x];
			dr = fcur[x + 2];
			du = fprev[x + 1];
			dv = fnext[x + 1];
			if (!glass_sdf_normal_aa(d, dl, dr, du, dv, &nnx, &nny,
			                         &aa)) {
				drow[x * 4 + 0] = 0;
				drow[x * 4 + 1] = 0;
				drow[x * 4 + 2] = 0;
				drow[x * 4 + 3] = 0;
				continue;
			}

			/* dome of glass: the surface it sits on is pulled in
			   toward the bead's edge, hardest right at the rim,
			   and dead flat by the time it reaches the label */
			depth  = -d;
			zone   = hh < BUBBLE_ZONE ? hh : BUBBLE_ZONE;
			pp     = zone > 0.0 ? depth / zone : 1.0;
			if (pp < 0.0)
				pp = 0.0;
			if (pp > 1.0)
				pp = 1.0;
			srcoff = glass_dome(pp, zone) * BUBBLE_REFRACT;
			dispx  = -nnx * srcoff;
			dispy  = -nny * srcoff;
			pedge  = (1.0 - pp) * (1.0 - pp);
			cs     = BUBBLE_CHROMA * pedge;

			c1 = barny_sample_bilinear_ch(sdata, sstride, sw, sh,
			                              x0 + x + dispx - nnx * cs,
			                              y0 + y + dispy - nny * cs,
			                              2);
			a1 = barny_sample_bilinear_ch(sdata, sstride, sw, sh,
			                              x0 + x + dispx - nnx * cs,
			                              y0 + y + dispy - nny * cs,
			                              3);
			c2 = barny_sample_bilinear_ch(sdata, sstride, sw, sh,
			                              x0 + x + dispx,
			                              y0 + y + dispy, 1);
			a2 = barny_sample_bilinear_ch(sdata, sstride, sw, sh,
			                              x0 + x + dispx,
			                              y0 + y + dispy, 3);
			c3 = barny_sample_bilinear_ch(sdata, sstride, sw, sh,
			                              x0 + x + dispx + nnx * cs,
			                              y0 + y + dispy + nny * cs,
			                              0);
			a3 = barny_sample_bilinear_ch(sdata, sstride, sw, sh,
			                              x0 + x + dispx + nnx * cs,
			                              y0 + y + dispy + nny * cs,
			                              3);
			fb = a3 == 255 ? c3 : (a3 > 0 ? c3 * 255.0 / a3 : 0.0);
			fg = a2 == 255 ? c2 : (a2 > 0 ? c2 * 255.0 / a2 : 0.0);
			fr = a1 == 255 ? c1 : (a1 > 0 ? c1 * 255.0 / a1 : 0.0);

			veil = depth / BUBBLE_VEIL_FADE;
			if (veil < 0.0)
				veil = 0.0;
			if (veil > 1.0)
				veil = 1.0;
			veil *= BUBBLE_VEIL;

			facing = nnx * GLASS_SPEC_LX + nny * GLASS_SPEC_LY;
			lit    = glass_lit(facing);
			ri     = 1.0 - depth / POPUP_RIM_W;
			if (ri < 0.0 || d > 0.0)
				ri = 0.0;
			ri   = ri * ri;
			core = 1.0 - fabs(d) / 1.4;
			if (core < 0.0)
				core = 0.0;
			rim = POPUP_RIM_STR * ri + POPUP_RIM_CORE * core * lit;

			if (d < 0.0 && depth < POPUP_SPEC_W && facing > 0.0) {
				band = 1.0 - depth / POPUP_SPEC_W;
				band = glass_smoothstep01(band);
				spec = POPUP_SPEC_GAIN * band
				       * glass_pow10(facing);
			}

			ty = hh > 0.0 ? (ly - bub->cy) / hh : 0.0;
			if (ty < -1.0)
				ty = -1.0;
			if (ty > 1.0)
				ty = 1.0;

			glass_tint(&fr, &fg, &fb, veil, rim, ty, spec,
			           GLASS_NO_COUNTER);
			ap = aa * a;
			glass_put(drow + x * 4, fr, fg, fb, ap);
		}

		ftmp  = fprev;
		fprev = fcur;
		fcur  = fnext;
		fnext = ftmp;
	}
#undef BUBBLE_ROW
#undef BUBBLE_SDF

	cairo_surface_mark_dirty(patch);
	cairo_set_source_surface(cr, patch, x0, y0);
	cairo_paint(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
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

static void
panel_compose_content(cairo_t *cr, const barny_glass_panel_t *panel,
                      cairo_surface_t *content)
{
	if (!content)
		return;

	cairo_save(cr);
	cairo_translate(cr, 0, panel->body_y);
	cairo_set_source_surface(cr, content, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
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
	cairo_restore(cr);

	panel_compose_content(cr, panel, content);
}

cairo_surface_t *
barny_glass_panel_rest(barny_state_t *state, barny_output_t *out,
                       const barny_glass_panel_t *panel, cairo_surface_t *bg)
{
	bool             morph = state->config.popup_animations;
	panel_shape_t    s;
	cairo_surface_t *rest;
	cairo_t         *rc;

	if (morph && !bg)
		return NULL;

	rest = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, panel->w,
	                                  panel->h);
	if (cairo_surface_status(rest) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(rest);
		return NULL;
	}

	if (morph) {
		panel_shape(panel, 1.0, &s);
		panel_morph_fill(rest, bg, panel, &s);
	} else {
		rc = cairo_create(rest);
		barny_glass_panel_compose(rc, state, out, panel, NULL);
		cairo_destroy(rc);
	}

	return rest;
}

void
barny_glass_panel_rest_content(cairo_t *cr, const barny_state_t *state,
                               const barny_glass_panel_t *panel,
                               cairo_surface_t *content)
{
	panel_shape_t s;

	if (!state->config.popup_animations) {
		panel_compose_content(cr, panel, content);
		return;
	}

	panel_shape(panel, 1.0, &s);
	panel_morph_content(cr, panel, &s, content);
}

/* One spring drives every panel's open and close morph. The analytic step keeps
   its curve stable across refresh rates and compositor hitches. */
bool
barny_glass_panel_advance(double *morph, double *vel, bool closing, double dt)
{
	double target = closing ? 0.0 : 1.0;
	double k      = closing ? POPUP_MORPH_K_CLOSE : POPUP_MORPH_K_OPEN;
	double z      = closing ? POPUP_MORPH_ZETA_CLOSE : POPUP_MORPH_ZETA_OPEN;
	bool   settled;

	barny_spring_step(morph, vel, target, k, z, dt);

	/* Retire the spring's exponential tail: below this the panel is still
	   creeping, but by under a pixel, so the last ~100 ms of it were frames
	   nobody could see. */
	settled = fabs(target - *morph) < 0.006 && fabs(*vel) < 0.10;
	if (settled) {
		*morph = target;
		*vel   = 0.0;
	}

	return settled;
}

bool
barny_glass_panel_step(double *morph, double *vel, uint64_t *last_us,
                       bool closing)
{
	uint64_t now = barny_now_us();
	double   dt  = (double)(now - *last_us) / 1000000.0;

	*last_us = now;
	return barny_glass_panel_advance(morph, vel, closing, dt);
}

static cairo_surface_t *
popup_rest(barny_popup_t *p, const barny_glass_panel_t *panel)
{
	barny_output_t *out = p->state->pointer_output;

	if (p->rest_glass && p->rest_out == out)
		return p->rest_glass;

	if (p->rest_glass)
		cairo_surface_destroy(p->rest_glass);
	p->rest_out   = out;
	p->rest_glass = barny_glass_panel_rest(p->state, out, panel,
	                                       p->glass_src);

	return p->rest_glass;
}

static void
popup_present(barny_popup_t *p, double m)
{
	cairo_t            *cr = p->cr;
	barny_glass_panel_t panel;

	if (!cr || !p->buffer)
		return;

	popup_panel(p, &panel);
	if (m == 1.0 && popup_rest(p, &panel)) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(cr, p->rest_glass, 0, 0);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
		barny_glass_panel_rest_content(cr, p->state, &panel,
		                               p->content_cache);
	} else {
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		if (p->state->config.popup_animations)
			barny_glass_panel_morph(cr, &panel, p->glass_src,
			                        p->content_cache, m);
		else
			barny_glass_panel_compose(cr, p->state,
			                          p->state->pointer_output,
			                          &panel, p->content_cache);
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
	if (p->rest_glass) {
		cairo_surface_destroy(p->rest_glass);
		p->rest_glass = NULL;
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

	settled = barny_glass_panel_step(&p->morph, &p->morph_v, &p->last_us,
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

	fd     = barny_shm_create((size_t)size);
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
		p->last_us = barny_now_us();
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
	p->last_us = barny_now_us();
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
