#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "util.h"

static bool
is_gap_placeholder(const barny_module_t *mod)
{
	return mod
	       && mod->name
	       && strcmp(mod->name, "gap") == 0
	       && mod->render == NULL;
}

static int
section_total(barny_state_t *state, const int *idx, int n, int spacing,
              bool include_gaps, const bool *dropped)
{
	int             sum = 0, visible = 0;
	int             i;
	int             j;
	barny_module_t *m;
	int             w;

	for (i = 0; i < n; i++) {
		j = idx[i];
		if (dropped[j])
			continue;
		m = state->modules[j];
		if (!m)
			continue;
		if (is_gap_placeholder(m) && !include_gaps)
			continue;
		w    = m->width > 0 ? m->width : 0;
		sum += w;
		visible++;
	}

	if (visible > 1)
		sum += spacing * (visible - 1);

	return sum;
}

static bool
sections_fit(int width, int pad_l, int pad_r, int spacing,
             int total_l, int total_c, int total_r)
{
	int left_end     = pad_l + total_l;
	int right_start  = (width - pad_r) - total_r;
	int center_start = (width / 2) - total_c / 2;
	int center_end   = center_start + total_c;

	if (total_c <= 0) {
		if (total_l <= 0 || total_r <= 0)
			return left_end <= right_start;
		return left_end + spacing <= right_start;
	}

	if (total_l > 0 && left_end + spacing > center_start)
		return false;
	if (total_l <= 0 && left_end > center_start)
		return false;
	if (total_r > 0 && center_end + spacing > right_start)
		return false;
	if (total_r <= 0 && center_end > right_start)
		return false;

	return true;
}

static int
last_visible_real(barny_state_t *state, const int *idx, int n,
                  const bool *dropped)
{
	int             i;
	int             j;
	barny_module_t *m;

	for (i = n - 1; i >= 0; i--) {
		j = idx[i];
		if (dropped[j])
			continue;
		m = state->modules[j];
		if (!m || is_gap_placeholder(m))
			continue;
		return j;
	}

	return -1;
}

static int
first_visible_real(barny_state_t *state, const int *idx, int n,
                   const bool *dropped)
{
	int             i;
	int             j;
	barny_module_t *m;

	for (i = 0; i < n; i++) {
		j = idx[i];
		if (dropped[j])
			continue;
		m = state->modules[j];
		if (!m || is_gap_placeholder(m))
			continue;
		return j;
	}

	return -1;
}

static int
pick_victim(barny_state_t *state,
            const int *left, int lc, const int *center, int cc,
            const int *right, int rc,
            int width, int pad_l, int pad_r, int spacing,
            int total_l, int total_c, int total_r,
            const bool *dropped)
{
	int left_end     = pad_l + total_l;
	int right_start  = (width - pad_r) - total_r;
	int center_start = (width / 2) - total_c / 2;
	int center_end   = center_start + total_c;

	int slack_lc = INT_MAX, slack_cr = INT_MAX, slack_lr = INT_MAX;
	int gap_l;
	int gap_r;
	int worst;
	int collision;
	int v;

	if (total_c > 0) {
		gap_l    = (total_l > 0) ? spacing : 0;
		gap_r    = (total_r > 0) ? spacing : 0;
		slack_lc = center_start - (left_end + gap_l);
		slack_cr = right_start - (center_end + gap_r);
	}
	if (total_c <= 0 && total_l > 0 && total_r > 0)
		slack_lr = right_start - (left_end + spacing);

	worst     = slack_lc;
	collision = 0;
	if (slack_cr < worst) {
		worst     = slack_cr;
		collision = 1;
	}
	if (slack_lr < worst) {
		worst     = slack_lr;
		collision = 2;
	}

	if (worst >= 0)
		return -1;

	switch (collision) {
	case 0:
		v = last_visible_real(state, left, lc, dropped);
		if (v >= 0)
			return v;
		return first_visible_real(state, center, cc, dropped);
	case 1:
		v = first_visible_real(state, right, rc, dropped);
		if (v >= 0)
			return v;
		return last_visible_real(state, center, cc, dropped);
	default:
		v = last_visible_real(state, left, lc, dropped);
		if (v >= 0)
			return v;
		return first_visible_real(state, right, rc, dropped);
	}
}

