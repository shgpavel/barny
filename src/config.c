#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

#include "barny.h"

void
barny_config_validate_font(const barny_config_t *config)
{
	const char *font_str = config->font;
	if (!font_str) {
		fprintf(stderr, "barny: no font configured, modules will use built-in defaults\n");
		return;
	}

	PangoFontDescription *desc = pango_font_description_from_string(font_str);
	if (!desc) {
		fprintf(stderr, "barny: failed to parse font string: \"%s\"\n", font_str);
		return;
	}

	const char *requested_family = pango_font_description_get_family(desc);
	if (!requested_family) {
		fprintf(stderr, "barny: font string \"%s\" has no family component\n", font_str);
		pango_font_description_free(desc);
		return;
	}

	/* Use a temporary PangoFontMap to check if the font actually resolves */
	PangoFontMap *font_map = pango_cairo_font_map_get_default();
	PangoContext *context   = pango_font_map_create_context(font_map);
	PangoFont    *font      = pango_font_map_load_font(font_map, context, desc);

	if (!font) {
		fprintf(stderr, "barny: ERROR: font \"%s\" not found on system, "
		        "text will render with fallback font\n", font_str);
	} else {
		PangoFontDescription *actual = pango_font_describe(font);
		const char *actual_family    = pango_font_description_get_family(actual);

		if (actual_family && strcasecmp(actual_family, requested_family) != 0) {
			fprintf(stderr, "barny: WARNING: font \"%s\" resolved to \"%s\" "
			        "(requested family \"%s\" not found)\n",
			        font_str, actual_family, requested_family);
		} else {
			printf("barny: font loaded: \"%s\"\n", font_str);
		}

		pango_font_description_free(actual);
		g_object_unref(font);
	}

	g_object_unref(context);
	pango_font_description_free(desc);
}

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

	/* Workspace module defaults */
	config->workspace_indicator_size = 30;
	config->workspace_spacing        = 10;

	/* Sysinfo module defaults */
	config->sysinfo_freq_combined    = true;
	config->sysinfo_power_decimals   = 0;

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
	} else if (strcmp(key, "workspace_indicator_size") == 0) {
		config->workspace_indicator_size = atoi(value);
	} else if (strcmp(key, "workspace_spacing") == 0) {
		config->workspace_spacing = atoi(value);
	} else if (strcmp(key, "sysinfo_freq_combined") == 0) {
		config->sysinfo_freq_combined = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "sysinfo_power_decimals") == 0) {
		config->sysinfo_power_decimals = atoi(value);
		if (config->sysinfo_power_decimals < 0) config->sysinfo_power_decimals = 0;
		if (config->sysinfo_power_decimals > 2) config->sysinfo_power_decimals = 2;
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
