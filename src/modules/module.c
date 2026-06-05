#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "util.h"

void
barny_module_register(barny_state_t *state, barny_module_t *module)
{
	if (!module)
		return;

	if (state->module_count >= BARNY_MAX_MODULES) {
		fprintf(stderr, "barny: maximum modules reached\n");
		return;
	}

	state->modules[state->module_count++] = module;
	printf("barny: registered module '%s'\n", module->name);
}

void
barny_modules_init(barny_state_t *state)
{
	int             i;
	barny_module_t *mod;

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod && mod->init) {
			if (mod->init(mod, state) < 0) {
				fprintf(stderr,
				        "barny: failed to init module '%s'\n",
				        mod->name);
			}
		}
	}
}

void
barny_modules_update(barny_state_t *state)
{
	uint64_t        t;
	int             i;
	barny_module_t *mod;
	barny_output_t *out;

	t = barny_now_ms();

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (!mod || !mod->update)
			continue;
		if (mod->update_interval_ms > 0
		    && mod->last_update_ms != 0
		    && (t - mod->last_update_ms)
		               < (uint64_t)mod->update_interval_ms) {
			continue;
		}
		mod->update(mod);
		mod->last_update_ms = t;
	}

	if (barny_modules_any_dirty(state)) {
		for (out = state->outputs; out; out = out->next) {
			if (out->configured) {
				barny_render_frame(out);
			}
		}
	}
}

void
barny_modules_destroy(barny_state_t *state)
{
	int             i;
	barny_module_t *mod;

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod) {
			if (mod->destroy) {
				mod->destroy(mod);
			}
			free(mod);
			state->modules[i] = NULL;
		}
	}
	state->module_count = 0;
}

void
barny_modules_mark_dirty(barny_state_t *state)
{
	int i;

	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = true;
		}
	}
}

barny_module_t *
barny_module_find(barny_state_t *state, const char *name)
{
	int             i;
	barny_module_t *mod;

	if (!name) {
		return NULL;
	}

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod && mod->name && strcmp(mod->name, name) == 0) {
			return mod;
		}
	}

	return NULL;
}

bool
barny_modules_any_dirty(const barny_state_t *state)
{
	int i;

	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i] && state->modules[i]->dirty) {
			return true;
		}
	}

	return false;
}

int
barny_module_render_text(cairo_t *cr, PangoFontDescription *font,
                         const char *text, int x, int y, int h,
                         const barny_config_t *cfg, double fb_r, double fb_g,
                         double fb_b, double alpha)
{
	PangoLayout *layout;
	int          tw;
	int          th;
	int          ty;

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_text(layout, text, -1);

	pango_layout_get_pixel_size(layout, &tw, &th);

	ty = y + (h - th) / 2;

	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	cairo_move_to(cr, x + 1, ty + 1);
	pango_cairo_show_layout(cr, layout);

	if (cfg->text_color_set) {
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
		                      cfg->text_color_b, alpha);
	} else {
		cairo_set_source_rgba(cr, fb_r, fb_g, fb_b, alpha);
	}
	cairo_move_to(cr, x, ty);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	return tw;
}
