#include <SDL3/SDL.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "barny.h"

#define EDITOR_WINDOW_W    1200
#define EDITOR_WINDOW_H    760
#define BLOCK_HEIGHT       36.0f
#define BLOCK_GAP          10.0f
#define SLOT_PAD           10.0f
#define MAX_UI_BLOCKS      256
#define CLICK_THRESHOLD_PX 6.0f
#define MAX_DETAIL_FIELDS  20
#define MAX_DETAIL_OPTIONS 8
#define DETAIL_ROW_H       24.0f
#define DETAIL_CTRL_W      228.0f
#define GAP_TOKEN_LEN      32

/*
 * UI color palette. SDL3 uses 8-bit RGBA. Centralizing the values keeps the
 * editor's look-and-feel coherent and makes future re-skinning a one-stop
 * change. Naming mirrors usage rather than raw color so that intent reads
 * cleanly at the call sites.
 */
typedef struct {
	Uint8 r, g, b, a;
} ui_color_t;

static const ui_color_t UI_COLOR_PANEL_BG     = { 22, 26, 34, 242 };
static const ui_color_t UI_COLOR_PANEL_BORDER = { 104, 126, 165, 255 };
static const ui_color_t UI_COLOR_OVERLAY_DIM  = { 0, 0, 0, 180 };
static const ui_color_t UI_COLOR_TEXT_MUTED   = { 220, 220, 220, 255 };
static const ui_color_t UI_COLOR_TEXT_PRIMARY = { 238, 242, 250, 255 };
static const ui_color_t UI_COLOR_TEXT_LABEL   = { 232, 238, 248, 255 };
static const ui_color_t UI_COLOR_STATUS_WARN  = { 255, 226, 160, 255 };
static const ui_color_t UI_COLOR_INFO_AMBER   = { 233, 208, 145, 255 };

static inline void
ui_set_draw_color(SDL_Renderer *renderer, ui_color_t color)
{
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

typedef struct {
	SDL_FRect bar;
	SDL_FRect bar_slot;
	SDL_FRect pool;
} ui_regions_t;

typedef enum { BLOCK_SRC_BAR, BLOCK_SRC_POOL } block_source_t;

typedef struct {
	const char    *name;
	SDL_FRect      rect;
	block_source_t source;
	int            index;
} ui_block_t;

typedef struct {
	ui_block_t blocks[MAX_UI_BLOCKS];
	int        count;
} block_map_t;

typedef struct {
	char *name;
	float x_rel;
} placed_module_t;

typedef struct {
	placed_module_t items[BARNY_MAX_MODULES];
	int             count;
} bar_layout_t;

typedef struct {
	bool  active;
	char *name;
	bool  source_in_bar;
	float source_x_rel;
	float mouse_x;
	float mouse_y;
	float start_mouse_x;
	float start_mouse_y;
	float offset_x;
	float offset_y;
	char  click_name[32];
} drag_state_t;

typedef struct {
	bool  valid;
	bool  to_pool;
	bool  to_bar;
	float x_rel;
} drop_target_t;

typedef enum {
	DETAIL_FIELD_BOOL,
	DETAIL_FIELD_ENUM_INT,
	DETAIL_FIELD_ENUM_CHAR,
	DETAIL_FIELD_ENUM_STR
} detail_field_kind_t;

typedef struct {
	const char *label;
	const char *config_value;
	int         int_value;
	char        char_value;
	const char *str_value;
} detail_option_t;

typedef struct {
	const char         *label;
	const char         *key;
	detail_field_kind_t kind;
	bool               *bool_ptr;
	int                *int_ptr;
	char               *char_ptr;
	char              **str_ptr;
	const char         *default_str;
	detail_option_t     options[MAX_DETAIL_OPTIONS];
	int                 option_count;
	bool                dropdown_open;
	SDL_FRect           control_rect;
	SDL_FRect           option_rects[MAX_DETAIL_OPTIONS];
} detail_field_t;

typedef struct {
	bool           open;
	char           module[32];
	detail_field_t fields[MAX_DETAIL_FIELDS];
	int            field_count;
	SDL_FRect      panel_rect;
	SDL_FRect      save_rect;
	SDL_FRect      close_rect;
} module_details_t;

typedef enum {
	DETAIL_CLICK_NONE = 0,
	DETAIL_CLICK_CHANGED,
	DETAIL_CLICK_SAVED,
	DETAIL_CLICK_SAVE_FAILED,
	DETAIL_CLICK_CLOSED
} detail_click_result_t;

static int
ensure_parent_dirs(const char *path);

static float
module_block_width(const char *name)
{
	size_t chars = name ? strlen(name) : 0;
	float  w     = 24.0f + (float)(chars * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
	if (w < 72.0f) {
		w = 72.0f;
	}
	return w;
}

static bool
point_in_rect(float x, float y, const SDL_FRect *rect)
{
	return rect
	       && x >= rect->x
	       && x <= rect->x + rect->w
	       && y >= rect->y
	       && y <= rect->y + rect->h;
}

static float
clampf(float v, float minv, float maxv)
{
	if (v < minv) {
		return minv;
	}
	if (v > maxv) {
		return maxv;
	}
	return v;
}

static const char *
bool_str(bool value)
{
	return value ? "true" : "false";
}

static void
module_details_reset(module_details_t *details)
{
	if (!details) {
		return;
	}
	memset(details, 0, sizeof(*details));
}

static void
detail_field_add_options(detail_field_t *field, const detail_option_t *options,
                         int option_count)
{
	if (!field || !options || option_count <= 0) {
		return;
	}

	if (option_count > MAX_DETAIL_OPTIONS) {
		option_count = MAX_DETAIL_OPTIONS;
	}
	for (int i = 0; i < option_count; i++) {
		field->options[i] = options[i];
	}
	field->option_count = option_count;
}

static detail_field_t *
module_details_add_field(module_details_t *details, const char *label,
                         const char *key, detail_field_kind_t kind)
{
	detail_field_t *field;

	if (!details
	    || details->field_count >= MAX_DETAIL_FIELDS
	    || !label
	    || !key) {
		return NULL;
	}

	field = &details->fields[details->field_count++];
	memset(field, 0, sizeof(*field));
	field->label = label;
	field->key   = key;
	field->kind  = kind;
	return field;
}

static void
module_details_add_bool(module_details_t *details, const char *label,
                        const char *key, bool *value_ptr)
{
	detail_field_t *field
	        = module_details_add_field(details, label, key, DETAIL_FIELD_BOOL);
	if (field) {
		field->bool_ptr = value_ptr;
	}
}

static void
module_details_add_enum_int(module_details_t *details, const char *label,
                            const char *key, int *value_ptr,
                            const detail_option_t *options, int option_count)
{
	detail_field_t *field = module_details_add_field(details, label, key,
	                                                 DETAIL_FIELD_ENUM_INT);
	if (!field) {
		return;
	}
	field->int_ptr = value_ptr;
	detail_field_add_options(field, options, option_count);
}

static void
module_details_add_enum_char(module_details_t *details, const char *label,
                             const char *key, char *value_ptr,
                             const detail_option_t *options, int option_count)
{
	detail_field_t *field = module_details_add_field(details, label, key,
	                                                 DETAIL_FIELD_ENUM_CHAR);
	if (!field) {
		return;
	}
	field->char_ptr = value_ptr;
	detail_field_add_options(field, options, option_count);
}

static void
module_details_add_enum_str(module_details_t *details, const char *label,
                            const char *key, char **value_ptr,
                            const char            *default_str,
                            const detail_option_t *options, int option_count)
{
	detail_field_t *field = module_details_add_field(details, label, key,
	                                                 DETAIL_FIELD_ENUM_STR);
	if (!field) {
		return;
	}
	field->str_ptr     = value_ptr;
	field->default_str = default_str;
	detail_field_add_options(field, options, option_count);
}

static int
detail_field_selected_index(const detail_field_t *field)
{
	const char *str_value;

	if (!field || field->option_count <= 0) {
		return -1;
	}

	switch (field->kind) {
	case DETAIL_FIELD_ENUM_INT:
		if (!field->int_ptr) {
			return -1;
		}
		for (int i = 0; i < field->option_count; i++) {
			if (field->options[i].int_value == *field->int_ptr) {
				return i;
			}
		}
		break;
	case DETAIL_FIELD_ENUM_CHAR:
		if (!field->char_ptr) {
			return -1;
		}
		for (int i = 0; i < field->option_count; i++) {
			if (field->options[i].char_value == *field->char_ptr) {
				return i;
			}
		}
		break;
	case DETAIL_FIELD_ENUM_STR:
		if (!field->str_ptr) {
			return -1;
		}
		str_value = (*field->str_ptr && **field->str_ptr) ?
		                    *field->str_ptr :
		                    field->default_str;
		for (int i = 0; i < field->option_count; i++) {
			if (field->options[i].str_value
			    && str_value
			    && strcmp(field->options[i].str_value, str_value)
			               == 0) {
				return i;
			}
		}
		break;
	case DETAIL_FIELD_BOOL:
	default:
		break;
	}

	return -1;
}

static const char *
detail_field_selected_label(const detail_field_t *field)
{
	int idx = detail_field_selected_index(field);

	if (idx >= 0 && idx < field->option_count && field->options[idx].label) {
		return field->options[idx].label;
	}
	return "(select)";
}

static int
detail_field_write_value(const detail_field_t *field, char *out, size_t out_len)
{
	int idx = detail_field_selected_index(field);

	if (!field || !out || out_len == 0) {
		return -1;
	}

	switch (field->kind) {
	case DETAIL_FIELD_BOOL:
		if (!field->bool_ptr) {
			return -1;
		}
		snprintf(out, out_len, "%s", bool_str(*field->bool_ptr));
		return 0;
	case DETAIL_FIELD_ENUM_INT:
	case DETAIL_FIELD_ENUM_CHAR:
	case DETAIL_FIELD_ENUM_STR:
		if (idx >= 0
		    && idx < field->option_count
		    && field->options[idx].config_value) {
			snprintf(out, out_len, "%s",
			         field->options[idx].config_value);
			return 0;
		}
		break;
	default:
		break;
	}

	return -1;
}

static void
module_details_close_dropdowns(module_details_t *details)
{
	if (!details) {
		return;
	}
	for (int i = 0; i < details->field_count; i++) {
		details->fields[i].dropdown_open = false;
	}
}

static char *
trim_local(char *str)
{
	char *end;

	while (*str && isspace((unsigned char)*str)) {
		str++;
	}
	if (*str == '\0') {
		return str;
	}
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) {
		*end = '\0';
		end--;
	}
	return str;
}

static int
save_module_config_fields(const char *path, const module_details_t *details)
{
	char  tmp_path[PATH_MAX];
	FILE *in                      = NULL;
	FILE *out                     = NULL;
	bool  seen[MAX_DETAIL_FIELDS] = { 0 };
	int   rc                      = -1;

	if (!path || !details) {
		return -1;
	}
	if (ensure_parent_dirs(path) < 0) {
		return -1;
	}
	if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path)
	    >= (int)sizeof(tmp_path)) {
		return -1;
	}

	in = fopen(path, "r");
	if (!in && errno != ENOENT) {
		return -1;
	}

	out = fopen(tmp_path, "w");
	if (!out) {
		if (in) {
			fclose(in);
		}
		return -1;
	}

	if (in) {
		char line[2048];

		while (fgets(line, sizeof(line), in)) {
			char keybuf[2048];
			char valuebuf[64];
			bool matched = false;

			snprintf(keybuf, sizeof(keybuf), "%s", line);
			char *trimmed = trim_local(keybuf);
			if (*trimmed == '#' || *trimmed == '\0') {
				fputs(line, out);
				continue;
			}

			char *eq = strchr(trimmed, '=');
			if (!eq) {
				fputs(line, out);
				continue;
			}

			*eq       = '\0';
			char *key = trim_local(trimmed);

			for (int i = 0; i < details->field_count; i++) {
				if (strcmp(details->fields[i].key, key) != 0) {
					continue;
				}
				if (detail_field_write_value(&details->fields[i],
				                             valuebuf,
				                             sizeof(valuebuf))
				    == 0) {
					fprintf(out, "%s=%s\n",
					        details->fields[i].key, valuebuf);
					seen[i] = true;
					matched = true;
				}
				break;
			}

			if (!matched) {
				fputs(line, out);
			}
		}

		fclose(in);
		in = NULL;
	}

	for (int i = 0; i < details->field_count; i++) {
		char valuebuf[64];

		if (seen[i]) {
			continue;
		}
		if (detail_field_write_value(&details->fields[i], valuebuf,
		                             sizeof(valuebuf))
		    == 0) {
			fprintf(out, "%s=%s\n", details->fields[i].key, valuebuf);
		}
	}

	if (fclose(out) != 0) {
		/*
		 * Treat stream as consumed after fclose() regardless of status to
		 * avoid closing the same FILE* again in the shared cleanup path.
		 */
		out = NULL;
		goto out;
	}
	out = NULL;

	if (rename(tmp_path, path) < 0) {
		goto out;
	}

	rc = 0;

