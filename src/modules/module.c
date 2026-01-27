#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"

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
	for (int i = 0; i < state->module_count; i++) {
		barny_module_t *mod = state->modules[i];
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
	for (int i = 0; i < state->module_count; i++) {
		barny_module_t *mod = state->modules[i];
		if (mod && mod->update) {
			mod->update(mod);
		}
	}

	/* Request frame redraw if any module is dirty */
	bool needs_redraw = false;
	for (int i = 0; i < state->module_count; i++) {
		if (state->modules[i] && state->modules[i]->dirty) {
			needs_redraw = true;
			break;
		}
	}

	if (needs_redraw) {
		for (barny_output_t *out = state->outputs; out; out = out->next) {
			if (out->configured) {
				barny_render_frame(out);
			}
		}
	}
}

void
barny_modules_destroy(barny_state_t *state)
{
	for (int i = 0; i < state->module_count; i++) {
		barny_module_t *mod = state->modules[i];
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
	for (int i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = true;
		}
	}
}
