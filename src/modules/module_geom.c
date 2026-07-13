#include "barny.h"

/* Module geometry is per output: the same module lands at a different x on
   every output (widths differ, narrow outputs drop modules), so input
   hit-testing must ask the output the pointer is actually on. */

bool
barny_output_module_rect(const barny_output_t *output,
                         const barny_module_t *mod, int *x, int *w)
{
	const barny_state_t *state;
	int                  i;

	if (!output || !mod || !output->state)
		return false;

	state = output->state;
	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i] != mod)
			continue;
		if (output->mod_x[i] < 0 || output->mod_w[i] <= 0)
			return false;
		if (x)
			*x = output->mod_x[i];
		if (w)
			*w = output->mod_w[i];
		return true;
	}

	return false;
}

bool
barny_pointer_module_rect(barny_state_t *state, const barny_module_t *mod,
                          int *x, int *w)
{
	if (!state)
		return false;

	return barny_output_module_rect(state->pointer_output, mod, x, w);
}
