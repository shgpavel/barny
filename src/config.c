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

	/* Global text color (NULL = use module defaults) */
	config->text_color           = NULL;
	config->text_color_r         = 0.0;
	config->text_color_g         = 0.0;
	config->text_color_b         = 0.0;
	config->text_color_set       = false;

	/* Workspace module defaults */
	config->workspace_indicator_size = 30;
	config->workspace_spacing        = 10;

	/* Sysinfo module defaults */
	config->sysinfo_freq_combined    = true;
	config->sysinfo_freq_decimals    = 2;
	config->sysinfo_power_decimals   = 0;
	config->sysinfo_p_cores          = 0;  /* 0 = auto-detect */
	config->sysinfo_e_cores          = 0;  /* 0 = auto-detect */

	/* Tray module defaults */
	config->tray_icon_size           = 24;
	config->tray_icon_spacing        = 4;

	/* Liquid glass effect defaults */
	config->refraction_mode      = BARNY_REFRACT_LENS;
	config->displacement_scale   = 8.0;
	config->chromatic_aberration = 1.5;
	config->edge_refraction      = 1.2;
	config->noise_scale          = 0.02;
	config->noise_octaves        = 2;

	/* Clock module defaults */
	config->clock_show_time      = true;
	config->clock_24h_format     = true;
	config->clock_show_seconds   = true;
	config->clock_show_date      = false;
	config->clock_show_year      = true;
	config->clock_show_month     = true;
	config->clock_show_day       = true;
	config->clock_show_weekday   = true;
	config->clock_date_order     = 0;  /* dd/mm/yyyy */
	config->clock_date_separator = '/';

	/* Disk module defaults */
	config->disk_path            = NULL;  /* will use "/" as default */
	config->disk_show_percentage = false;
	config->disk_decimals        = 0;

	/* CPU temperature module defaults */
	config->cpu_temp_path        = NULL;  /* auto-detect */
	config->cpu_temp_zone        = 0;
	config->cpu_temp_show_unit   = true;

	/* RAM module defaults */
	config->ram_show_percentage  = false;
	config->ram_decimals         = 1;
	config->ram_used_method      = NULL;  /* will use "available" */

	/* Network module defaults */
	config->network_interface    = NULL;  /* auto */
	config->network_show_ip      = true;
	config->network_show_interface = false;
	config->network_prefer_ipv4  = true;

	/* File read module defaults */
	config->fileread_path        = NULL;
	config->fileread_title       = NULL;
	config->fileread_max_chars   = 64;
}

