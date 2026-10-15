#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "util.h"

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
	int             i;
	barny_module_t *mod;

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
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
	uint64_t        t;
	int             i;
	barny_module_t *mod;
	barny_output_t *out;

	t = barny_now_ms();

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (!mod || !mod->update)
			continue;
		if (mod->update_interval_ms > 0
		    && mod->last_update_ms != 0
		    && (t - mod->last_update_ms)
		               < (uint64_t)mod->update_interval_ms) {
			continue;
		}
		mod->update(mod);
		mod->last_update_ms = t;
	}

	if (barny_modules_any_dirty(state)) {
		for (out = state->outputs; out; out = out->next) {
			if (out->configured) {
				barny_render_frame(out);
			}
		}
	}
}

void
barny_modules_destroy(barny_state_t *state)
{
	int             i;
	barny_module_t *mod;

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod) {
			if (mod->destroy) {
				mod->destroy(mod);
			}
			free(mod);
			state->modules[i] = NULL;
		}
	}
	state->module_count = 0;
	barny_text_cache_clear();
}

void
barny_modules_mark_dirty(barny_state_t *state)
{
	int i;

	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i]) {
			state->modules[i]->dirty = true;
		}
	}
}

barny_module_t *
barny_module_find(barny_state_t *state, const char *name)
{
	int             i;
	barny_module_t *mod;

	if (!name) {
		return NULL;
	}

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod && mod->name && strcmp(mod->name, name) == 0) {
			return mod;
		}
	}

	return NULL;
}

bool
barny_modules_any_dirty(const barny_state_t *state)
{
	int i;

	for (i = 0; i < state->module_count; i++) {
		if (state->modules[i] && state->modules[i]->dirty) {
			return true;
		}
	}

	return false;
}

#define TEXT_CACHE_SLOTS 32

typedef struct {
	PangoLayout                *layout;
	const cairo_t              *cr;
	const cairo_surface_t      *target;
	cairo_matrix_t              ctm;
	cairo_font_options_t       *surface_opts;
	cairo_font_options_t       *cr_opts;
	const PangoFontDescription *font;
	uint64_t                    used;
} text_slot_t;

typedef struct {
	cairo_surface_t      *target;
	cairo_matrix_t        ctm;
	cairo_font_options_t *surface_opts;
	cairo_font_options_t *cr_opts;
} text_ctx_t;

static text_slot_t           text_cache[TEXT_CACHE_SLOTS];
static uint64_t              text_clock;
static cairo_font_options_t *text_surface_opts;
static cairo_font_options_t *text_cr_opts;

static void
text_ctx_read(cairo_t *cr, text_ctx_t *ctx)
{
	if (!text_surface_opts) {
		text_surface_opts = cairo_font_options_create();
		text_cr_opts      = cairo_font_options_create();
	}

	ctx->target = cairo_get_target(cr);
	cairo_get_matrix(cr, &ctx->ctm);
	cairo_surface_get_font_options(ctx->target, text_surface_opts);
	cairo_get_font_options(cr, text_cr_opts);
	ctx->surface_opts = text_surface_opts;
	ctx->cr_opts      = text_cr_opts;
}

static bool
matrix_equal(const cairo_matrix_t *a, const cairo_matrix_t *b)
{
	return a->xx == b->xx && a->yx == b->yx && a->xy == b->xy
	       && a->yy == b->yy && a->x0 == b->x0 && a->y0 == b->y0;
}

static bool
text_slot_synced(const text_slot_t *s, const cairo_t *cr, const text_ctx_t *ctx)
{
	return s->cr == cr && s->target == ctx->target
	       && matrix_equal(&s->ctm, &ctx->ctm)
	       && s->surface_opts && s->cr_opts
	       && cairo_font_options_equal(s->surface_opts, ctx->surface_opts)
	       && cairo_font_options_equal(s->cr_opts, ctx->cr_opts);
}

static void
text_slot_sync(text_slot_t *s, cairo_t *cr, const text_ctx_t *ctx)
{
	pango_cairo_update_layout(cr, s->layout);

	if (s->surface_opts)
		cairo_font_options_destroy(s->surface_opts);
	if (s->cr_opts)
		cairo_font_options_destroy(s->cr_opts);
	s->cr           = cr;
	s->target       = ctx->target;
	s->ctm          = ctx->ctm;
	s->surface_opts = cairo_font_options_copy(ctx->surface_opts);
	s->cr_opts      = cairo_font_options_copy(ctx->cr_opts);
}

PangoLayout *
barny_text_layout(cairo_t *cr, PangoFontDescription *font, const char *text)
{
	text_slot_t *slot = NULL;
	text_slot_t *lru  = &text_cache[0];
	text_slot_t *s;
	text_ctx_t   ctx;
	int          i;

	for (i = 0; i < TEXT_CACHE_SLOTS; i++) {
		s = &text_cache[i];
		if (s->layout && s->cr == cr && s->font == font
		    && strcmp(pango_layout_get_text(s->layout), text) == 0) {
			slot = s;
			break;
		}
		if (s->used < lru->used)
			lru = s;
	}

	if (!slot) {
		slot = lru;
		if (!slot->layout)
			slot->layout = pango_cairo_create_layout(cr);
		slot->font = font;
	}

	text_ctx_read(cr, &ctx);
	if (!text_slot_synced(slot, cr, &ctx))
		text_slot_sync(slot, cr, &ctx);
	pango_layout_set_font_description(slot->layout, font);
	if (strcmp(pango_layout_get_text(slot->layout), text) != 0)
		pango_layout_set_text(slot->layout, text, -1);
	slot->used = ++text_clock;

	return slot->layout;
}

void
barny_text_cache_clear(void)
{
	text_slot_t *s;
	int          i;

	for (i = 0; i < TEXT_CACHE_SLOTS; i++) {
		s = &text_cache[i];
		if (s->layout)
			g_object_unref(s->layout);
		if (s->surface_opts)
			cairo_font_options_destroy(s->surface_opts);
		if (s->cr_opts)
			cairo_font_options_destroy(s->cr_opts);
		*s = (text_slot_t){ 0 };
	}
	text_clock = 0;
	if (text_surface_opts) {
		cairo_font_options_destroy(text_surface_opts);
		cairo_font_options_destroy(text_cr_opts);
		text_surface_opts = NULL;
		text_cr_opts      = NULL;
	}
}

int
barny_module_render_text(cairo_t *cr, PangoFontDescription *font,
                         const char *text, int x, int y, int h,
                         const barny_config_t *cfg, double fb_r, double fb_g,
                         double fb_b, double alpha)
{
	PangoLayout *layout;
	int          tw;
	int          th;
	int          ty;

	layout = barny_text_layout(cr, font, text);

	pango_layout_get_pixel_size(layout, &tw, &th);

	ty = y + (h - th) / 2;

	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	barny_move_to_pixel(cr, x + 1, ty + 1);
	pango_cairo_show_layout(cr, layout);

	if (cfg->text_color_set) {
		cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
		                      cfg->text_color_b, alpha);
	} else {
		cairo_set_source_rgba(cr, fb_r, fb_g, fb_b, alpha);
	}
	barny_move_to_pixel(cr, x, ty);
	pango_cairo_show_layout(cr, layout);

	return tw;
}
