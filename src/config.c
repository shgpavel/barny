#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <limits.h>
#include <errno.h>

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
	config->workspace_names          = NULL;
	config->workspace_name_count     = 0;
	config->workspace_shape          = NULL;  /* NULL = circle (default) */
	config->workspace_corner_radius  = 4;

	/* Module layout defaults */
	config->module_spacing           = 16;

	/* Sysinfo module defaults */
	config->sysinfo_freq_combined    = true;
	config->sysinfo_freq_decimals    = 2;
	config->sysinfo_power_decimals   = 0;
	config->sysinfo_p_cores          = 0;  /* 0 = auto-detect */
	config->sysinfo_e_cores          = 0;  /* 0 = auto-detect */
	config->sysinfo_item_spacing     = 8;
	config->sysinfo_freq_show_unit   = true;
	config->sysinfo_freq_label_space = true;
	config->sysinfo_freq_unit_space  = true;
	config->sysinfo_power_unit_space = true;
	config->sysinfo_temp_unit_space  = true;

	/* Tray module defaults */
	config->tray_icon_size           = 24;
	config->tray_icon_spacing        = 4;
	config->tray_icon_shape          = NULL;  /* NULL = "circle" */
	config->tray_icon_corner_radius  = 4;
	config->tray_icon_bg_r           = 0.0;
	config->tray_icon_bg_g           = 0.0;
	config->tray_icon_bg_b           = 0.0;
	config->tray_icon_bg_opacity     = 0.3;

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
	config->disk_mode            = NULL;  /* NULL = "used_total" */
	config->disk_decimals        = 0;
	config->disk_unit_space      = false;

	/* Sysinfo temperature defaults */
	config->sysinfo_temp_path      = NULL;  /* auto-detect */
	config->sysinfo_temp_zone      = -1;  /* -1 = auto-detect */
	config->sysinfo_temp_show_unit = true;

	/* RAM module defaults */
	config->ram_mode             = NULL;  /* NULL = "used_total" */
	config->ram_decimals         = 1;
	config->ram_used_method      = NULL;  /* will use "available" */
	config->ram_unit_space       = false;

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
	while (isspace((unsigned char)*str))
		str++;
	if (*str == 0)
		return str;

	char *end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end))
		end--;
	end[1] = '\0';

	return str;
}

static bool
parse_bool(const char *value)
{
	return strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
}

