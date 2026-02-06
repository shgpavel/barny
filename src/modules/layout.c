#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "barny.h"

typedef struct {
	const char       *name;
	barny_position_t  default_position;
} module_catalog_entry_t;

static const module_catalog_entry_t module_catalog[] = {
	{ "clock", BARNY_POS_RIGHT },
	{ "workspace", BARNY_POS_LEFT },
	{ "sysinfo", BARNY_POS_RIGHT },
	{ "weather", BARNY_POS_RIGHT },
	{ "disk", BARNY_POS_RIGHT },
	{ "ram", BARNY_POS_RIGHT },
	{ "network", BARNY_POS_RIGHT },
	{ "fileread", BARNY_POS_RIGHT },
	{ "crypto", BARNY_POS_RIGHT },
	{ "tray", BARNY_POS_RIGHT },
};

static int
catalog_size(void)
{
	return (int)(sizeof(module_catalog) / sizeof(module_catalog[0]));
}

int
barny_module_layout_gap_units(const char *name)
{
	char *end = NULL;
	long  units;

	if (!name) {
		return 0;
	}

	if (strncmp(name, "gap:", 4) != 0 && strncmp(name, "__gap:", 6) != 0) {
		return 0;
	}

	const char *num = (name[0] == 'g') ? name + 4 : name + 6;
	if (!*num) {
		return 0;
	}

	units = strtol(num, &end, 10);
	if (!end || *end != '\0') {
		return 0;
	}
	if (units < 1 || units > 4096) {
		return 0;
	}

	return (int)units;
}

static char **
slot_array_for_position(barny_module_layout_t *layout, barny_position_t position,
                        int **count_ptr)
{
	if (!layout || !count_ptr) {
		return NULL;
	}

	switch (position) {
	case BARNY_POS_LEFT:
		*count_ptr = &layout->left_count;
		return layout->left;
	case BARNY_POS_CENTER:
		*count_ptr = &layout->center_count;
		return layout->center;
	case BARNY_POS_RIGHT:
		*count_ptr = &layout->right_count;
		return layout->right;
	default:
		return NULL;
	}
}

static char *
trim_ws(char *str)
{
	while (*str && isspace((unsigned char)*str)) {
		str++;
	}

	if (*str == '\0') {
		return str;
	}

	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}

	return str;
}

static void
layout_clear_slot(char **slot, int *count)
{
	if (!slot || !count) {
		return;
	}

	for (int i = 0; i < *count; i++) {
		free(slot[i]);
		slot[i] = NULL;
	}
	*count = 0;
}

static void
layout_clear_all(barny_module_layout_t *layout)
{
	layout_clear_slot(layout->left, &layout->left_count);
	layout_clear_slot(layout->center, &layout->center_count);
	layout_clear_slot(layout->right, &layout->right_count);
}

void
barny_module_layout_init(barny_module_layout_t *layout)
{
	if (!layout) {
		return;
	}
	memset(layout, 0, sizeof(*layout));
}

void
barny_module_layout_destroy(barny_module_layout_t *layout)
{
	if (!layout) {
		return;
	}
	layout_clear_all(layout);
}

bool
barny_module_catalog_has(const char *name)
{
	if (!name || !*name) {
		return false;
	}

	if (barny_module_layout_gap_units(name) > 0) {
		return true;
	}

	int count = catalog_size();
	for (int i = 0; i < count; i++) {
		if (strcmp(module_catalog[i].name, name) == 0) {
			return true;
		}
	}

	return false;
}

int
barny_module_catalog_names(const char **names, int max_names)
{
	int total = catalog_size();
	if (!names || max_names <= 0) {
		return total;
	}

	int limit = max_names < total ? max_names : total;
	for (int i = 0; i < limit; i++) {
		names[i] = module_catalog[i].name;
	}

	return total;
}

bool
barny_module_layout_contains(const barny_module_layout_t *layout, const char *name)
{
	if (!layout || !name) {
		return false;
	}

	if (barny_module_layout_gap_units(name) > 0) {
		return false;
	}

	for (int i = 0; i < layout->left_count; i++) {
		if (layout->left[i] && strcmp(layout->left[i], name) == 0) {
			return true;
		}
	}
	for (int i = 0; i < layout->center_count; i++) {
		if (layout->center[i] && strcmp(layout->center[i], name) == 0) {
			return true;
		}
	}
	for (int i = 0; i < layout->right_count; i++) {
		if (layout->right[i] && strcmp(layout->right[i], name) == 0) {
			return true;
		}
	}

	return false;
}