out:
	if (in) {
		fclose(in);
	}
	if (out) {
		fclose(out);
	}
	if (rc < 0) {
		remove(tmp_path);
	}
	return rc;
}

/*
 * Per-module field initializers. Each function appends the editable bool/enum
 * fields for a single module to `details` using the shared add_bool/add_enum_*
 * helpers. Splitting them out keeps `module_details_open` to a table dispatch.
 */
static void
module_details_init_clock(module_details_t *details, barny_config_t *config)
{
	static const detail_option_t date_order[] = {
		{ "dd/mm/yyyy", "0", 0, 0, NULL },
		{ "mm/dd/yyyy", "1", 1, 0, NULL },
		{ "yyyy/mm/dd", "2", 2, 0, NULL },
	};
	static const detail_option_t date_separator[] = {
		{ "/", "/", 0, '/', NULL },
		{ "-", "-", 0, '-', NULL },
		{ ".", ".", 0, '.', NULL },
	};
	module_details_add_bool(details, "Show time", "clock_show_time",
	                        &config->clock_show_time);
	module_details_add_bool(details, "Use 24h format", "clock_24h_format",
	                        &config->clock_24h_format);
	module_details_add_bool(details, "Show seconds", "clock_show_seconds",
	                        &config->clock_show_seconds);
	module_details_add_bool(details, "Show date", "clock_show_date",
	                        &config->clock_show_date);
	module_details_add_bool(details, "Show weekday", "clock_show_weekday",
	                        &config->clock_show_weekday);
	module_details_add_bool(details, "Show day", "clock_show_day",
	                        &config->clock_show_day);
	module_details_add_bool(details, "Show month", "clock_show_month",
	                        &config->clock_show_month);
	module_details_add_bool(details, "Show year", "clock_show_year",
	                        &config->clock_show_year);
	module_details_add_enum_int(details, "Date order", "clock_date_order",
	                            &config->clock_date_order, date_order, 3);
	module_details_add_enum_char(details, "Date separator",
	                             "clock_date_separator",
	                             &config->clock_date_separator,
	                             date_separator, 3);
}