static int
parse_int_clamped(const char *value, int min, int max)
{
	errno = 0;
	long v = strtol(value, NULL, 10);
	if (errno == ERANGE) {
		return v < 0 ? min : max;
	}
	if (v < min) return min;
	if (v > max) return max;
	return (int)v;
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
	} else if (strcmp(key, "workspace_names") == 0) {
		/* Parse comma-separated workspace names */
		/* Free existing names */
		if (config->workspace_names) {
			for (int i = 0; i < config->workspace_name_count; i++) {
				free(config->workspace_names[i]);
			}
			free((void *)config->workspace_names);
		}
		config->workspace_names = NULL;
		config->workspace_name_count = 0;

		/* Count commas to determine array size */
		int count = 1;
		for (const char *p = value; *p; p++) {
			if (*p == ',') count++;
		}

		config->workspace_names = (char **)calloc(count + 1, sizeof(char *));
		if (config->workspace_names) {
			char *tmp = strdup(value);
			char *saveptr;
			char *token = strtok_r(tmp, ",", &saveptr);
			int idx = 0;
			while (token && idx < count) {
				/* Trim whitespace */
				while (isspace((unsigned char)*token)) token++;
				if (*token) {
					char *end = token + strlen(token) - 1;
					while (end > token && isspace((unsigned char)*end)) *end-- = '\0';
				}
				if (*token) {
					char *dup = strdup(token);
					if (dup) {
						config->workspace_names[idx++] = dup;
					}
				}
				token = strtok_r(NULL, ",", &saveptr);
			}
			config->workspace_name_count = idx;
			free(tmp);
		}
	} else if (strcmp(key, "workspace_shape") == 0) {
		free(config->workspace_shape);
		config->workspace_shape = strdup(value);
	} else if (strcmp(key, "workspace_corner_radius") == 0) {
		config->workspace_corner_radius = parse_int_clamped(value, 0, 32);
	} else if (strcmp(key, "sysinfo_freq_combined") == 0) {
		config->sysinfo_freq_combined = parse_bool(value);
	} else if (strcmp(key, "sysinfo_freq_decimals") == 0) {
		config->sysinfo_freq_decimals = parse_int_clamped(value, 0, 2);
	} else if (strcmp(key, "sysinfo_power_decimals") == 0) {
		config->sysinfo_power_decimals = parse_int_clamped(value, 0, 2);
	} else if (strcmp(key, "sysinfo_p_cores") == 0) {
		config->sysinfo_p_cores = parse_int_clamped(value, 0, INT_MAX);
	} else if (strcmp(key, "sysinfo_e_cores") == 0) {
		config->sysinfo_e_cores = parse_int_clamped(value, 0, INT_MAX);
	} else if (strcmp(key, "sysinfo_item_spacing") == 0) {
		config->sysinfo_item_spacing = parse_int_clamped(value, 0, 32);
	} else if (strcmp(key, "sysinfo_freq_show_unit") == 0) {
		config->sysinfo_freq_show_unit = parse_bool(value);
	} else if (strcmp(key, "sysinfo_freq_label_space") == 0) {
		config->sysinfo_freq_label_space = parse_bool(value);
	} else if (strcmp(key, "sysinfo_freq_unit_space") == 0) {
		config->sysinfo_freq_unit_space = parse_bool(value);
	} else if (strcmp(key, "sysinfo_power_unit_space") == 0) {
		config->sysinfo_power_unit_space = parse_bool(value);
	} else if (strcmp(key, "sysinfo_temp_unit_space") == 0) {
		config->sysinfo_temp_unit_space = parse_bool(value);
	} else if (strcmp(key, "module_spacing") == 0) {
		config->module_spacing = parse_int_clamped(value, 0, 64);
	} else if (strcmp(key, "tray_icon_size") == 0) {
		config->tray_icon_size = parse_int_clamped(value, 8, 64);
	} else if (strcmp(key, "tray_icon_spacing") == 0) {
		config->tray_icon_spacing = parse_int_clamped(value, 0, 32);
	} else if (strcmp(key, "tray_icon_shape") == 0) {
		free(config->tray_icon_shape);
		config->tray_icon_shape = strdup(value);
	} else if (strcmp(key, "tray_icon_corner_radius") == 0) {
		config->tray_icon_corner_radius = parse_int_clamped(value, 0, 32);
	} else if (strcmp(key, "tray_icon_bg_color") == 0) {
		parse_hex_color(value, &config->tray_icon_bg_r,
		                &config->tray_icon_bg_g,
		                &config->tray_icon_bg_b);
	} else if (strcmp(key, "tray_icon_bg_opacity") == 0) {
		double v = atof(value);
		if (v < 0.0) v = 0.0;
		if (v > 1.0) v = 1.0;
		config->tray_icon_bg_opacity = v;
	/* Clock module */
	} else if (strcmp(key, "clock_show_time") == 0) {
		config->clock_show_time = parse_bool(value);
	} else if (strcmp(key, "clock_24h_format") == 0) {
		config->clock_24h_format = parse_bool(value);
	} else if (strcmp(key, "clock_show_seconds") == 0) {
		config->clock_show_seconds = parse_bool(value);
	} else if (strcmp(key, "clock_show_date") == 0) {
		config->clock_show_date = parse_bool(value);
	} else if (strcmp(key, "clock_show_year") == 0) {
		config->clock_show_year = parse_bool(value);
	} else if (strcmp(key, "clock_show_month") == 0) {
		config->clock_show_month = parse_bool(value);
	} else if (strcmp(key, "clock_show_day") == 0) {
		config->clock_show_day = parse_bool(value);
	} else if (strcmp(key, "clock_show_weekday") == 0) {
		config->clock_show_weekday = parse_bool(value);
	} else if (strcmp(key, "clock_date_order") == 0) {
		config->clock_date_order = parse_int_clamped(value, 0, 2);
	} else if (strcmp(key, "clock_date_separator") == 0) {
		if (strlen(value) > 0) config->clock_date_separator = value[0];
	/* Disk module */
	} else if (strcmp(key, "disk_path") == 0) {
		free(config->disk_path);
		config->disk_path = strdup(value);
	} else if (strcmp(key, "disk_mode") == 0) {
		free(config->disk_mode);
		config->disk_mode = strdup(value);
	} else if (strcmp(key, "disk_decimals") == 0) {
		config->disk_decimals = parse_int_clamped(value, 0, 2);
	} else if (strcmp(key, "disk_unit_space") == 0) {
		config->disk_unit_space = parse_bool(value);
	/* Sysinfo temperature */
	} else if (strcmp(key, "sysinfo_temp_path") == 0) {
		free(config->sysinfo_temp_path);
		config->sysinfo_temp_path = strdup(value);
	} else if (strcmp(key, "sysinfo_temp_zone") == 0) {
		config->sysinfo_temp_zone = atoi(value);
	} else if (strcmp(key, "sysinfo_temp_show_unit") == 0) {
		config->sysinfo_temp_show_unit = parse_bool(value);
	/* RAM module */
	} else if (strcmp(key, "ram_mode") == 0) {
		free(config->ram_mode);
		config->ram_mode = strdup(value);
	} else if (strcmp(key, "ram_decimals") == 0) {
		config->ram_decimals = parse_int_clamped(value, 0, 2);
	} else if (strcmp(key, "ram_unit_space") == 0) {
		config->ram_unit_space = parse_bool(value);
	} else if (strcmp(key, "ram_used_method") == 0) {
		free(config->ram_used_method);
		config->ram_used_method = strdup(value);
	/* Network module */
	} else if (strcmp(key, "network_interface") == 0) {
		free(config->network_interface);
		config->network_interface = strdup(value);
	} else if (strcmp(key, "network_show_ip") == 0) {
		config->network_show_ip = parse_bool(value);
	} else if (strcmp(key, "network_show_interface") == 0) {
		config->network_show_interface = parse_bool(value);
	} else if (strcmp(key, "network_prefer_ipv4") == 0) {
		config->network_prefer_ipv4 = parse_bool(value);
	/* File read module */
	} else if (strcmp(key, "fileread_path") == 0) {
		free(config->fileread_path);
		config->fileread_path = strdup(value);
	} else if (strcmp(key, "fileread_title") == 0) {
		free(config->fileread_title);
		config->fileread_title = strdup(value);
	} else if (strcmp(key, "fileread_max_chars") == 0) {
		config->fileread_max_chars = parse_int_clamped(value, 1, 256);
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

		/* Strip inline comments (# not inside quotes) */
		{
			bool in_quotes = false;
			for (char *p = value; *p; p++) {
				if (*p == '"') {
					in_quotes = !in_quotes;
				} else if (*p == '#' && !in_quotes &&
				           p > value && isspace((unsigned char)*(p - 1))) {
					char *end = p - 1;
					while (end > value && isspace((unsigned char)*end))
						end--;
					*(end + 1) = '\0';
					break;
				}
			}
		}

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
