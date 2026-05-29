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
	int sum = 0, visible = 0;

	for (int i = 0; i < n; i++) {
		int j = idx[i];
		if (dropped[j])
			continue;
		barny_module_t *m = state->modules[j];
		if (!m)
			continue;
		if (is_gap_placeholder(m) && !include_gaps)
			continue;
		int w = m->width > 0 ? m->width : 0;
		sum  += w;
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
	int left_end    = pad_l + total_l;
	int right_start = (width - pad_r) - total_r;

	if (total_c <= 0) {
		if (total_l <= 0 || total_r <= 0)
			return left_end <= right_start;
		return left_end + spacing <= right_start;
	}

	int center_start = (width / 2) - total_c / 2;
	int center_end   = center_start + total_c;

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
	for (int i = n - 1; i >= 0; i--) {
		int j = idx[i];
		if (dropped[j])
			continue;
		barny_module_t *m = state->modules[j];
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
	for (int i = 0; i < n; i++) {
		int j = idx[i];
		if (dropped[j])
			continue;
		barny_module_t *m = state->modules[j];
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

	if (total_c > 0) {
		int gap_l = (total_l > 0) ? spacing : 0;
		int gap_r = (total_r > 0) ? spacing : 0;
		slack_lc  = center_start - (left_end + gap_l);
		slack_cr  = right_start - (center_end + gap_r);
	}
	if (total_c <= 0 && total_l > 0 && total_r > 0)
		slack_lr = right_start - (left_end + spacing);

	int worst     = slack_lc;
	int collision = 0;
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

	int v;
	switch (collision) {
	case 0: /* left vs center */
		v = last_visible_real(state, left, lc, dropped);
		if (v >= 0)
			return v;
		return first_visible_real(state, center, cc, dropped);
	case 1: /* center vs right — prefer dropping right to preserve center */
		v = first_visible_real(state, right, rc, dropped);
		if (v >= 0)
			return v;
		return last_visible_real(state, center, cc, dropped);
	default: /* left vs right (no center) */
		v = last_visible_real(state, left, lc, dropped);
		if (v >= 0)
			return v;
		return first_visible_real(state, right, rc, dropped);
	}
}

static void
render_section(barny_state_t *state, cairo_t *cr, const int *idx, int n,
               int start_x, int height, int spacing, bool include_gaps,
               const bool *dropped)
{
	int x = start_x;
	for (int i = 0; i < n; i++) {
		int j               = idx[i];
		barny_module_t *mod = state->modules[j];
		if (!mod)
			continue;

		if (is_gap_placeholder(mod) && !include_gaps) {
			mod->render_x = -1;
			continue;
		}

		if (dropped[j]) {
			/* Measure-only pass: clip to nothing so the module's
			 * render() can refresh its width without producing
			 * visible output. Required so a module that shrinks
			 * later can be un-dropped on the next layout pass. */
			mod->render_x = -1;
			if (mod->render) {
				int h = mod->height > 0 ? mod->height : height;
				cairo_save(cr);
				cairo_new_path(cr);
				cairo_rectangle(cr, 0, 0, 0, 0);
				cairo_clip(cr);
				mod->render(mod, cr, 0, 0, 0, h);
				cairo_restore(cr);
			}
			continue;
		}

		int w = mod->width > 0 ? mod->width : 0;
		int h = mod->height > 0 ? mod->height : height;
		int y = (height - h) / 2;

		mod->render_x = x;
		cairo_save(cr);
		if (mod->render)
			mod->render(mod, cr, x, y, w, h);
		cairo_restore(cr);

		x += w + spacing;
	}
}

void
barny_render_frame(barny_output_t *output)
{
	if (!output->configured || !output->cr) {
		return;
	}

	/* If a frame is already in-flight, queue a redraw request for this
	 * output.  Rendering now would overwrite the SHM buffer while the
	 * compositor may still be reading it. */
	if (output->frame_pending) {
		output->redraw_queued = true;
		return;
	}

	/* This render is consuming the latest queued request. */
	output->redraw_queued = false;

	cairo_t       *cr    = output->cr;
	barny_state_t *state = output->state;

	/* Snapshot module widths before rendering so we can detect layout
	 * changes (e.g. workspace count change, text width change).  If any
	 * width changed we re-render once more — still inside the same frame
	 * before commit — so the compositor only ever sees the final layout. */
	int saved_widths[BARNY_MAX_MODULES];
	for (int i = 0; i < state->module_count; i++) {
		saved_widths[i] = state->modules[i] ? state->modules[i]->width : 0;
	}

	/* Render liquid glass background */
	barny_render_liquid_glass(output, cr);

	/* Render modules */
	barny_render_modules(output, cr);

	/* Check if any module width changed during render */
	bool width_changed = false;
	for (int i = 0; i < state->module_count; i++) {
		int new_w = state->modules[i] ? state->modules[i]->width : 0;
		if (new_w != saved_widths[i]) {
			width_changed = true;
			break;
		}
	}

	/* If layout changed, re-render with the now-correct widths so the
	 * committed frame already has the final positions. */
	if (width_changed) {
		barny_render_liquid_glass(output, cr);
		barny_render_modules(output, cr);
	}

	/* Mark all modules as clean */
	for (int i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = false;
		}
	}

	/* Register the frame callback BEFORE committing — the Wayland spec
	 * requires wl_surface.frame to precede the commit that activates it.
	 * This guarantees frame_done fires after the compositor presents this
	 * frame, which is when it is safe to render the next one. */
	barny_output_request_frame(output);

	/* Commit surface */
	cairo_surface_flush(output->cairo_surface);
	wl_surface_attach(output->surface, output->buffer, 0, 0);
	wl_surface_damage_buffer(output->surface, 0, 0,
	                         output->width * output->scale,
	                         output->height * output->scale);
	wl_surface_commit(output->surface);
}

void
barny_render_modules(barny_output_t *output, cairo_t *cr)
{
	barny_state_t *state  = output->state;
	int            width  = output->width;
	int            height = output->height;

	for (int i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->render_x = -1;
		}
	}

	int left[BARNY_MAX_MODULES], lc   = 0;
	int center[BARNY_MAX_MODULES], cc = 0;
	int right[BARNY_MAX_MODULES], rc  = 0;

	for (int i = 0; i < state->module_count; i++) {
		barny_module_t *mod = state->modules[i];
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

	int spacing = state->config.module_spacing;
	int pad_l   = 16;
	int pad_r   = 16;

	bool dropped[BARNY_MAX_MODULES] = { false };
	bool include_gaps               = true;

	int total_l, total_c, total_r;

	/* Phase 1: full gaps. Phase 2: gaps dropped. Then drop real modules
	 * one at a time, picking from the section nearest the worst overlap. */
	for (int phase = 0; phase < 2; phase++) {
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
				break; /* try gaps-off phase first */

			int victim = pick_victim(state, left, lc, center, cc,
			                         right, rc, width, pad_l, pad_r,
			                         spacing, total_l, total_c,
			                         total_r, dropped);
			if (victim < 0)
				goto layout_ready; /* nothing more to drop */
			dropped[victim] = true;
		}
	}

layout_ready:
	render_section(state, cr, left, lc, pad_l, height, spacing, include_gaps,
	               dropped);

	int center_start = (width / 2) - total_c / 2;
	if (center_start < pad_l)
		center_start = pad_l;
	render_section(state, cr, center, cc, center_start, height, spacing,
	               include_gaps, dropped);

	int right_start = (width - pad_r) - total_r;
	if (right_start < pad_l)
		right_start = pad_l;
	render_section(state, cr, right, rc, right_start, height, spacing,
	               include_gaps, dropped);
}