/* Bleed allowance around a module's box: the text shadow sits a pixel out and
   antialiasing frays the pill edges. */
#define MODULE_BLEED 8

static bool
module_visible(const barny_output_t *output, int x, int y, int w, int h)
{
	const barny_rect_t *c = &output->clip;

	if (!output->render_clipped)
		return true;
	if (barny_rect_empty(*c))
		return false;

	return x + w + MODULE_BLEED > c->x
	       && x - MODULE_BLEED < c->x + c->w
	       && y + h + MODULE_BLEED > c->y
	       && y - MODULE_BLEED < c->y + c->h;
}

static void
render_section(barny_output_t *output, cairo_t *cr, const int *idx, int n,
               int start_x, int base_y, int avail_h, int spacing,
               bool include_gaps, const bool *dropped)
{
	barny_state_t  *state = output->state;
	int             x     = start_x;
	int             i;
	int             j;
	barny_module_t *mod;
	int             w;
	int             h;
	int             y;

	for (i = 0; i < n; i++) {
		j   = idx[i];
		mod = state->modules[j];
		if (!mod)
			continue;

		if (is_gap_placeholder(mod) && !include_gaps)
			continue;

		if (dropped[j]) {
			/* rendered off-surface purely so the module still
			   measures itself; a clipped frame changes no widths,
			   so the shaping is wasted there */
			if (mod->render && !output->render_clipped) {
				h = mod->height > 0 ? mod->height : avail_h;
				cairo_save(cr);
				cairo_new_path(cr);
				cairo_rectangle(cr, 0, 0, 0, 0);
				cairo_clip(cr);
				mod->render(mod, cr, 0, 0, 0, h);
				cairo_restore(cr);
			}
			continue;
		}

		w                = mod->width > 0 ? mod->width : 0;
		h                = mod->height > 0 ? mod->height : avail_h;
		y                = base_y + (avail_h - h) / 2;

		output->mod_x[j] = x;
		output->mod_w[j] = w;

		if (mod->render && module_visible(output, x, y, w, h)) {
			cairo_save(cr);
			mod->render(mod, cr, x, y, w, h);
			cairo_restore(cr);
		}

		x += w + spacing;
	}
}

/* Union of the strip the droplet covers now and the one it covered last frame:
   everything the bar surface needs re-derived, and nothing else. Clamped to the
   surface. Empty (w == 0) when there is no droplet on either frame. */
static barny_rect_t
lens_damage(const barny_output_t *output, barny_rect_t lens)
{
	return barny_rect_clamp(barny_rect_union(lens, output->lens_dmg),
	                        output->surf_width, output->surf_height);
}

/* A droplet-only frame may repaint just the droplet's strip: the rest of the
   bar is already correct in the (single, persistent) shm buffer. Anything that
   can change pixels elsewhere -- a dirty module, a rebuilt bar cache, the
   droplet living on another output -- forces the full path. */
static bool
lens_partial_ok(barny_output_t *output)
{
	barny_state_t *state = output->state;

	return output->lens_dmg_valid
	       && output->bg_cache
	       && output == state->dyn_output
	       && !barny_modules_any_dirty(state);
}

static unsigned int
rim_snap(barny_output_t *output, const barny_rect_t *rects, int n,
         uint8_t **buf, bool cmp)
{
	cairo_surface_t *surf = output->cairo_surface;
	uint8_t         *data;
	uint8_t         *p;
	size_t           len  = 0;
	size_t           rlen;
	unsigned int     diff = 0;
	int              s    = output->scale;
	int              bw;
	int              bh;
	int              st;
	int              i;
	int              y;
	barny_rect_t     r;

	cairo_surface_flush(surf);
	data = cairo_image_surface_get_data(surf);
	bw   = cairo_image_surface_get_width(surf);
	bh   = cairo_image_surface_get_height(surf);
	st   = cairo_image_surface_get_stride(surf);
	if (!data)
		return 0;

	for (i = 0; i < n; i++) {
		r = barny_rect_clamp((barny_rect_t){ rects[i].x * s,
		                                     rects[i].y * s,
		                                     rects[i].w * s,
		                                     rects[i].h * s },
		                     bw, bh);
		len += (size_t)r.w * 4 * (size_t)r.h;
	}
	if (!cmp) {
		*buf = malloc(len ? len : 1);
		if (!*buf)
			return 0;
	}

	p = *buf;
	for (i = 0; i < n; i++) {
		r    = barny_rect_clamp((barny_rect_t){ rects[i].x * s,
		                                        rects[i].y * s,
		                                        rects[i].w * s,
		                                        rects[i].h * s },
		                        bw, bh);
		rlen = (size_t)r.w * 4;
		for (y = r.y; y < r.y + r.h; y++) {
			if (!cmp)
				memcpy(p, data + y * st + r.x * 4, rlen);
			else if (memcmp(p, data + y * st + r.x * 4, rlen) != 0)
				diff |= 1u << i;
			p += rlen;
		}
	}

	return diff;
}

