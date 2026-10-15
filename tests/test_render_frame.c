#include "test_framework.h"
#include "barny.h"
#include <cairo/cairo.h>

#include "../src/render/render.c"

#define RF_BAR_W  900
#define RF_BAR_H  47
#define RF_T0_US  1000000000ULL
#define RF_T1_US  (RF_T0_US + 1000000ULL)

void
barny_output_request_frame(barny_output_t *output)
{
	(void)output;
}

typedef struct {
	char                  text[32];
	int                   width;
	bool                  top;
	PangoFontDescription *font;
} rf_mock_t;

static void
rf_mock_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	rf_mock_t     *m   = self->data;
	barny_config_t cfg = { 0 };

	(void)w;
	cairo_set_source_rgba(cr, 0.9, 0.4, 0.2, 0.8);
	if (m->top)
		cairo_rectangle(cr, x + 2, y - 3, 10, 9);
	else
		cairo_rectangle(cr, x + 2, y + 12, 10, 6);
	cairo_fill(cr);
	barny_module_render_text(cr, m->font, m->text, x + 14, y, h, &cfg, 1,
	                         1, 1, 0.9);
	self->width = m->width;
}

typedef struct {
	barny_state_t    state;
	barny_output_t   out;
	barny_module_t   mods[3];
	rf_mock_t        data[3];
	cairo_surface_t *wallpaper;
	barny_rect_t     dmg[FRAME_DMG_MAX];
	uint8_t         *snap;
	size_t           len;
} rf_bar_t;

static void
rf_setup(rf_bar_t *b, uint32_t s120)
{
	static const barny_position_t pos[3] = { BARNY_POS_LEFT,
		                                 BARNY_POS_CENTER,
		                                 BARNY_POS_RIGHT };
	double                        k      = (double)s120 / BARNY_SCALE_UNIT;
	cairo_t                      *wc;
	int                           i;

	memset(b, 0, sizeof(*b));
	barny_config_defaults(&b->state.config);
	b->state.config.dynamic_glass    = true;
	b->state.config.glass_prism      = 0.8;
	b->state.config.glass_prism_flow = 5.0;
	b->state.config.glass_prism_fps  = 15.0;
	b->state.config.border_radius    = 22;
	b->state.dyn_output              = &b->out;
	b->state.lens_scale              = 0.0;

	b->wallpaper = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, RF_BAR_W,
	                                          RF_BAR_H);
	wc           = cairo_create(b->wallpaper);
	cairo_set_source_rgb(wc, 0.12, 0.16, 0.24);
	cairo_paint(wc);
	cairo_destroy(wc);
	b->state.blurred_wallpaper = b->wallpaper;

	for (i = 0; i < 3; i++) {
		b->data[i].font  = pango_font_description_from_string("Sans 10");
		b->data[i].width = 90;
		snprintf(b->data[i].text, sizeof(b->data[i].text), "mod %d", i);
		b->mods[i].name     = "mock";
		b->mods[i].position = pos[i];
		b->mods[i].render   = rf_mock_render;
		b->mods[i].data     = &b->data[i];
		b->mods[i].width    = 90;
		b->state.modules[i] = &b->mods[i];
	}
	b->state.module_count = 3;

	b->out.state       = &b->state;
	b->out.scale       = s120 > BARNY_SCALE_UNIT ? 2 : 1;
	b->out.scale120    = s120;
	b->out.width       = RF_BAR_W;
	b->out.height      = RF_BAR_H;
	b->out.pad_left    = 8;
	b->out.pad_right   = 8;
	b->out.pad_top     = 4;
	b->out.pad_bottom  = 8;
	b->out.surf_width  = 8 + RF_BAR_W + 8;
	b->out.surf_height = 4 + RF_BAR_H + 8;
	b->out.configured  = true;
	b->out.cairo_surface
	        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                     barny_scale_len(b->out.surf_width,
	                                                     s120),
	                                     barny_scale_len(b->out.surf_height,
	                                                     s120));
	b->out.cr = cairo_create(b->out.cairo_surface);
	if (s120 != BARNY_SCALE_UNIT)
		cairo_scale(b->out.cr, k, k);
	for (i = 0; i < BARNY_MAX_MODULES; i++)
		b->out.mod_x[i] = -1;
}

