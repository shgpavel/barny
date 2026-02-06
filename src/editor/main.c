#include <SDL3/SDL.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "barny.h"

#define EDITOR_WINDOW_W 1200
#define EDITOR_WINDOW_H 760
#define BLOCK_HEIGHT 36.0f
#define BLOCK_GAP 10.0f
#define SLOT_PAD 10.0f
#define MAX_UI_BLOCKS 256

typedef struct {
	SDL_FRect bar;
	SDL_FRect bar_slot;
	SDL_FRect pool;
} ui_regions_t;

typedef enum {
	BLOCK_SRC_BAR,
	BLOCK_SRC_POOL
} block_source_t;

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
	char  *name;
	float  x_rel;
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
	float offset_x;
	float offset_y;
} drag_state_t;

typedef struct {
	bool  valid;
	bool  to_pool;
	bool  to_bar;
	float x_rel;
} drop_target_t;

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
	return rect && x >= rect->x && x <= rect->x + rect->w
	       && y >= rect->y && y <= rect->y + rect->h;
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
		bar->items[i].name = NULL;
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
		float total_gaps = 0.0f;

		gaps[0] = bar->items[0].x_rel;
		if (gaps[0] > 0.0f) {
			total_gaps += gaps[0];
		}

		for (int i = 1; i < bar->count; i++) {
			float prev_right = bar->items[i - 1].x_rel
			                   + module_block_width(bar->items[i - 1].name);
			float gap = bar->items[i].x_rel - prev_right;
			if (gap > 0.0f) {
				gaps[i] = gap;
				total_gaps += gaps[i];
			}
		}

		if (total_gaps > 0.0f) {
			float keep  = total_gaps - overflow;
			float scale = (keep > 0.0f) ? (keep / total_gaps) : 0.0f;

			bar->items[0].x_rel = gaps[0] * scale;

			for (int i = 1; i < bar->count; i++) {
				float prev_w = module_block_width(bar->items[i - 1].name);
				float prev_right = bar->items[i - 1].x_rel + prev_w;
				bar->items[i].x_rel = prev_right + gaps[i] * scale;
			}
		}
	}

	/* Final hard clamp so each module box remains inside bar bounds. */
	float cursor = 0.0f;
	for (int i = 0; i < bar->count; i++) {
		float w = module_block_width(bar->items[i].name);
		float max_x = content_width - w;
		float x = bar->items[i].x_rel;

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
		cursor = x + w;
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

	bar->items[bar->count].name = name;
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
	bar->items[bar->count - 1].name = NULL;
	bar->items[bar->count - 1].x_rel = 0.0f;
	bar->count--;
	return name;
}

static void
load_tokens_into_bar(bar_layout_t *bar, char *const *tokens, int count, int spacing)
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
bar_layout_from_module_layout(bar_layout_t *bar,
                              const barny_module_layout_t *layout,
                              int spacing)
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
	int         total      = barny_module_catalog_names(catalog,
	                                                    BARNY_MAX_MODULES);
	int         pool_count = 0;
	int         limit      = total < BARNY_MAX_MODULES ? total : BARNY_MAX_MODULES;

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
build_bar_blocks(const bar_layout_t *bar, const SDL_FRect *bar_rect, block_map_t *map)
{
	float start_x = bar_rect->x + SLOT_PAD;
	float y       = bar_rect->y + (bar_rect->h - BLOCK_HEIGHT) * 0.5f;

	for (int i = 0; i < bar->count; i++) {
		if (!bar->items[i].name) {
			continue;
		}

		float w = module_block_width(bar->items[i].name);
		SDL_FRect rect = { start_x + bar->items[i].x_rel, y, w, BLOCK_HEIGHT };
		append_block(map, bar->items[i].name, &rect, BLOCK_SRC_BAR, i);
	}
}

