#include <limits.h>
#include <string.h>

#include "barny.h"

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
module_visible(const barny_output_t *output, int x, int w)
{
	if (!output->render_clipped)
		return true;

	return x + w + MODULE_BLEED > output->clip_x0
	       && x - MODULE_BLEED < output->clip_x1;
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

		if (mod->render && module_visible(output, x, w)) {
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
static void
lens_damage(const barny_output_t *output, bool have_lens, int lx, int ly,
            int lw, int lh, int *dx, int *dy, int *dw, int *dh)
{
	int x0 = INT_MAX;
	int y0 = INT_MAX;
	int x1 = INT_MIN;
	int y1 = INT_MIN;

	if (have_lens) {
		x0 = lx;
		y0 = ly;
		x1 = lx + lw;
		y1 = ly + lh;
	}
	if (output->lens_dmg_w > 0) {
		if (output->lens_dmg_x < x0)
			x0 = output->lens_dmg_x;
		if (output->lens_dmg_y < y0)
			y0 = output->lens_dmg_y;
		if (output->lens_dmg_x + output->lens_dmg_w > x1)
			x1 = output->lens_dmg_x + output->lens_dmg_w;
		if (output->lens_dmg_y + output->lens_dmg_h > y1)
			y1 = output->lens_dmg_y + output->lens_dmg_h;
	}

	if (x1 <= x0 || y1 <= y0) {
		*dx = *dy = *dw = *dh = 0;
		return;
	}

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > output->surf_width)
		x1 = output->surf_width;
	if (y1 > output->surf_height)
		y1 = output->surf_height;

	*dx = x0;
	*dy = y0;
	*dw = x1 > x0 ? x1 - x0 : 0;
	*dh = y1 > y0 ? y1 - y0 : 0;
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

void
barny_render_frame(barny_output_t *output)
{
	cairo_t       *cr;
	barny_state_t *state;
	int            saved_widths[BARNY_MAX_MODULES];
	bool           width_changed = false;
	bool           lens_anim;
	bool           have_lens;
	bool           partial;
	int            lx = 0, ly = 0, lw = 0, lh = 0;
	int            dx = 0, dy = 0, dw = 0, dh = 0;
	int            i;
	int            new_w;

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

	for (i = 0; i < state->module_count; i++) {
		saved_widths[i] = state->modules[i] ? state->modules[i]->width : 0;
	}

	have_lens = barny_lens_rect(output, &lx, &ly, &lw, &lh);
	lens_damage(output, have_lens, lx, ly, lw, lh, &dx, &dy, &dw, &dh);
	partial = lens_partial_ok(output) && dw > 0 && dh > 0;

	if (partial) {
		/* Clip first: the clear, the bar-cache blit and the droplet all
		   land inside the strip, and the modules outside it skip their
		   text shaping entirely. */
		cairo_save(cr);
		cairo_rectangle(cr, dx, dy, dw, dh);
		cairo_clip(cr);

		output->render_clipped = true;
		output->clip_x0        = dx;
		output->clip_x1        = dx + dw;

		barny_render_liquid_glass(output, cr);
		barny_render_modules(output, cr);

		output->render_clipped = false;
		cairo_restore(cr);
	} else {
		barny_render_liquid_glass(output, cr);
		barny_render_modules(output, cr);
	}

	for (i = 0; i < state->module_count; i++) {
		new_w = state->modules[i] ? state->modules[i]->width : 0;
		if (new_w != saved_widths[i]) {
			width_changed = true;
			break;
		}
	}

	/* A module resized while drawing, so the layout it was drawn with is
	   stale: redo the bar in full, strip or not. */
	if (width_changed) {
		partial = false;
		barny_render_liquid_glass(output, cr);
		barny_render_modules(output, cr);
	}

	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = false;
		}
	}

	if (lens_anim) {
		output->redraw_queued = true;
	}

	output->lens_dmg_x     = have_lens ? lx : 0;
	output->lens_dmg_y     = have_lens ? ly : 0;
	output->lens_dmg_w     = have_lens ? lw : 0;
	output->lens_dmg_h     = have_lens ? lh : 0;
	output->lens_dmg_valid = true;

	barny_output_request_frame(output);

	cairo_surface_flush(output->cairo_surface);
	wl_surface_attach(output->surface, output->buffer, 0, 0);
	if (partial) {
		wl_surface_damage_buffer(output->surface, dx * output->scale,
		                         dy * output->scale,
		                         dw * output->scale,
		                         dh * output->scale);
	} else {
		wl_surface_damage_buffer(output->surface, 0, 0,
		                         output->surf_width * output->scale,
		                         output->surf_height * output->scale);
	}
	wl_surface_commit(output->surface);
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