static int
parse_hex_color(const char *str, double *r, double *g, double *b)
{
	/* Handle common color names */
	if (strcasecmp(str, "black") == 0) {
		*r = *g = *b = 0.0;
		return 0;
	}
	if (strcasecmp(str, "white") == 0) {
		*r = *g = *b = 1.0;
		return 0;
	}

	/* Parse #RRGGBB format */
	if (str[0] != '#' || strlen(str) != 7)
		return -1;

	unsigned int rv, gv, bv;
	if (sscanf(str + 1, "%02x%02x%02x", &rv, &gv, &bv) != 3)
		return -1;

	*r = rv / 255.0;
	*g = gv / 255.0;
	*b = bv / 255.0;
	return 0;
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
	} else if (strcmp(key, "text_color") == 0) {
		free(config->text_color);
		if (strcmp(value, "default") == 0 || strlen(value) == 0) {
			config->text_color = NULL;
			config->text_color_set = false;
		} else {
			config->text_color = strdup(value);
			if (parse_hex_color(value, &config->text_color_r,
			                    &config->text_color_g,
			                    &config->text_color_b) == 0) {
				config->text_color_set = true;
			} else {
				fprintf(stderr, "barny: invalid text_color '%s', using default\n", value);
				config->text_color_set = false;
			}
		}
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
	} else if (strcmp(key, "sysinfo_freq_decimals") == 0) {
		config->sysinfo_freq_decimals = atoi(value);
		if (config->sysinfo_freq_decimals < 0) config->sysinfo_freq_decimals = 0;
		if (config->sysinfo_freq_decimals > 2) config->sysinfo_freq_decimals = 2;
	} else if (strcmp(key, "sysinfo_power_decimals") == 0) {
		config->sysinfo_power_decimals = atoi(value);
		if (config->sysinfo_power_decimals < 0) config->sysinfo_power_decimals = 0;
		if (config->sysinfo_power_decimals > 2) config->sysinfo_power_decimals = 2;
	} else if (strcmp(key, "sysinfo_p_cores") == 0) {
		config->sysinfo_p_cores = atoi(value);
		if (config->sysinfo_p_cores < 0) config->sysinfo_p_cores = 0;
	} else if (strcmp(key, "sysinfo_e_cores") == 0) {
		config->sysinfo_e_cores = atoi(value);
		if (config->sysinfo_e_cores < 0) config->sysinfo_e_cores = 0;
	} else if (strcmp(key, "tray_icon_size") == 0) {
		config->tray_icon_size = atoi(value);
		if (config->tray_icon_size < 8) config->tray_icon_size = 8;
		if (config->tray_icon_size > 64) config->tray_icon_size = 64;
	} else if (strcmp(key, "tray_icon_spacing") == 0) {
		config->tray_icon_spacing = atoi(value);
		if (config->tray_icon_spacing < 0) config->tray_icon_spacing = 0;
		if (config->tray_icon_spacing > 32) config->tray_icon_spacing = 32;
	/* Clock module */
	} else if (strcmp(key, "clock_show_time") == 0) {
		config->clock_show_time = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_24h_format") == 0) {
		config->clock_24h_format = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_show_seconds") == 0) {
		config->clock_show_seconds = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_show_date") == 0) {
		config->clock_show_date = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_show_year") == 0) {
		config->clock_show_year = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_show_month") == 0) {
		config->clock_show_month = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_show_day") == 0) {
		config->clock_show_day = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_show_weekday") == 0) {
		config->clock_show_weekday = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "clock_date_order") == 0) {
		config->clock_date_order = atoi(value);
		if (config->clock_date_order < 0) config->clock_date_order = 0;
		if (config->clock_date_order > 2) config->clock_date_order = 2;
	} else if (strcmp(key, "clock_date_separator") == 0) {
		if (strlen(value) > 0) config->clock_date_separator = value[0];
	/* Disk module */
	} else if (strcmp(key, "disk_path") == 0) {
		free(config->disk_path);
		config->disk_path = strdup(value);
	} else if (strcmp(key, "disk_show_percentage") == 0) {
		config->disk_show_percentage = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "disk_decimals") == 0) {
		config->disk_decimals = atoi(value);
		if (config->disk_decimals < 0) config->disk_decimals = 0;
		if (config->disk_decimals > 2) config->disk_decimals = 2;
	/* CPU temperature module */
	} else if (strcmp(key, "cpu_temp_path") == 0) {
		free(config->cpu_temp_path);
		config->cpu_temp_path = strdup(value);
	} else if (strcmp(key, "cpu_temp_zone") == 0) {
		config->cpu_temp_zone = atoi(value);
	} else if (strcmp(key, "cpu_temp_show_unit") == 0) {
		config->cpu_temp_show_unit = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	/* RAM module */
	} else if (strcmp(key, "ram_show_percentage") == 0) {
		config->ram_show_percentage = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "ram_decimals") == 0) {
		config->ram_decimals = atoi(value);
		if (config->ram_decimals < 0) config->ram_decimals = 0;
		if (config->ram_decimals > 2) config->ram_decimals = 2;
	} else if (strcmp(key, "ram_used_method") == 0) {
		free(config->ram_used_method);
		config->ram_used_method = strdup(value);
	/* Network module */
	} else if (strcmp(key, "network_interface") == 0) {
		free(config->network_interface);
		config->network_interface = strdup(value);
	} else if (strcmp(key, "network_show_ip") == 0) {
		config->network_show_ip = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "network_show_interface") == 0) {
		config->network_show_interface = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	} else if (strcmp(key, "network_prefer_ipv4") == 0) {
		config->network_prefer_ipv4 = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
	/* File read module */
	} else if (strcmp(key, "fileread_path") == 0) {
		free(config->fileread_path);
		config->fileread_path = strdup(value);
	} else if (strcmp(key, "fileread_title") == 0) {
		free(config->fileread_title);
		config->fileread_title = strdup(value);
	} else if (strcmp(key, "fileread_max_chars") == 0) {
		config->fileread_max_chars = atoi(value);
		if (config->fileread_max_chars < 1) config->fileread_max_chars = 1;
		if (config->fileread_max_chars > 256) config->fileread_max_chars = 256;
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