static void
module_details_init_workspace(module_details_t *details, barny_config_t *config)
{
	static const detail_option_t shape_options[] = {
		{ "circle", "circle", 0, 0, "circle" },
		{ "square", "square", 0, 0, "square" },
	};
	module_details_add_enum_str(details, "Shape", "workspace_shape",
	                            &config->workspace_shape, "circle",
	                            shape_options, 2);
}

static void
module_details_init_sysinfo(module_details_t *details, barny_config_t *config)
{
	module_details_add_bool(details, "Combined frequency view",
	                        "sysinfo_freq_combined",
	                        &config->sysinfo_freq_combined);
	module_details_add_bool(details, "Show frequency unit",
	                        "sysinfo_freq_show_unit",
	                        &config->sysinfo_freq_show_unit);
	module_details_add_bool(details, "Space after freq label",
	                        "sysinfo_freq_label_space",
	                        &config->sysinfo_freq_label_space);
	module_details_add_bool(details, "Space before GHz",
	                        "sysinfo_freq_unit_space",
	                        &config->sysinfo_freq_unit_space);
	module_details_add_bool(details, "Space before W",
	                        "sysinfo_power_unit_space",
	                        &config->sysinfo_power_unit_space);
	module_details_add_bool(details, "Show temp unit",
	                        "sysinfo_temp_show_unit",
	                        &config->sysinfo_temp_show_unit);
	module_details_add_bool(details, "Space before C",
	                        "sysinfo_temp_unit_space",
	                        &config->sysinfo_temp_unit_space);
}

static void
module_details_init_tray(module_details_t *details, barny_config_t *config)
{
	static const detail_option_t shape_options[] = {
		{ "circle", "circle", 0, 0, "circle" },
		{ "square", "square", 0, 0, "square" },
	};
	module_details_add_enum_str(details, "Icon shape", "tray_icon_shape",
	                            &config->tray_icon_shape, "circle",
	                            shape_options, 2);
}

static void
module_details_init_disk(module_details_t *details, barny_config_t *config)
{
	static const detail_option_t mode_options[] = {
		{ "percentage", "percentage", 0, 0, "percentage" },
		{ "used_total", "used_total", 0, 0, "used_total" },
		{ "free",       "free",       0, 0, "free"       },
	};
	module_details_add_enum_str(details, "Mode", "disk_mode",
	                            &config->disk_mode, "used_total",
	                            mode_options, 3);
	module_details_add_bool(details, "Space before units",
	                        "disk_unit_space", &config->disk_unit_space);
}

static void
module_details_init_ram(module_details_t *details, barny_config_t *config)
{
	static const detail_option_t mode_options[] = {
		{ "percentage", "percentage", 0, 0, "percentage" },
		{ "used_total", "used_total", 0, 0, "used_total" },
		{ "used",       "used",       0, 0, "used"       },
		{ "free",       "free",       0, 0, "free"       },
	};
	static const detail_option_t used_method_options[] = {
		{ "available", "available", 0, 0, "available" },
		{ "free",      "free",      0, 0, "free"      },
	};
	module_details_add_enum_str(details, "Mode", "ram_mode",
	                            &config->ram_mode, "used_total",
	                            mode_options, 4);
	module_details_add_enum_str(details, "Used method", "ram_used_method",
	                            &config->ram_used_method, "available",
	                            used_method_options, 2);
	module_details_add_bool(details, "Space before units", "ram_unit_space",
	                        &config->ram_unit_space);
}

static void
module_details_init_network(module_details_t *details, barny_config_t *config)
{
	module_details_add_bool(details, "Show IP", "network_show_ip",
	                        &config->network_show_ip);
	module_details_add_bool(details, "Show interface",
	                        "network_show_interface",
	                        &config->network_show_interface);
	module_details_add_bool(details, "Prefer IPv4", "network_prefer_ipv4",
	                        &config->network_prefer_ipv4);
}

static void
module_details_init_fileread(module_details_t *details, barny_config_t *config)
{
	/* Text/number fields are currently read-only in this editor. */
	(void)details;
	(void)config;
}

static void
module_details_init_battery(module_details_t *details, barny_config_t *config)
{
	module_details_add_bool(details, "Show charging status",
	                        "battery_show_status",
	                        &config->battery_show_status);
	module_details_add_bool(details, "Space before percent",
	                        "battery_unit_space",
	                        &config->battery_unit_space);
}

typedef void (*module_opener_fn)(module_details_t *details,
                                 barny_config_t   *config);

static const struct {
	const char       *name;
	module_opener_fn  open;
} module_openers[] = {
	{ "clock",     module_details_init_clock     },
	{ "workspace", module_details_init_workspace },
	{ "sysinfo",   module_details_init_sysinfo   },
	{ "tray",      module_details_init_tray      },
	{ "disk",      module_details_init_disk      },
	{ "ram",       module_details_init_ram       },
	{ "network",   module_details_init_network   },
	{ "fileread",  module_details_init_fileread  },
	{ "battery",   module_details_init_battery   },
};

static void
module_details_open(module_details_t *details, barny_config_t *config,
                    const char *module_name)
{
	if (!details || !config || !module_name || !*module_name) {
		return;
	}

	module_details_reset(details);
	details->open = true;
	snprintf(details->module, sizeof(details->module), "%s", module_name);

	for (size_t i = 0;
	     i < sizeof(module_openers) / sizeof(module_openers[0]); i++) {
		if (strcmp(module_name, module_openers[i].name) == 0) {
			module_openers[i].open(details, config);
			return;
		}
	}
}

static void
compute_regions(int width, int height, ui_regions_t *ui)
{
	float bar_margin_x = 32.0f;
	float bar_top      = 74.0f;
	float bar_height   = 96.0f;

	ui->bar.x          = bar_margin_x;
	ui->bar.y          = bar_top;
	ui->bar.w          = (float)width - bar_margin_x * 2.0f;
	ui->bar.h          = bar_height;

	ui->bar_slot.x     = ui->bar.x + 12.0f;
	ui->bar_slot.y     = ui->bar.y + 10.0f;
	ui->bar_slot.w     = ui->bar.w - 24.0f;
	ui->bar_slot.h     = ui->bar.h - 20.0f;

	ui->pool.x         = 32.0f;
	ui->pool.y         = ui->bar.y + ui->bar.h + 38.0f;
	ui->pool.w         = (float)width - 64.0f;
	ui->pool.h         = (float)height - ui->pool.y - 60.0f;
	if (ui->pool.h < 180.0f) {
		ui->pool.h = 180.0f;
	}
}

static void
bar_layout_init(bar_layout_t *bar)
{
	if (!bar) {
		return;
	}
	memset(bar, 0, sizeof(*bar));
}

static void
bar_layout_clear(bar_layout_t *bar)
{
	if (!bar) {
		return;
	}

	for (int i = 0; i < bar->count; i++) {
		free(bar->items[i].name);
		bar->items[i].name  = NULL;
		bar->items[i].x_rel = 0.0f;
	}
	bar->count = 0;
}

static bool
bar_layout_contains(const bar_layout_t *bar, const char *name)
{
	if (!bar || !name) {
		return false;
	}

	for (int i = 0; i < bar->count; i++) {
		if (bar->items[i].name && strcmp(bar->items[i].name, name) == 0) {
			return true;
		}
	}

	return false;
}

static int
placed_cmp(const void *a, const void *b)
{
	const placed_module_t *pa = a;
	const placed_module_t *pb = b;
	if (pa->x_rel < pb->x_rel) {
		return -1;
	}
	if (pa->x_rel > pb->x_rel) {
		return 1;
	}
	return 0;
}

