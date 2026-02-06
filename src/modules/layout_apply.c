#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "barny.h"

typedef barny_module_t *(*module_factory_fn)(void);

typedef struct {
	const char       *name;
	module_factory_fn factory;
} module_factory_entry_t;

static const module_factory_entry_t module_factories[] = {
	{ "clock", barny_module_clock_create },
	{ "workspace", barny_module_workspace_create },
	{ "sysinfo", barny_module_sysinfo_create },
	{ "weather", barny_module_weather_create },
	{ "disk", barny_module_disk_create },
	{ "ram", barny_module_ram_create },
	{ "network", barny_module_network_create },
	{ "fileread", barny_module_fileread_create },
	{ "crypto", barny_module_crypto_create },
	{ "tray", barny_module_tray_create },
};

static barny_module_t *
create_module_by_name(const char *name)
{
	int count = (int)(sizeof(module_factories) / sizeof(module_factories[0]));

	for (int i = 0; i < count; i++) {
		if (strcmp(module_factories[i].name, name) == 0) {
			return module_factories[i].factory();
		}
	}

	return NULL;
}

static barny_module_t *
create_gap_module(barny_state_t *state, barny_position_t position, int units)
{
	barny_module_t *mod;
	int             spacing;
	int             width;

	if (units < 1) {
		return NULL;
	}

	mod = calloc(1, sizeof(*mod));
	if (!mod) {
		return NULL;
	}

	spacing = (state && state->config.module_spacing > 0)
	                  ? state->config.module_spacing
	                  : 16;
	width = (units - 1) * spacing;
	if (width < 0) {
		width = 0;
	}

	mod->name = "gap";
	mod->position = position;
	mod->width = width;
	mod->height = 0;
	mod->dirty = false;
	return mod;
}

static int
register_slot_modules(barny_state_t *state, barny_position_t position,
                      char *const *names, int count)
{
	int registered = 0;

	for (int i = 0; i < count; i++) {
		if (!names[i] || !*names[i]) {
			continue;
		}

		int gap_units = barny_module_layout_gap_units(names[i]);
		barny_module_t *mod = NULL;
		if (gap_units > 0) {
			mod = create_gap_module(state, position, gap_units);
		} else {
			mod = create_module_by_name(names[i]);
		}
		if (!mod) {
			fprintf(stderr, "barny: failed to create module '%s'\n", names[i]);
			continue;
		}

		mod->position = position;
		barny_module_register(state, mod);
		registered++;
	}

	return registered;
}

static bool
has_factory(const char *name)
{
	int count = (int)(sizeof(module_factories) / sizeof(module_factories[0]));

	for (int i = 0; i < count; i++) {
		if (strcmp(module_factories[i].name, name) == 0) {
			return true;
		}
	}

	return false;
}

static void
validate_catalog_vs_factories(void)
{
	static bool validated = false;
	if (validated) {
		return;
	}
	validated = true;

	const char *names[BARNY_MAX_MODULES] = { 0 };
	int total = barny_module_catalog_names(names, BARNY_MAX_MODULES);

	for (int i = 0; i < total; i++) {
		if (!has_factory(names[i])) {
			fprintf(stderr,
			        "barny: warning: catalog module '%s' has no factory\n",
			        names[i]);
		}
	}
}

int
barny_module_layout_apply_to_state(const barny_module_layout_t *layout,
                                   barny_state_t *state)
{
	int registered = 0;

	if (!layout || !state) {
		return -1;
	}

	validate_catalog_vs_factories();

	registered += register_slot_modules(state, BARNY_POS_LEFT,
	                                    layout->left, layout->left_count);
	registered += register_slot_modules(state, BARNY_POS_CENTER,
	                                    layout->center, layout->center_count);
	registered += register_slot_modules(state, BARNY_POS_RIGHT,
	                                    layout->right, layout->right_count);

	return registered;
}
