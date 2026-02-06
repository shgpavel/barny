#include <string.h>

#include "barny.h"

static bool
is_gap_placeholder(const barny_module_t *mod)
{
	return mod && mod->name && strcmp(mod->name, "gap") == 0
	       && mod->render == NULL;
}

static int
effective_module_width(const barny_module_t *mod, double gap_scale)
{
	int w = (mod && mod->width > 0) ? mod->width : 0;
	if (w <= 0) {
		return 0;
	}

	if (is_gap_placeholder(mod) && gap_scale < 1.0) {
		int scaled = (int)(w * gap_scale + 0.5);
		return scaled > 0 ? scaled : 0;
	}

	return w;
}

static double
compute_gap_scale(barny_state_t *state, const int *module_indices, int count,
                  int available_width, int spacing)
{
	int total_width = 0;
	int gap_width   = 0;

	for (int i = 0; i < count; i++) {
		barny_module_t *mod = state->modules[module_indices[i]];
		int             w   = (mod && mod->width > 0) ? mod->width : 0;
		total_width += w;
		if (i < count - 1) {
			total_width += spacing;
		}
		if (is_gap_placeholder(mod)) {
			gap_width += w;
		}
	}

	if (total_width <= available_width || gap_width <= 0) {
		return 1.0;
	}

	int overflow = total_width - available_width;
	int keep     = gap_width - overflow;
	if (keep <= 0) {
		return 0.0;
	}

	return (double)keep / (double)gap_width;
}

void
barny_render_frame(barny_output_t *output)
{
	if (!output->configured || !output->cr) {
		return;
	}

	cairo_t       *cr    = output->cr;
	barny_state_t *state = output->state;

	/* Render liquid glass background */
	barny_render_liquid_glass(output, cr);

	/* Render modules */
	barny_render_modules(output, cr);

	/* Mark all modules as clean */
	for (int i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = false;
		}
	}

	/* Commit surface */
	cairo_surface_flush(output->cairo_surface);
	wl_surface_attach(output->surface, output->buffer, 0, 0);
	wl_surface_damage_buffer(output->surface, 0, 0,
	                         output->width * output->scale,
	                         output->height * output->scale);
	wl_surface_commit(output->surface);

	/* Request frame callback for vsync pacing — frame_done will
	 * trigger the next render only if a module is dirty */
	barny_output_request_frame(output);
}

void
barny_render_modules(barny_output_t *output, cairo_t *cr)
{
	barny_state_t *state    = output->state;
	int            width    = output->width;
	int            height   = output->height;

	/* Calculate module positions */
	int            left_x   = 16;
	int            right_x  = width - 16;
	int            center_x = width / 2;

	/* Collect modules by position */
	int            left_modules[BARNY_MAX_MODULES], left_count     = 0;
	int            center_modules[BARNY_MAX_MODULES], center_count = 0;
	int            right_modules[BARNY_MAX_MODULES], right_count   = 0;

	for (int i = 0; i < state->module_count; i++) {
		barny_module_t *mod = state->modules[i];
		if (!mod)
			continue;

		switch (mod->position) {
		case BARNY_POS_LEFT:
			left_modules[left_count++] = i;
			break;
		case BARNY_POS_CENTER:
			center_modules[center_count++] = i;
			break;
		case BARNY_POS_RIGHT:
			right_modules[right_count++] = i;
			break;
		}
	}

	/* Module spacing from config */
	int spacing = state->config.module_spacing;
	int available_width = right_x - left_x;
	double left_gap_scale = compute_gap_scale(state, left_modules, left_count,
	                                          available_width, spacing);
	double center_gap_scale = compute_gap_scale(state, center_modules, center_count,
	                                            available_width, spacing);
	double right_gap_scale = compute_gap_scale(state, right_modules, right_count,
	                                           available_width, spacing);

	/* Calculate center total width for positioning */
	int center_total_width = 0;
	for (int i = 0; i < center_count; i++) {
		center_total_width += effective_module_width(
		        state->modules[center_modules[i]], center_gap_scale);
		if (i < center_count - 1)
			center_total_width += spacing;
	}

	/* Render left modules */
	int x = left_x;
	for (int i = 0; i < left_count; i++) {
		barny_module_t *mod = state->modules[left_modules[i]];
		int mod_width       = effective_module_width(mod, left_gap_scale);
		int mod_height      = mod->height > 0 ? mod->height : height;
		int y               = (height - mod_height) / 2;

		if (x >= right_x) {
			break;
		}
		if (mod_width > 0 && x + mod_width > right_x) {
			break;
		}

		cairo_save(cr);
		if (mod->render) {
			mod->render(mod, cr, x, y, mod_width, mod_height);
		}
		cairo_restore(cr);

		x += mod_width + spacing;
	}

	/* Render center modules */
	x = center_x - center_total_width / 2;
	if (x < left_x) {
		x = left_x;
	}
	for (int i = 0; i < center_count; i++) {
		barny_module_t *mod = state->modules[center_modules[i]];
		int mod_width       = effective_module_width(mod, center_gap_scale);
		int mod_height      = mod->height > 0 ? mod->height : height;
		int y               = (height - mod_height) / 2;

		if (x >= right_x) {
			break;
		}
		if (mod_width > 0 && x + mod_width > right_x) {
			break;
		}

		cairo_save(cr);
		if (mod->render) {
			mod->render(mod, cr, x, y, mod_width, mod_height);
		}
		cairo_restore(cr);

		x += mod_width + spacing;
	}

	/* Render right modules (right-to-left) */
	x = right_x;
	for (int i = right_count - 1; i >= 0; i--) {
		barny_module_t *mod  = state->modules[right_modules[i]];
		int mod_width        = effective_module_width(mod, right_gap_scale);
		int mod_height       = mod->height > 0 ? mod->height : height;
		int y                = (height - mod_height) / 2;

		if (x <= left_x) {
			break;
		}
		if (mod_width > 0 && x - mod_width < left_x) {
			break;
		}

		x                   -= mod_width;

		cairo_save(cr);
		if (mod->render) {
			mod->render(mod, cr, x, y, mod_width, mod_height);
		}
		cairo_restore(cr);

		x -= spacing;
	}
}