static void
bar_layout_sort(bar_layout_t *bar)
{
	if (!bar || bar->count <= 1) {
		return;
	}

	qsort(bar->items, (size_t)bar->count, sizeof(bar->items[0]), placed_cmp);
}

static void
bar_layout_constrain_to_width(bar_layout_t *bar, float content_width)
{
	if (!bar || bar->count <= 0 || content_width <= 1.0f) {
		return;
	}

	bar_layout_sort(bar);

	/* Keep modules ordered and non-overlapping first. */
	float right = 0.0f;
	for (int i = 0; i < bar->count; i++) {
		float w = module_block_width(bar->items[i].name);
		if (bar->items[i].x_rel < right) {
			bar->items[i].x_rel = right;
		}
		if (bar->items[i].x_rel < 0.0f) {
			bar->items[i].x_rel = 0.0f;
		}
		right = bar->items[i].x_rel + w;
	}

	float overflow = right - content_width;
	if (overflow > 0.0f) {
		/* Shrink leading and inter-module gaps proportionally to fit width. */
		float gaps[BARNY_MAX_MODULES] = { 0 };
		float total_gaps              = 0.0f;

		gaps[0]                       = bar->items[0].x_rel;
		if (gaps[0] > 0.0f) {
			total_gaps += gaps[0];
		}

		for (int i = 1; i < bar->count; i++) {
			float prev_right
			        = bar->items[i - 1].x_rel
			          + module_block_width(bar->items[i - 1].name);
			float gap = bar->items[i].x_rel - prev_right;
			if (gap > 0.0f) {
				gaps[i]     = gap;
				total_gaps += gaps[i];
			}
		}

		if (total_gaps > 0.0f) {
			float keep  = total_gaps - overflow;
			float scale = (keep > 0.0f) ? (keep / total_gaps) : 0.0f;

			bar->items[0].x_rel = gaps[0] * scale;

			for (int i = 1; i < bar->count; i++) {
				float prev_w = module_block_width(
				        bar->items[i - 1].name);
				float prev_right
				        = bar->items[i - 1].x_rel + prev_w;
				bar->items[i].x_rel = prev_right + gaps[i] * scale;
			}
		}
	}

	/* Final hard clamp so each module box remains inside bar bounds. */
	float cursor = 0.0f;
	for (int i = 0; i < bar->count; i++) {
		float w     = module_block_width(bar->items[i].name);
		float max_x = content_width - w;
		float x     = bar->items[i].x_rel;

		if (max_x < 0.0f) {
			max_x = 0.0f;
		}
		if (x < cursor) {
			x = cursor;
		}
		if (x > max_x) {
			x = max_x;
		}

		bar->items[i].x_rel = x;
		cursor              = x + w;
	}
}

static int
bar_layout_add_owned(bar_layout_t *bar, char *name, float x_rel)
{
	if (!bar || !name || !*name) {
		return -1;
	}
	if (!barny_module_catalog_has(name)
	    || barny_module_layout_gap_units(name) > 0) {
		return -1;
	}
	if (bar_layout_contains(bar, name)) {
		return -1;
	}
	if (bar->count >= BARNY_MAX_MODULES) {
		return -1;
	}

	bar->items[bar->count].name  = name;
	bar->items[bar->count].x_rel = x_rel;
	bar->count++;
	bar_layout_sort(bar);
	return 0;
}

static int
bar_layout_add_copy(bar_layout_t *bar, const char *name, float x_rel)
{
	char *copy;

	if (!name) {
		return -1;
	}

	copy = strdup(name);
	if (!copy) {
		return -1;
	}

	if (bar_layout_add_owned(bar, copy, x_rel) < 0) {
		free(copy);
		return -1;
	}

	return 0;
}

static char *
bar_layout_take_index(bar_layout_t *bar, int index, float *x_rel)
{
	char *name;

	if (!bar || index < 0 || index >= bar->count) {
		return NULL;
	}

	name = bar->items[index].name;
	if (x_rel) {
		*x_rel = bar->items[index].x_rel;
	}

	for (int i = index; i < bar->count - 1; i++) {
		bar->items[i] = bar->items[i + 1];
	}
	bar->items[bar->count - 1].name  = NULL;
	bar->items[bar->count - 1].x_rel = 0.0f;
	bar->count--;
	return name;
}

static void
load_tokens_into_bar(bar_layout_t *bar, char *const *tokens, int count,
                     int spacing)
{
	float x_rel = 0.0f;

	for (int i = 0; i < count; i++) {
		if (!tokens[i]) {
			continue;
		}

		int gap_units = barny_module_layout_gap_units(tokens[i]);
		if (gap_units > 0) {
			x_rel += (float)(gap_units * spacing);
			continue;
		}

		if (!barny_module_catalog_has(tokens[i])) {
			continue;
		}

		if (bar_layout_add_copy(bar, tokens[i], x_rel) == 0) {
			x_rel += module_block_width(tokens[i]) + (float)spacing;
		}
	}
}

static void
bar_layout_from_module_layout(bar_layout_t                *bar,
                              const barny_module_layout_t *layout,
                              int                          spacing)
{
	if (!bar || !layout) {
		return;
	}

	bar_layout_clear(bar);
	load_tokens_into_bar(bar, layout->left, layout->left_count, spacing);
	load_tokens_into_bar(bar, layout->center, layout->center_count, spacing);
	load_tokens_into_bar(bar, layout->right, layout->right_count, spacing);
}

static void
bar_layout_load_defaults(bar_layout_t *bar, int spacing)
{
	barny_module_layout_t layout;

	barny_module_layout_init(&layout);
	barny_module_layout_set_defaults(&layout);
	bar_layout_from_module_layout(bar, &layout, spacing);
	barny_module_layout_destroy(&layout);
}

static int
build_pool(const bar_layout_t *bar, const char **pool, int pool_cap)
{
	const char *catalog[BARNY_MAX_MODULES];
	int         total = barny_module_catalog_names(catalog, BARNY_MAX_MODULES);
	int         pool_count = 0;
	int         limit = total < BARNY_MAX_MODULES ? total : BARNY_MAX_MODULES;

	for (int i = 0; i < limit && pool_count < pool_cap; i++) {
		if (!bar_layout_contains(bar, catalog[i])) {
			pool[pool_count++] = catalog[i];
		}
	}

	return pool_count;
}

static void
append_block(block_map_t *map, const char *name, const SDL_FRect *rect,
             block_source_t source, int index)
{
	if (!map || !name || !rect || map->count >= MAX_UI_BLOCKS) {
		return;
	}

	map->blocks[map->count].name   = name;
	map->blocks[map->count].rect   = *rect;
	map->blocks[map->count].source = source;
	map->blocks[map->count].index  = index;
	map->count++;
}

static void
build_bar_blocks(const bar_layout_t *bar, const SDL_FRect *bar_rect,
                 block_map_t *map)
{
	float start_x = bar_rect->x + SLOT_PAD;
	float y       = bar_rect->y + (bar_rect->h - BLOCK_HEIGHT) * 0.5f;

	for (int i = 0; i < bar->count; i++) {
		if (!bar->items[i].name) {
			continue;
		}

		float     w = module_block_width(bar->items[i].name);
		SDL_FRect rect
		        = { start_x + bar->items[i].x_rel, y, w, BLOCK_HEIGHT };
		append_block(map, bar->items[i].name, &rect, BLOCK_SRC_BAR, i);
	}
}