static void
render_full(barny_output_t *output, cairo_t *cr)
{
	barny_rect_t rims[4];
	uint8_t     *snap = NULL;
	int          n;

	barny_render_liquid_glass(output, cr);

	n = barny_rim_rects(output, rims);
	if (n > 0)
		rim_snap(output, rims, n, &snap, false);

	barny_render_modules(output, cr);

	if (snap) {
		output->rim_mods_touch = rim_snap(output, rims, n, &snap, true);
		free(snap);
	}
}

static void
render_bar(barny_output_t *output, cairo_t *cr, const barny_rect_t *clip,
           int n, const barny_rect_t *mods)
{
	int i;

	if (n == 0) {
		render_full(output, cr);
		return;
	}

	/* Clip first: the clear, the bar-cache blit and the droplet all
	   land inside the clip rects, and the modules outside them skip
	   their text shaping entirely. */
	cairo_save(cr);
	for (i = 0; i < n; i++)
		cairo_rectangle(cr, clip[i].x, clip[i].y, clip[i].w, clip[i].h);
	cairo_clip(cr);

	output->render_clipped = true;
	output->clip           = *mods;

	barny_render_liquid_glass(output, cr);
	barny_render_modules(output, cr);

	output->render_clipped = false;
	cairo_restore(cr);
}

static bool
rim_partial_ok(barny_output_t *output)
{
	barny_state_t *state = output->state;

	return output->lens_dmg_valid
	       && output->bg_cache
	       && (output == state->dyn_output
	           || barny_rect_empty(output->lens_dmg))
	       && !barny_modules_any_dirty(state);
}

static void
module_widths_save(const barny_state_t *state, int *widths)
{
	int i;

	for (i = 0; i < state->module_count; i++) {
		widths[i] = state->modules[i] ? state->modules[i]->width : 0;
	}
}

static bool
module_widths_changed(const barny_state_t *state, const int *widths)
{
	int i;
	int w;

	for (i = 0; i < state->module_count; i++) {
		w = state->modules[i] ? state->modules[i]->width : 0;
		if (w != widths[i])
			return true;
	}

	return false;
}

static void
modules_clear_dirty(barny_state_t *state)
{
	int i;

	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = false;
		}
	}
}

static void
frame_commit(barny_output_t *output, const barny_rect_t *dmg, int n)
{
	int s = output->scale;
	int i;

	barny_output_request_frame(output);

	cairo_surface_flush(output->cairo_surface);
	wl_surface_attach(output->surface, output->buffer, 0, 0);
	if (n == 0) {
		wl_surface_damage_buffer(output->surface, 0, 0,
		                         output->surf_width * s,
		                         output->surf_height * s);
	}
	for (i = 0; i < n; i++) {
		wl_surface_damage_buffer(output->surface, dmg[i].x * s,
		                         dmg[i].y * s, dmg[i].w * s,
		                         dmg[i].h * s);
	}
	wl_surface_commit(output->surface);
}