static void
build_pool_blocks(const char *const *pool, int pool_count, const SDL_FRect *pool_rect,
                  block_map_t *map)
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
			x = pool_rect->x + SLOT_PAD;
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
	float start_x;
	float max_rel;

	if (point_in_rect(x, y, &ui->pool)) {
		target.valid   = true;
		target.to_pool = true;
		return target;
	}

	if (!point_in_rect(x, y, &ui->bar) && !point_in_rect(x, y, &ui->bar_slot)) {
		return target;
	}

	start_x = ui->bar_slot.x + SLOT_PAD;
	max_rel = ui->bar_slot.w - SLOT_PAD * 2.0f;
	if (max_rel < 0.0f) {
		max_rel = 0.0f;
	}
	target.valid = true;
	target.to_bar = true;
	target.x_rel = x - start_x;
	target.x_rel = clampf(target.x_rel, 0.0f, max_rel);
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
		int lead_units = (int)lroundf(bar->items[0].x_rel / (float)spacing);
		if (lead_units > 0) {
			gap_tokens[gap_count] = malloc(32);
			if (!gap_tokens[gap_count]) {
				goto out;
			}
			snprintf(gap_tokens[gap_count], 32, "gap:%d", lead_units);
			tokens[token_count++] = gap_tokens[gap_count++];
		}
	}

	for (int i = 0; i < bar->count; i++) {
		float right;
		tokens[token_count++] = bar->items[i].name;

		if (i >= bar->count - 1) {
			continue;
		}

		right = bar->items[i].x_rel + module_block_width(bar->items[i].name);
		float gap_px = bar->items[i + 1].x_rel - right;
		float extra  = gap_px - (float)spacing;
		int   units  = (int)lroundf(extra / (float)spacing);

		if (units > 0) {
			gap_tokens[gap_count] = malloc(32);
			if (!gap_tokens[gap_count]) {
				goto out;
			}
			snprintf(gap_tokens[gap_count], 32, "gap:%d", units);
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
set_status(char *status, size_t status_len, Uint64 *status_until,
           const char *msg)
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
	SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
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

static void
draw_frame(SDL_Renderer *renderer, const ui_regions_t *ui,
           const block_map_t *map, const drag_state_t *drag,
           const drop_target_t *target, const char *config_path,
           const char *status, bool show_status)
{
	SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
	SDL_RenderClear(renderer);

	SDL_SetRenderDrawColor(renderer, 35, 38, 44, 255);
	SDL_RenderFillRect(renderer, &ui->bar);
	SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
	SDL_RenderRect(renderer, &ui->bar);

	draw_labeled_zone(renderer, &ui->bar_slot, "CONTIGUOUS BAR (FREE PLACEMENT)",
	                  70, 110, 165);
	draw_labeled_zone(renderer, &ui->pool, "MODULE POOL", 90, 95, 105);

	for (int i = 0; i < map->count; i++) {
		draw_block(renderer, &map->blocks[i]);
	}

	if (drag->active && target->valid && target->to_bar) {
		float marker_x = ui->bar_slot.x + SLOT_PAD + target->x_rel;
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
			.name = drag->name,
			.rect = rect,
			.source = BLOCK_SRC_POOL,
			.index = 0,
		};
		draw_block(renderer, &ghost);
	}

	SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
	SDL_RenderDebugText(renderer, 32.0f, 20.0f,
	                    "Barny Layout Editor  |  Drag modules anywhere on the bar");
	SDL_RenderDebugText(renderer, 32.0f, 36.0f,
	                    "Saved layout uses gap:N tokens proportional to your spacing");
	SDL_RenderDebugText(renderer, 32.0f, 52.0f,
	                    "Keys: S=save  R=reset defaults  C=clear bar  ESC=quit");

	SDL_RenderDebugTextFormat(renderer, 32.0f, ui->pool.y + ui->pool.h + 10.0f,
	                          "Config: %s", config_path);
	if (show_status) {
		SDL_SetRenderDrawColor(renderer, 255, 226, 160, 255);
		SDL_RenderDebugText(renderer, 32.0f, ui->pool.y + ui->pool.h + 24.0f,
		                    status);
	}

	SDL_RenderPresent(renderer);
}

static void
resolve_config_path(int argc, char **argv, char *out, size_t out_len)
{
	if (argc > 1) {
		snprintf(out, out_len, "%s", argv[1]);
		return;
	}

	const char *home = getenv("HOME");
	if (home && *home) {
		snprintf(out, out_len, "%s/.config/barny/barny.conf", home);
		return;
	}

	snprintf(out, out_len, "/etc/barny/barny.conf");
}

int
main(int argc, char **argv)
{
	SDL_Window            *window = NULL;
	SDL_Renderer          *renderer = NULL;
	barny_config_t         config;
	barny_module_layout_t  loaded_layout;
	bar_layout_t           bar;
	char                   config_path[PATH_MAX];
	char                   status[256] = "";
	Uint64                 status_until = 0;
	bool                   running = true;
	drag_state_t           drag = { 0 };
	int                    spacing;

	resolve_config_path(argc, argv, config_path, sizeof(config_path));

	barny_config_defaults(&config);
	barny_config_load(&config, config_path);

	spacing = config.module_spacing > 0 ? config.module_spacing : 16;

	barny_module_layout_init(&loaded_layout);
	barny_module_layout_load_from_config(&config, &loaded_layout);

	bar_layout_init(&bar);
	bar_layout_from_module_layout(&bar, &loaded_layout, spacing);
	barny_module_layout_destroy(&loaded_layout);

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
		fprintf(stderr, "barny-layout-editor: window creation failed: %s\n",
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
		bar_layout_constrain_to_width(&bar, ui.bar_slot.w - SLOT_PAD * 2.0f);

		pool_count = build_pool(&bar, pool, BARNY_MAX_MODULES);
		build_block_map(&bar, &ui, pool, pool_count, &map);
		if (drag.active) {
			target = compute_drop_target(drag.mouse_x, drag.mouse_y, &ui);
		}

		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
			} else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
				if (event.key.key == SDLK_ESCAPE) {
					running = false;
				} else if (event.key.key == SDLK_S) {
					if (save_layout(config_path, &bar, spacing) == 0) {
						set_status(status, sizeof(status), &status_until,
						           "Saved layout with proportional gaps.");
					} else {
						char msg[128];
						snprintf(msg, sizeof(msg), "Save failed (%s).",
						         strerror(errno));
						set_status(status, sizeof(status), &status_until,
						           msg);
					}
				} else if (event.key.key == SDLK_R) {
					bar_layout_load_defaults(&bar, spacing);
					set_status(status, sizeof(status), &status_until,
					           "Reset to legacy default layout.");
				} else if (event.key.key == SDLK_C) {
					bar_layout_clear(&bar);
					set_status(status, sizeof(status), &status_until,
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

				for (int i = map.count - 1; i >= 0; i--) {
					ui_block_t *block = &map.blocks[i];
					if (!point_in_rect(mx, my, &block->rect)) {
						continue;
					}

					drag.active       = true;
					drag.source_in_bar = block->source == BLOCK_SRC_BAR;
					drag.mouse_x       = mx;
					drag.mouse_y       = my;
					drag.offset_x      = mx - block->rect.x;
					drag.offset_y      = my - block->rect.y;

					if (drag.source_in_bar) {
						drag.name = bar_layout_take_index(&bar, block->index,
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
			           && event.button.button == SDL_BUTTON_LEFT && drag.active) {
				bool placed = false;
				drop_target_t drop_target = compute_drop_target(event.button.x,
				                                                event.button.y,
				                                                &ui);

				if (drop_target.valid && drop_target.to_bar) {
					float block_left = event.button.x - drag.offset_x;
					float rel = block_left - (ui.bar_slot.x + SLOT_PAD);
					float max_rel = ui.bar_slot.w - module_block_width(drag.name)
					                - SLOT_PAD * 2.0f;
					rel = clampf(rel, 0.0f, max_rel > 0.0f ? max_rel : 0.0f);

					if (bar_layout_add_owned(&bar, drag.name, rel) == 0) {
						drag.name = NULL;
						placed = true;
					}
				} else if (drop_target.valid && drop_target.to_pool) {
					free(drag.name);
					drag.name = NULL;
					placed = true;
				}

				if (!placed && drag.source_in_bar) {
					if (bar_layout_add_owned(&bar, drag.name, drag.source_x_rel)
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
		           status, SDL_GetTicks() < status_until);
	}

	free(drag.name);
	bar_layout_clear(&bar);
	barny_config_cleanup(&config);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