static void
build_pool_blocks(const char *const *pool, int pool_count,
                  const SDL_FRect *pool_rect, block_map_t *map)
{
	float x         = pool_rect->x + SLOT_PAD;
	float y         = pool_rect->y + 28.0f;
	float line_h    = BLOCK_HEIGHT + BLOCK_GAP;
	float max_right = pool_rect->x + pool_rect->w - SLOT_PAD;

	for (int i = 0; i < pool_count; i++) {
		if (!pool[i]) {
			continue;
		}

		float w = module_block_width(pool[i]);
		if (x + w > max_right) {
			x  = pool_rect->x + SLOT_PAD;
			y += line_h;
		}

		SDL_FRect rect = { x, y, w, BLOCK_HEIGHT };
		append_block(map, pool[i], &rect, BLOCK_SRC_POOL, i);
		x += w + BLOCK_GAP;
	}
}

static void
build_block_map(const bar_layout_t *bar, const ui_regions_t *ui, const char **pool,
                int pool_count, block_map_t *map)
{
	if (!map) {
		return;
	}

	map->count = 0;
	build_bar_blocks(bar, &ui->bar_slot, map);
	build_pool_blocks(pool, pool_count, &ui->pool, map);
}

static drop_target_t
compute_drop_target(float x, float y, const ui_regions_t *ui)
{
	drop_target_t target = { 0 };
	float         start_x;
	float         max_rel;

	if (point_in_rect(x, y, &ui->pool)) {
		target.valid   = true;
		target.to_pool = true;
		return target;
	}

	if (!point_in_rect(x, y, &ui->bar)
	    && !point_in_rect(x, y, &ui->bar_slot)) {
		return target;
	}

	start_x = ui->bar_slot.x + SLOT_PAD;
	max_rel = ui->bar_slot.w - SLOT_PAD * 2.0f;
	if (max_rel < 0.0f) {
		max_rel = 0.0f;
	}
	target.valid  = true;
	target.to_bar = true;
	target.x_rel  = x - start_x;
	target.x_rel  = clampf(target.x_rel, 0.0f, max_rel);
	return target;
}

static int
ensure_parent_dirs(const char *path)
{
	char *slash = NULL;
	char  dir[PATH_MAX];

	if (!path || !*path) {
		return -1;
	}

	if (strlen(path) >= sizeof(dir)) {
		return -1;
	}

	snprintf(dir, sizeof(dir), "%s", path);
	slash = strrchr(dir, '/');
	if (!slash) {
		return 0;
	}
	*slash = '\0';

	if (dir[0] == '\0') {
		return 0;
	}

	for (char *p = dir + 1; *p; p++) {
		if (*p != '/') {
			continue;
		}
		*p = '\0';
		if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
			return -1;
		}
		*p = '/';
	}

	if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
		return -1;
	}

	return 0;
}

static int
save_layout(const char *config_path, const bar_layout_t *bar, int spacing)
{
	const char *tokens[BARNY_MAX_MODULES * 2];
	char       *gap_tokens[BARNY_MAX_MODULES];
	int         token_count = 0;
	int         gap_count   = 0;
	char       *csv         = NULL;
	int         rc          = -1;

	if (!bar || spacing < 1) {
		return -1;
	}

	if (bar->count > 0) {
		int lead_units
		        = (int)lroundf(bar->items[0].x_rel / (float)spacing);
		if (lead_units > 0) {
			gap_tokens[gap_count] = malloc(GAP_TOKEN_LEN);
			if (!gap_tokens[gap_count]) {
				goto out;
			}
			snprintf(gap_tokens[gap_count], GAP_TOKEN_LEN,
			         "gap:%d", lead_units);
			tokens[token_count++] = gap_tokens[gap_count++];
		}
	}

	for (int i = 0; i < bar->count; i++) {
		float right;
		tokens[token_count++] = bar->items[i].name;

		if (i >= bar->count - 1) {
			continue;
		}

		right = bar->items[i].x_rel
		        + module_block_width(bar->items[i].name);
		float gap_px = bar->items[i + 1].x_rel - right;
		float extra  = gap_px - (float)spacing;
		int   units  = (int)lroundf(extra / (float)spacing);

		if (units > 0) {
			gap_tokens[gap_count] = malloc(GAP_TOKEN_LEN);
			if (!gap_tokens[gap_count]) {
				goto out;
			}
			snprintf(gap_tokens[gap_count], GAP_TOKEN_LEN,
			         "gap:%d", units);
			tokens[token_count++] = gap_tokens[gap_count++];
		}
	}

	csv = barny_module_layout_serialize_csv(tokens, token_count);
	if (!csv) {
		goto out;
	}

	if (ensure_parent_dirs(config_path) < 0) {
		goto out;
	}

	rc = barny_config_write_module_layout(config_path, csv, "", "");

out:
	free(csv);
	for (int i = 0; i < gap_count; i++) {
		free(gap_tokens[i]);
	}
	return rc;
}

static void
set_status(char *status, size_t status_len, Uint64 *status_until, const char *msg)
{
	snprintf(status, status_len, "%s", msg ? msg : "");
	*status_until = SDL_GetTicks() + 3000;
}

static void
draw_labeled_zone(SDL_Renderer *renderer, const SDL_FRect *rect, const char *label,
                  Uint8 r, Uint8 g, Uint8 b)
{
	SDL_SetRenderDrawColor(renderer, r, g, b, 80);
	SDL_RenderFillRect(renderer, rect);
	SDL_SetRenderDrawColor(renderer, r, g, b, 180);
	SDL_RenderRect(renderer, rect);
	ui_set_draw_color(renderer, UI_COLOR_TEXT_MUTED);
	SDL_RenderDebugText(renderer, rect->x + 6.0f, rect->y + 6.0f, label);
}

static void
draw_block(SDL_Renderer *renderer, const ui_block_t *block)
{
	Uint8 r = 70, g = 110, b = 165;

	if (block->source == BLOCK_SRC_POOL) {
		r = 90;
		g = 95;
		b = 105;
	}

	SDL_SetRenderDrawColor(renderer, r, g, b, 220);
	SDL_RenderFillRect(renderer, &block->rect);
	SDL_SetRenderDrawColor(renderer, 12, 12, 14, 255);
	SDL_RenderRect(renderer, &block->rect);
	SDL_SetRenderDrawColor(renderer, 240, 245, 250, 255);
	SDL_RenderDebugText(renderer, block->rect.x + 8.0f,
	                    block->rect.y + (BLOCK_HEIGHT - 8.0f) * 0.5f,
	                    block->name);
}

static bool
module_details_compute_panel_rect(SDL_Renderer *renderer, SDL_FRect *panel)
{
	int w = 0, h = 0;

	if (!renderer || !panel) {
		return false;
	}

	if (!SDL_GetCurrentRenderOutputSize(renderer, &w, &h) || w <= 0 || h <= 0) {
		return false;
	}

	panel->w = (float)w - 140.0f;
	panel->h = (float)h - 110.0f;
	if (panel->w < 560.0f) {
		panel->w = (float)w - 24.0f;
	}
	if (panel->h < 260.0f) {
		panel->h = (float)h - 24.0f;
	}
	panel->x = ((float)w - panel->w) * 0.5f;
	panel->y = ((float)h - panel->h) * 0.5f;
	return true;
}