void
barny_render_frame(barny_output_t *output)
{
	cairo_t       *cr;
	barny_state_t *state;
	int            saved_widths[BARNY_MAX_MODULES];
	barny_rect_t   lens;
	barny_rect_t   dmg[5];
	barny_rect_t   mods;
	uint64_t       tick;
	bool           rim_moved;
	bool           partial;
	bool           lens_anim;
	int            n = 0;
	int            k;
	int            i;

	if (!output->configured || !output->cr) {
		return;
	}

	if (output->frame_pending) {
		output->redraw_queued = true;
		return;
	}

	output->redraw_queued = false;

	lens_anim             = barny_lens_step(output);

	cr                    = output->cr;
	state                 = output->state;

	module_widths_save(state, saved_widths);

	tick             = barny_rim_tick(&state->config, barny_now_us());
	rim_moved        = output->rim && output->rim_tick != tick;
	output->rim_tick = tick;

	if (!barny_lens_rect(output, &lens.x, &lens.y, &lens.w, &lens.h))
		lens = (barny_rect_t){ 0 };
	mods = lens_damage(output, lens);
	partial = barny_rect_empty(mods) ? rim_partial_ok(output)
	                                 : lens_partial_ok(output);
	if (partial && !barny_rect_empty(mods))
		dmg[n++] = mods;
	if (partial && rim_moved) {
		k  = n;
		n += barny_rim_rects(output, dmg + n);
		for (i = k; i < n; i++)
			if (output->rim_mods_touch & (1u << (i - k)))
				mods = barny_rect_union(mods, dmg[i]);
	}

	render_bar(output, cr, dmg, n, &mods);

	/* A module resized while drawing, so the layout it was drawn with is
	   stale: redo the bar in full, strip or not. */
	if (module_widths_changed(state, saved_widths)) {
		n = 0;
		render_bar(output, cr, NULL, 0, NULL);
	}

	modules_clear_dirty(state);

	if (lens_anim) {
		output->redraw_queued = true;
	}

	output->lens_dmg       = lens;
	output->lens_dmg_valid = true;

	frame_commit(output, dmg, n);
}

void
barny_render_modules(barny_output_t *output, cairo_t *cr)
{
	barny_state_t  *state  = output->state;
	int             width  = output->width;
	int             height = output->height;
	int             cx0    = output->pad_left;
	int             cy0    = output->pad_top;
	int             left[BARNY_MAX_MODULES];
	int             center[BARNY_MAX_MODULES];
	int             right[BARNY_MAX_MODULES];
	int             lc                         = 0;
	int             cc                         = 0;
	int             rc                         = 0;
	int             spacing                    = state->config.module_spacing;
	int             pad_l                      = 16;
	int             pad_r                      = 16;
	bool            dropped[BARNY_MAX_MODULES] = { false };
	bool            include_gaps               = true;
	int             total_l, total_c, total_r;
	int             i;
	int             phase;
	barny_module_t *mod;
	int             victim;
	int             center_start;
	int             right_start;

	for (i = 0; i < BARNY_MAX_MODULES; i++) {
		output->mod_x[i] = -1;
		output->mod_w[i] = 0;
	}

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (!mod)
			continue;

		switch (mod->position) {
		case BARNY_POS_LEFT:
			left[lc++] = i;
			break;
		case BARNY_POS_CENTER:
			center[cc++] = i;
			break;
		case BARNY_POS_RIGHT:
			right[rc++] = i;
			break;
		}
	}

	for (phase = 0; phase < 2; phase++) {
		include_gaps = (phase == 0);

		for (;;) {
			total_l = section_total(state, left, lc, spacing,
			                        include_gaps, dropped);
			total_c = section_total(state, center, cc, spacing,
			                        include_gaps, dropped);
			total_r = section_total(state, right, rc, spacing,
			                        include_gaps, dropped);

			if (sections_fit(width, pad_l, pad_r, spacing, total_l,
			                 total_c, total_r))
				goto layout_ready;

			if (phase == 0)
				break;

			victim = pick_victim(state, left, lc, center, cc, right,
			                     rc, width, pad_l, pad_r, spacing,
			                     total_l, total_c, total_r, dropped);
			if (victim < 0)
				goto layout_ready;
			dropped[victim] = true;
		}
	}

layout_ready:
	render_section(output, cr, left, lc, cx0 + pad_l, cy0, height, spacing,
	               include_gaps, dropped);

	center_start = (width / 2) - total_c / 2;
	if (center_start < pad_l)
		center_start = pad_l;
	render_section(output, cr, center, cc, cx0 + center_start, cy0, height,
	               spacing, include_gaps, dropped);

	right_start = (width - pad_r) - total_r;
	if (right_start < pad_l)
		right_start = pad_l;
	render_section(output, cr, right, rc, cx0 + right_start, cy0, height,
	               spacing, include_gaps, dropped);
}