int
barny_module_layout_insert(barny_module_layout_t *layout,
                           barny_position_t position, const char *name, int index)
{
	int   *count = NULL;
	char **slot  = slot_array_for_position(layout, position, &count);
	bool   is_gap;

	if (!slot || !count || !name || !*name) {
		return -1;
	}

	if (!barny_module_catalog_has(name)) {
		return -1;
	}

	is_gap = barny_module_layout_gap_units(name) > 0;
	if (!is_gap && barny_module_layout_contains(layout, name)) {
		return -1;
	}

	if (*count >= BARNY_MAX_MODULES) {
		return -1;
	}

	if (index < 0 || index > *count) {
		index = *count;
	}

	char *copy = strdup(name);
	if (!copy) {
		return -1;
	}

	for (int i = *count; i > index; i--) {
		slot[i] = slot[i - 1];
	}
	slot[index] = copy;
	(*count)++;
	return 0;
}

bool
barny_module_layout_remove(barny_module_layout_t *layout, const char *name)
{
	int  *count = NULL;
	char **slot = NULL;

	if (!layout || !name) {
		return false;
	}

	for (int pos = BARNY_POS_LEFT; pos <= BARNY_POS_RIGHT; pos++) {
		slot = slot_array_for_position(layout, (barny_position_t)pos, &count);
		if (!slot || !count) {
			continue;
		}

		for (int i = 0; i < *count; i++) {
			if (!slot[i] || strcmp(slot[i], name) != 0) {
				continue;
			}

			free(slot[i]);
			for (int j = i; j < *count - 1; j++) {
				slot[j] = slot[j + 1];
			}
			slot[*count - 1] = NULL;
			(*count)--;
			return true;
		}
	}

	return false;
}

static void
parse_csv_slot(barny_module_layout_t *layout, barny_position_t position,
               const char *csv)
{
	if (!layout || !csv) {
		return;
	}

	char *tmp = strdup(csv);
	if (!tmp) {
		return;
	}

	char *saveptr = NULL;
	char *token   = strtok_r(tmp, ",", &saveptr);
	while (token) {
		const char *name = trim_ws(token);
		bool        is_gap;
		if (*name) {
			is_gap = barny_module_layout_gap_units(name) > 0;
			if (!barny_module_catalog_has(name)) {
				fprintf(stderr, "barny: ignoring unknown module in layout: %s\n",
				        name);
			} else if (barny_module_layout_insert(layout, position, name, -1)
			           < 0) {
				if (!is_gap) {
					fprintf(stderr,
					        "barny: ignoring duplicate module in layout: %s\n",
					        name);
				}
			}
		}

		token = strtok_r(NULL, ",", &saveptr);
	}

	free(tmp);
}

void
barny_module_layout_set_defaults(barny_module_layout_t *layout)
{
	if (!layout) {
		return;
	}

	layout_clear_all(layout);

	int count = catalog_size();
	for (int i = 0; i < count; i++) {
		barny_module_layout_insert(layout, module_catalog[i].default_position,
		                           module_catalog[i].name, -1);
	}
}

int
barny_module_layout_load_from_config(const barny_config_t *config,
                                     barny_module_layout_t *layout)
{
	bool has_explicit_layout = false;

	if (!layout) {
		return -1;
	}

	layout_clear_all(layout);

	if (!config) {
		barny_module_layout_set_defaults(layout);
		return 0;
	}

	has_explicit_layout = config->modules_left || config->modules_center
	                      || config->modules_right;

	if (!has_explicit_layout) {
		barny_module_layout_set_defaults(layout);
		return 0;
	}

	parse_csv_slot(layout, BARNY_POS_LEFT, config->modules_left);
	parse_csv_slot(layout, BARNY_POS_CENTER, config->modules_center);
	parse_csv_slot(layout, BARNY_POS_RIGHT, config->modules_right);

	return 0;
}

char *
barny_module_layout_serialize_csv(const char *const *names, int count)
{
	size_t len = 1;
	char  *out = NULL;
	int    valid_count = 0;

	if (!names || count <= 0) {
		out = malloc(1);
		if (out) {
			out[0] = '\0';
		}
		return out;
	}

	for (int i = 0; i < count; i++) {
		if (!names[i]) {
			continue;
		}
		len += strlen(names[i]);
		valid_count++;
	}

	if (valid_count > 1) {
		len += (size_t)(valid_count - 1) * 2;
	}

	out = calloc(1, len);
	if (!out) {
		return NULL;
	}

	char *p = out;
	int   added = 0;
	for (int i = 0; i < count; i++) {
		if (!names[i]) {
			continue;
		}

		size_t nlen = strlen(names[i]);
		memcpy(p, names[i], nlen);
		p += nlen;
		added++;
		if (added < valid_count) {
			memcpy(p, ", ", 2);
			p += 2;
		}
	}
	*p = '\0';

	return out;
}