static void
detail_field_apply_option(detail_field_t *field, int option_index)
{
	const detail_option_t *option;

	if (!field || option_index < 0 || option_index >= field->option_count) {
		return;
	}

	option = &field->options[option_index];

	switch (field->kind) {
	case DETAIL_FIELD_ENUM_INT:
		if (field->int_ptr) {
			*field->int_ptr = option->int_value;
		}
		break;
	case DETAIL_FIELD_ENUM_CHAR:
		if (field->char_ptr) {
			*field->char_ptr = option->char_value;
		}
		break;
	case DETAIL_FIELD_ENUM_STR:
		if (field->str_ptr) {
			free(*field->str_ptr);
			*field->str_ptr = NULL;
			if (option->str_value) {
				*field->str_ptr = strdup(option->str_value);
			}
		}
		break;
	case DETAIL_FIELD_BOOL:
	default:
		break;
	}
}

static detail_click_result_t
module_details_handle_click(module_details_t *details, SDL_Renderer *renderer,
                            float x, float y, const char *config_path)
{
	if (!details || !details->open || !renderer) {
		return DETAIL_CLICK_NONE;
	}
	if (!module_details_compute_panel_rect(renderer, &details->panel_rect)) {
		return DETAIL_CLICK_NONE;
	}

	if (!point_in_rect(x, y, &details->panel_rect)) {
		details->open = false;
		module_details_close_dropdowns(details);
		return DETAIL_CLICK_CLOSED;
	}

	if (point_in_rect(x, y, &details->close_rect)) {
		details->open = false;
		module_details_close_dropdowns(details);
		return DETAIL_CLICK_CLOSED;
	}

	if (point_in_rect(x, y, &details->save_rect)) {
		if (details->field_count <= 0) {
			return DETAIL_CLICK_NONE;
		}
		if (save_module_config_fields(config_path, details) == 0) {
			return DETAIL_CLICK_SAVED;
		}
		return DETAIL_CLICK_SAVE_FAILED;
	}

	for (int i = 0; i < details->field_count; i++) {
		detail_field_t *field = &details->fields[i];

		if (field->kind == DETAIL_FIELD_BOOL) {
			if (point_in_rect(x, y, &field->control_rect)
			    && field->bool_ptr) {
				*field->bool_ptr = !*field->bool_ptr;
				return DETAIL_CLICK_CHANGED;
			}
			continue;
		}

		if (field->dropdown_open) {
			for (int o = 0; o < field->option_count; o++) {
				if (point_in_rect(x, y, &field->option_rects[o])) {
					detail_field_apply_option(field, o);
					field->dropdown_open = false;
					return DETAIL_CLICK_CHANGED;
				}
			}
		}

		if (point_in_rect(x, y, &field->control_rect)) {
			bool open = !field->dropdown_open;
			module_details_close_dropdowns(details);
			field->dropdown_open = open;
			return DETAIL_CLICK_NONE;
		}
	}

	module_details_close_dropdowns(details);
	return DETAIL_CLICK_NONE;
}

/*
 * Render a boolean checkbox row inside the details panel. The row covers the
 * "control_rect" already laid out by the caller; this helper does not advance
 * the y cursor (the caller does).
 */
static void
draw_checkbox(SDL_Renderer *renderer, const detail_field_t *field, float text_y)
{
	SDL_FRect box = { field->control_rect.x + 6.0f,
		          field->control_rect.y + 2.0f, 14.0f, 14.0f };
	bool      checked = field->bool_ptr && *field->bool_ptr;

	SDL_SetRenderDrawColor(renderer, 94, 102, 118, 255);
	SDL_RenderRect(renderer, &box);
	if (checked) {
		SDL_FRect fill = { box.x + 3.0f, box.y + 3.0f, box.w - 6.0f,
			           box.h - 6.0f };
		SDL_SetRenderDrawColor(renderer, 112, 180, 126, 255);
		SDL_RenderFillRect(renderer, &fill);
	}
	SDL_SetRenderDrawColor(renderer, 210, 220, 236, 255);
	SDL_RenderDebugText(renderer, field->control_rect.x + 28.0f, text_y,
	                    checked ? "true" : "false");
}

/*
 * Render the closed dropdown control plus, when expanded, the popup list of
 * options. Returns the y offset *after* the control row and any popup, so the
 * caller can continue laying out subsequent fields.
 */
static float
draw_dropdown_field(SDL_Renderer *renderer, detail_field_t *field, float y)
{
	SDL_SetRenderDrawColor(renderer, 46, 54, 66, 255);
	SDL_RenderFillRect(renderer, &field->control_rect);
	SDL_SetRenderDrawColor(renderer, 110, 122, 141, 255);
	SDL_RenderRect(renderer, &field->control_rect);
	SDL_SetRenderDrawColor(renderer, 220, 226, 236, 255);
	SDL_RenderDebugText(renderer, field->control_rect.x + 6.0f, y + 6.0f,
	                    detail_field_selected_label(field));
	SDL_RenderDebugText(renderer,
	                    field->control_rect.x + field->control_rect.w
	                            - 14.0f,
	                    y + 6.0f, "v");

	y += DETAIL_ROW_H;
	if (!field->dropdown_open) {
		return y;
	}

	for (int o = 0; o < field->option_count; o++) {
		field->option_rects[o].x = field->control_rect.x;
		field->option_rects[o].y = y;
		field->option_rects[o].w = field->control_rect.w;
		field->option_rects[o].h = 18.0f;

		SDL_SetRenderDrawColor(renderer, 38, 45, 55, 255);
		SDL_RenderFillRect(renderer, &field->option_rects[o]);
		SDL_SetRenderDrawColor(renderer, 98, 112, 130, 255);
		SDL_RenderRect(renderer, &field->option_rects[o]);
		SDL_SetRenderDrawColor(renderer, 219, 226, 238, 255);
		SDL_RenderDebugText(renderer, field->option_rects[o].x + 6.0f,
		                    y + 6.0f, field->options[o].label);

		y += 19.0f;
	}
	y += 2.0f;
	return y;
}

/*
 * Lay out and draw the "Save module settings" / "Close" buttons plus the
 * keyboard hint string at the bottom of the panel. Updates save_rect /
 * close_rect on `details` so click handling stays in sync.
 */
static void
draw_detail_buttons(SDL_Renderer *renderer, module_details_t *details)
{
	details->save_rect.x = details->panel_rect.x + 14.0f;
	details->save_rect.y
	        = details->panel_rect.y + details->panel_rect.h - 30.0f;
	details->save_rect.w = 180.0f;
	details->save_rect.h = 18.0f;
	details->close_rect.x
	        = details->panel_rect.x + details->panel_rect.w - 84.0f;
	details->close_rect.y = details->save_rect.y;
	details->close_rect.w = 70.0f;
	details->close_rect.h = 18.0f;

	SDL_SetRenderDrawColor(renderer, 52, 86, 68, 255);
	SDL_RenderFillRect(renderer, &details->save_rect);
	SDL_SetRenderDrawColor(renderer, 110, 172, 132, 255);
	SDL_RenderRect(renderer, &details->save_rect);
	SDL_SetRenderDrawColor(renderer, 236, 246, 240, 255);
	SDL_RenderDebugText(renderer, details->save_rect.x + 8.0f,
	                    details->save_rect.y + 6.0f, "Save module settings");

	SDL_SetRenderDrawColor(renderer, 73, 73, 86, 255);
	SDL_RenderFillRect(renderer, &details->close_rect);
	SDL_SetRenderDrawColor(renderer, 132, 132, 146, 255);
	SDL_RenderRect(renderer, &details->close_rect);
	SDL_SetRenderDrawColor(renderer, 228, 230, 236, 255);
	SDL_RenderDebugText(renderer, details->close_rect.x + 14.0f,
	                    details->close_rect.y + 6.0f, "Close");

	SDL_SetRenderDrawColor(renderer, 205, 215, 230, 255);
	SDL_RenderDebugText(renderer, details->panel_rect.x + 206.0f,
	                    details->panel_rect.y + details->panel_rect.h - 16.0f,
	                    "q/ESC close  |  w save");
}