static void
rf_teardown(rf_bar_t *b)
{
	int i;

	free(b->snap);
	cairo_destroy(b->out.cr);
	cairo_surface_destroy(b->out.cairo_surface);
	barny_output_free_glass_caches(&b->out, false);
	cairo_surface_destroy(b->wallpaper);
	for (i = 0; i < 3; i++)
		pango_font_description_free(b->data[i].font);
	barny_config_cleanup(&b->state.config);
	barny_text_cache_clear();
}

static int
rf_frame(rf_bar_t *b, uint64_t now_us)
{
	return frame_render(&b->out, now_us, b->dmg);
}

static void
rf_snapshot(rf_bar_t *b)
{
	cairo_surface_t *s = b->out.cairo_surface;

	cairo_surface_flush(s);
	b->len = (size_t)cairo_image_surface_get_stride(s)
	         * cairo_image_surface_get_height(s);
	free(b->snap);
	b->snap = malloc(b->len);
	memcpy(b->snap, cairo_image_surface_get_data(s), b->len);
}

static bool
rf_matches_full(rf_bar_t *b, uint64_t now_us)
{
	cairo_surface_t *s     = b->out.cairo_surface;
	bool             exact = b->out.scale120 % BARNY_SCALE_UNIT == 0;
	const uint8_t   *d;
	size_t           off   = 0;
	size_t           i;
	int              diff;
	int              worst = 0;

	rf_snapshot(b);
	b->out.lens_dmg_valid = false;
	if (rf_frame(b, now_us) != 0)
		return false;
	cairo_surface_flush(s);
	d = cairo_image_surface_get_data(s);

	for (i = 0; i < b->len; i++) {
		diff = abs((int)d[i] - (int)b->snap[i]);
		if (diff > worst)
			worst = diff;
		off += diff != 0;
	}

	if (exact)
		return off == 0;

	return worst <= 64 && off * 100 < b->len;
}

static void
rf_case(uint32_t s120)
{
	rf_bar_t b;
	unsigned before;
	char     name[96];

	rf_setup(&b, s120);
	rf_frame(&b, RF_T0_US);

	snprintf(name, sizeof(name), "x%.2f: a dirty module redraws only its strip",
	         s120 / 120.0);
	TEST(name)
	{
		snprintf(b.data[1].text, sizeof(b.data[1].text), "12:01");
		b.mods[1].dirty = true;
		ASSERT_EQ_INT(1, rf_frame(&b, RF_T0_US));
		ASSERT_TRUE(b.dmg[0].w < b.out.surf_width / 4);
		ASSERT_TRUE(rf_matches_full(&b, RF_T0_US));
	}

	snprintf(name, sizeof(name),
	         "x%.2f: a module that starts touching the rim stays drawn",
	         s120 / 120.0);
	TEST(name)
	{
		before          = b.out.rim_mods_touch;
		b.data[2].top   = true;
		b.mods[2].dirty = true;
		ASSERT_TRUE(rf_frame(&b, RF_T0_US) > 0);
		ASSERT_TRUE(b.out.rim_mods_touch != before);
		ASSERT_EQ_INT(4, rf_frame(&b, RF_T1_US));
		ASSERT_TRUE(rf_matches_full(&b, RF_T1_US));
	}

	snprintf(name, sizeof(name),
	         "x%.2f: dirty modules on a rim tick and under the droplet",
	         s120 / 120.0);
	TEST(name)
	{
		b.state.lens_scale = 1.0;
		b.state.lens_x     = 300.0;
		rf_frame(&b, RF_T1_US);
		b.state.lens_x = 340.0;
		snprintf(b.data[0].text, sizeof(b.data[0].text), "ws 3");
		snprintf(b.data[2].text, sizeof(b.data[2].text), "41%%");
		b.mods[0].dirty = true;
		b.mods[2].dirty = true;
		ASSERT_EQ_INT(1 + 4 + 2, rf_frame(&b, RF_T1_US + 1000000ULL));
		ASSERT_TRUE(rf_matches_full(&b, RF_T1_US + 1000000ULL));
		b.state.lens_scale = 0.0;
	}

	snprintf(name, sizeof(name), "x%.2f: a resized module redraws in full",
	         s120 / 120.0);
	TEST(name)
	{
		b.data[1].width = 120;
		b.mods[1].dirty = true;
		ASSERT_EQ_INT(0, rf_frame(&b, RF_T1_US));
	}

	rf_teardown(&b);
}

void
test_render_frame_partial(void)
{
	TEST_SUITE_BEGIN("Frame Partial Redraw");

	rf_case(BARNY_SCALE_UNIT);
	rf_case(150);
	rf_case(240);

	TEST_SUITE_END();
}
