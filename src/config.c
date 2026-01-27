#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "barny.h"

void
barny_config_defaults(barny_config_t *config)
{
	memset(config, 0, sizeof(*config));

	config->height               = BARNY_DEFAULT_HEIGHT;
	config->margin_top           = 0;
	config->margin_bottom        = 0;
	config->margin_left          = 0;
	config->margin_right         = 0;
	config->border_radius        = BARNY_BORDER_RADIUS;
	config->position_top         = true;
	config->font                 = NULL;
	config->wallpaper_path       = NULL;
	config->blur_radius          = BARNY_BLUR_RADIUS;
	config->brightness           = 1.1;

	/* Liquid glass effect defaults */
	config->refraction_mode      = BARNY_REFRACT_LENS;
	config->displacement_scale   = 8.0;
	config->chromatic_aberration = 1.5;
	config->edge_refraction      = 1.2;
	config->noise_scale          = 0.02;
	config->noise_octaves        = 2;
}

static char *
trim(char *str)
{
	while (isspace(*str))
		str++;
	if (*str == 0)
		return str;

	char *end = str + strlen(str) - 1;
	while (end > str && isspace(*end))
		end--;
	end[1] = '\0';

	return str;
}

static void
parse_line(barny_config_t *config, const char *key, const char *value)
{
	if (strcmp(key, "height") == 0) {
		config->height = atoi(value);
	} else if (strcmp(key, "margin_top") == 0) {
		config->margin_top = atoi(value);
	} else if (strcmp(key, "margin_bottom") == 0) {
		config->margin_bottom = atoi(value);
	} else if (strcmp(key, "margin_left") == 0) {
		config->margin_left = atoi(value);
	} else if (strcmp(key, "margin_right") == 0) {
		config->margin_right = atoi(value);
	} else if (strcmp(key, "border_radius") == 0) {
		config->border_radius = atoi(value);
	} else if (strcmp(key, "position") == 0) {
		config->position_top = strcmp(value, "top") == 0;
	} else if (strcmp(key, "font") == 0) {
		free(config->font);
		config->font = strdup(value);
	} else if (strcmp(key, "wallpaper") == 0) {
		free(config->wallpaper_path);
		config->wallpaper_path = strdup(value);
	} else if (strcmp(key, "blur_radius") == 0) {
		config->blur_radius = atof(value);
	} else if (strcmp(key, "brightness") == 0) {
		config->brightness = atof(value);
	} else if (strcmp(key, "refraction") == 0) {
		if (strcmp(value, "none") == 0) {
			config->refraction_mode = BARNY_REFRACT_NONE;
		} else if (strcmp(value, "lens") == 0) {
			config->refraction_mode = BARNY_REFRACT_LENS;
		} else if (strcmp(value, "liquid") == 0) {
			config->refraction_mode = BARNY_REFRACT_LIQUID;
		}
	} else if (strcmp(key, "displacement_scale") == 0) {
		config->displacement_scale = atof(value);
	} else if (strcmp(key, "chromatic_aberration") == 0) {
		config->chromatic_aberration = atof(value);
	} else if (strcmp(key, "edge_refraction") == 0) {
		config->edge_refraction = atof(value);
	} else if (strcmp(key, "noise_scale") == 0) {
		config->noise_scale = atof(value);
	} else if (strcmp(key, "noise_octaves") == 0) {
		config->noise_octaves = atoi(value);
	}
}

int
barny_config_load(barny_config_t *config, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		return -1;
	}

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char *trimmed = trim(line);

		/* Skip comments and empty lines */
		if (*trimmed == '#' || *trimmed == '\0') {
			continue;
		}

		/* Find = separator */
		char *eq = strchr(trimmed, '=');
		if (!eq)
			continue;

		*eq          = '\0';
		char  *key   = trim(trimmed);
		char  *value = trim(eq + 1);

		/* Remove quotes from value */
		size_t vlen  = strlen(value);
		if (vlen >= 2 && value[0] == '"' && value[vlen - 1] == '"') {
			value[vlen - 1] = '\0';
			value++;
		}

		parse_line(config, key, value);
	}

	fclose(f);
	return 0;
}