static void
draw_module_details(SDL_Renderer *renderer, module_details_t *details)
{
	SDL_FRect bg;
	float     y;

	if (!details || !details->open) {
		return;
	}

	if (!module_details_compute_panel_rect(renderer, &details->panel_rect)) {
		return;
	}

	bg.x = 0.0f;
	bg.y = 0.0f;
	bg.w = details->panel_rect.x * 2.0f + details->panel_rect.w;
	bg.h = details->panel_rect.y * 2.0f + details->panel_rect.h;
	ui_set_draw_color(renderer, UI_COLOR_OVERLAY_DIM);
	SDL_RenderFillRect(renderer, &bg);

	ui_set_draw_color(renderer, UI_COLOR_PANEL_BG);
	SDL_RenderFillRect(renderer, &details->panel_rect);
	ui_set_draw_color(renderer, UI_COLOR_PANEL_BORDER);
	SDL_RenderRect(renderer, &details->panel_rect);

	ui_set_draw_color(renderer, UI_COLOR_TEXT_PRIMARY);
	SDL_RenderDebugTextFormat(renderer, details->panel_rect.x + 14.0f,
	                          details->panel_rect.y + 14.0f,
	                          "Module settings: %s", details->module);
	SDL_RenderDebugText(
	        renderer, details->panel_rect.x + 14.0f,
	        details->panel_rect.y + 28.0f,
	        "Checkboxes for true/false, dropdowns for multi-choice.");

	y = details->panel_rect.y + 48.0f;
	for (int i = 0; i < details->field_count; i++) {
		detail_field_t *field  = &details->fields[i];
		float           ctrl_x = details->panel_rect.x
		               + details->panel_rect.w
		               - DETAIL_CTRL_W
		               - 16.0f;

		memset(field->option_rects, 0, sizeof(field->option_rects));
		field->control_rect.x = ctrl_x;
		field->control_rect.y = y + 2.0f;
		field->control_rect.w = DETAIL_CTRL_W;
		field->control_rect.h = 18.0f;

		if (field->control_rect.y + field->control_rect.h
		    > details->panel_rect.y + details->panel_rect.h - 56.0f) {
			break;
		}

		ui_set_draw_color(renderer, UI_COLOR_TEXT_LABEL);
		SDL_RenderDebugText(renderer, details->panel_rect.x + 14.0f,
		                    y + 6.0f, field->label);

		if (field->kind == DETAIL_FIELD_BOOL) {
			draw_checkbox(renderer, field, y + 6.0f);
			y += DETAIL_ROW_H;
			continue;
		}

		y = draw_dropdown_field(renderer, field, y);
	}

	draw_detail_buttons(renderer, details);

	if (details->field_count == 0) {
		ui_set_draw_color(renderer, UI_COLOR_INFO_AMBER);
		SDL_RenderDebugText(
		        renderer, details->panel_rect.x + 14.0f,
		        details->panel_rect.y + 54.0f,
		        "No editable bool/enum fields for this module yet.");
	}
}

static void
draw_frame(SDL_Renderer *renderer, const ui_regions_t *ui, const block_map_t *map,
           const drag_state_t *drag, const drop_target_t *target,
           const char *config_path, const char *status, bool show_status,
           module_details_t *details)
{
	SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
	SDL_RenderClear(renderer);

	SDL_SetRenderDrawColor(renderer, 35, 38, 44, 255);
	SDL_RenderFillRect(renderer, &ui->bar);
	SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
	SDL_RenderRect(renderer, &ui->bar);

	draw_labeled_zone(renderer, &ui->bar_slot,
	                  "CONTIGUOUS BAR (FREE PLACEMENT)", 70, 110, 165);
	draw_labeled_zone(renderer, &ui->pool, "MODULE POOL", 90, 95, 105);

	for (int i = 0; i < map->count; i++) {
		draw_block(renderer, &map->blocks[i]);
	}

	if (drag->active && target->valid && target->to_bar) {
		float     marker_x  = ui->bar_slot.x + SLOT_PAD + target->x_rel;
		SDL_FRect insertion = {
			marker_x - 1.0f,
			ui->bar_slot.y + 8.0f,
			3.0f,
			ui->bar_slot.h - 16.0f,
		};
		SDL_SetRenderDrawColor(renderer, 255, 215, 120, 255);
		SDL_RenderFillRect(renderer, &insertion);
	}

	if (drag->active) {
		float     w    = module_block_width(drag->name);
		SDL_FRect rect = {
			drag->mouse_x - drag->offset_x,
			drag->mouse_y - drag->offset_y,
			w,
			BLOCK_HEIGHT,
		};
		ui_block_t ghost = {
			.name   = drag->name,
			.rect   = rect,
			.source = BLOCK_SRC_POOL,
			.index  = 0,
		};
		draw_block(renderer, &ghost);
	}

	ui_set_draw_color(renderer, UI_COLOR_TEXT_MUTED);
	SDL_RenderDebugText(
	        renderer, 32.0f, 20.0f,
	        "Barny Layout Editor  |  Drag modules or click for details");
	SDL_RenderDebugText(
	        renderer, 32.0f, 36.0f,
	        "Saved layout uses gap:N tokens proportional to your spacing");
	SDL_RenderDebugText(
	        renderer, 32.0f, 52.0f,
	        "Keys: w=write(save)  r=reset defaults  c=clear bar  q=quit");

	SDL_RenderDebugTextFormat(renderer, 32.0f, ui->pool.y + ui->pool.h + 10.0f,
	                          "Config: %s", config_path);
	if (show_status) {
		ui_set_draw_color(renderer, UI_COLOR_STATUS_WARN);
		SDL_RenderDebugText(renderer, 32.0f,
		                    ui->pool.y + ui->pool.h + 24.0f, status);
	}
	draw_module_details(renderer, details);

	SDL_RenderPresent(renderer);
}

static void
resolve_config_path(int argc, char **argv, char *out, size_t out_len)
{
	(void)argc;
	(void)argv;
	snprintf(out, out_len, "config/barny.conf");
}

