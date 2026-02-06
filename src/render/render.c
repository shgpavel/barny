#include "barny.h"

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

	/* Calculate center total width for positioning */
	int center_total_width = 0;
	for (int i = 0; i < center_count; i++) {
		center_total_width += state->modules[center_modules[i]]->width;
		if (i < center_count - 1)
			center_total_width += spacing;
	}

	/* Render left modules */
	int x = left_x;
	for (int i = 0; i < left_count; i++) {
		barny_module_t *mod = state->modules[left_modules[i]];
		int mod_height      = mod->height > 0 ? mod->height : height;
		int y               = (height - mod_height) / 2;

		cairo_save(cr);
		if (mod->render) {
			mod->render(mod, cr, x, y, mod->width, mod_height);
		}
		cairo_restore(cr);

		x += mod->width + spacing;
	}

	/* Render center modules */
	x = center_x - center_total_width / 2;
	for (int i = 0; i < center_count; i++) {
		barny_module_t *mod = state->modules[center_modules[i]];
		int mod_height      = mod->height > 0 ? mod->height : height;
		int y               = (height - mod_height) / 2;

		cairo_save(cr);
		if (mod->render) {
			mod->render(mod, cr, x, y, mod->width, mod_height);
		}
		cairo_restore(cr);

		x += mod->width + spacing;
	}

	/* Render right modules (right-to-left) */
	x = right_x;
	for (int i = right_count - 1; i >= 0; i--) {
		barny_module_t *mod  = state->modules[right_modules[i]];
		int mod_height       = mod->height > 0 ? mod->height : height;
		int y                = (height - mod_height) / 2;

		x                   -= mod->width;

		cairo_save(cr);
		if (mod->render) {
			mod->render(mod, cr, x, y, mod->width, mod_height);
		}
		cairo_restore(cr);

		x -= spacing;
	}
}