int
main(int argc, char **argv)
{
	SDL_Window           *window   = NULL;
	SDL_Renderer         *renderer = NULL;
	barny_config_t        config;
	barny_module_layout_t loaded_layout;
	bar_layout_t          bar;
	char                  config_path[PATH_MAX];
	char                  status[256]  = "";
	Uint64                status_until = 0;
	bool                  running      = true;
	drag_state_t          drag         = { 0 };
	module_details_t      details;
	int                   spacing;

	resolve_config_path(argc, argv, config_path, sizeof(config_path));

	barny_config_defaults(&config);
	barny_config_load(&config, config_path);

	spacing = config.module_spacing > 0 ? config.module_spacing : 16;

	barny_module_layout_init(&loaded_layout);
	barny_module_layout_load_from_config(&config, &loaded_layout);

	bar_layout_init(&bar);
	bar_layout_from_module_layout(&bar, &loaded_layout, spacing);
	barny_module_layout_destroy(&loaded_layout);
	module_details_reset(&details);

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "barny-layout-editor: SDL init failed: %s\n",
		        SDL_GetError());
		bar_layout_clear(&bar);
		barny_config_cleanup(&config);
		return 1;
	}

	if (!SDL_CreateWindowAndRenderer("barny layout editor", EDITOR_WINDOW_W,
	                                 EDITOR_WINDOW_H, SDL_WINDOW_RESIZABLE,
	                                 &window, &renderer)) {
		fprintf(stderr,
		        "barny-layout-editor: window creation failed: %s\n",
		        SDL_GetError());
		bar_layout_clear(&bar);
		barny_config_cleanup(&config);
		SDL_Quit();
		return 1;
	}

	SDL_SetRenderVSync(renderer, 1);

	while (running) {
		int           w = 0, h = 0;
		ui_regions_t  ui;
		block_map_t   map;
		drop_target_t target = { 0 };
		const char   *pool[BARNY_MAX_MODULES];
		int           pool_count;
		SDL_Event     event;

		SDL_GetWindowSizeInPixels(window, &w, &h);
		compute_regions(w, h, &ui);
		bar_layout_constrain_to_width(&bar,
		                              ui.bar_slot.w - SLOT_PAD * 2.0f);

		pool_count = build_pool(&bar, pool, BARNY_MAX_MODULES);
		build_block_map(&bar, &ui, pool, pool_count, &map);
		if (drag.active) {
			target = compute_drop_target(drag.mouse_x, drag.mouse_y,
			                             &ui);
		}

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
			} else if (event.type == SDL_EVENT_KEY_DOWN
			           && !event.key.repeat) {
				if (details.open) {
					if (event.key.key == SDLK_Q
					    || event.key.key == SDLK_ESCAPE) {
						details.open = false;
						module_details_close_dropdowns(
						        &details);
					} else if (event.key.key == SDLK_W
					           || event.key.key == SDLK_S) {
						if (details.field_count > 0
						    && save_module_config_fields(
						               config_path,
						               &details)
						               == 0) {
							set_status(
							        status,
							        sizeof(status),
							        &status_until,
							        "Module settings saved to config/barny.conf");
						} else if (details.field_count
						           <= 0) {
							set_status(
							        status,
							        sizeof(status),
							        &status_until,
							        "This module has no editable bool/enum options yet.");
						} else {
							char msg[128];
							snprintf(
							        msg, sizeof(msg),
							        "Module settings save failed (%s).",
							        strerror(errno));
							set_status(status,
							           sizeof(status),
							           &status_until,
							           msg);
						}
					}
					continue;
				}

				if (event.key.key == SDLK_Q
				    || event.key.key == SDLK_ESCAPE) {
					running = false;
				} else if (event.key.key == SDLK_W
				           || event.key.key == SDLK_S) {
					if (save_layout(config_path, &bar, spacing)
					    == 0) {
						set_status(
						        status, sizeof(status),
						        &status_until,
						        "Config saved to current project dir: config/barny.conf");
					} else {
						char msg[128];
						snprintf(msg, sizeof(msg),
						         "Save failed (%s).",
						         strerror(errno));
						set_status(status, sizeof(status),
						           &status_until, msg);
					}
				} else if (event.key.key == SDLK_R) {
					bar_layout_load_defaults(&bar, spacing);
					set_status(
					        status, sizeof(status),
					        &status_until,
					        "Reset to legacy default layout.");
				} else if (event.key.key == SDLK_C) {
					bar_layout_clear(&bar);
					set_status(status, sizeof(status),
					           &status_until,
					           "Cleared bar modules.");
				}
			} else if (event.type == SDL_EVENT_MOUSE_MOTION) {
				if (drag.active) {
					drag.mouse_x = event.motion.x;
					drag.mouse_y = event.motion.y;
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
			           && event.button.button == SDL_BUTTON_LEFT) {
				float mx = event.button.x;
				float my = event.button.y;

				if (details.open) {
					detail_click_result_t click_result
					        = module_details_handle_click(
					                &details, renderer, mx, my,
					                config_path);
					if (click_result == DETAIL_CLICK_CHANGED) {
						set_status(
						        status, sizeof(status),
						        &status_until,
						        "Module setting updated.");
					} else if (click_result
					           == DETAIL_CLICK_SAVED) {
						set_status(
						        status, sizeof(status),
						        &status_until,
						        "Module settings saved to config/barny.conf");
					} else if (click_result
					           == DETAIL_CLICK_SAVE_FAILED) {
						char msg[128];
						snprintf(
						        msg, sizeof(msg),
						        "Module settings save failed (%s).",
						        strerror(errno));
						set_status(status, sizeof(status),
						           &status_until, msg);
					}
					continue;
				}

				for (int i = map.count - 1; i >= 0; i--) {
					ui_block_t *block = &map.blocks[i];
					if (!point_in_rect(mx, my, &block->rect)) {
						continue;
					}

					drag.active        = true;
					drag.source_in_bar = block->source
					                     == BLOCK_SRC_BAR;
					drag.mouse_x       = mx;
					drag.mouse_y       = my;
					drag.start_mouse_x = mx;
					drag.start_mouse_y = my;
					drag.offset_x      = mx - block->rect.x;
					drag.offset_y      = my - block->rect.y;
					snprintf(drag.click_name,
					         sizeof(drag.click_name), "%s",
					         block->name);

					if (drag.source_in_bar) {
						drag.name = bar_layout_take_index(
						        &bar, block->index,
						        &drag.source_x_rel);
					} else {
						drag.source_x_rel = 0.0f;
						drag.name = strdup(block->name);
					}

					if (!drag.name) {
						memset(&drag, 0, sizeof(drag));
					}
					break;
				}
			} else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
			           && event.button.button == SDL_BUTTON_LEFT
			           && drag.active) {
				bool  placed = false;
				float dx     = event.button.x - drag.start_mouse_x;
				float dy     = event.button.y - drag.start_mouse_y;
				bool  click_action
				        = fabsf(dx) <= CLICK_THRESHOLD_PX
				          && fabsf(dy) <= CLICK_THRESHOLD_PX
				          && drag.click_name[0] != '\0';
				drop_target_t drop_target = compute_drop_target(
				        event.button.x, event.button.y, &ui);

				if (click_action) {
					if (drag.source_in_bar && drag.name) {
						if (bar_layout_add_owned(
						            &bar, drag.name,
						            drag.source_x_rel)
						    == 0) {
							drag.name = NULL;
						}
					}
					if (drag.name) {
						free(drag.name);
						drag.name = NULL;
					}
					module_details_open(&details, &config,
					                    drag.click_name);
					set_status(status, sizeof(status),
					           &status_until,
					           "Opened module details.");
					memset(&drag, 0, sizeof(drag));
					continue;
				}

				if (drop_target.valid && drop_target.to_bar) {
					float block_left
					        = event.button.x - drag.offset_x;
					float rel = block_left
					            - (ui.bar_slot.x + SLOT_PAD);
					float max_rel
					        = ui.bar_slot.w
					          - module_block_width(drag.name)
					          - SLOT_PAD * 2.0f;
					rel = clampf(rel, 0.0f,
					             max_rel > 0.0f ? max_rel :
					                              0.0f);

					if (bar_layout_add_owned(&bar, drag.name,
					                         rel)
					    == 0) {
						drag.name = NULL;
						placed    = true;
					}
				} else if (drop_target.valid
				           && drop_target.to_pool) {
					free(drag.name);
					drag.name = NULL;
					placed    = true;
				}

				if (!placed && drag.source_in_bar) {
					if (bar_layout_add_owned(&bar, drag.name,
					                         drag.source_x_rel)
					    == 0) {
						drag.name = NULL;
					}
				}

				if (drag.name) {
					free(drag.name);
					drag.name = NULL;
				}
				memset(&drag, 0, sizeof(drag));
			}
		}

		draw_frame(renderer, &ui, &map, &drag, &target, config_path,
		           status, SDL_GetTicks() < status_until, &details);
	}

	free(drag.name);
	bar_layout_clear(&bar);
	barny_config_cleanup(&config);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
