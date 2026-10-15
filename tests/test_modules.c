#include "test_framework.h"
#include "barny.h"
#include <string.h>

static int mock_init_called    = 0;
static int mock_update_called  = 0;

static int
mock_init(barny_module_t *self, barny_state_t *state)
{
	(void)self;
	(void)state;
	mock_init_called++;
	return 0;
}

static void
mock_destroy(barny_module_t *self)
{
	(void)self;
}

static void
mock_update(barny_module_t *self)
{
	(void)self;
	mock_update_called++;
}

static void
mock_render(barny_module_t *self, cairo_t *cr, int x, int y, int w, int h)
{
	(void)self;
	(void)cr;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void
reset_mock_counters(void)
{
	mock_init_called    = 0;
	mock_update_called  = 0;
}

static barny_module_t *
create_mock_module(const char *name, barny_position_t pos)
{
	barny_module_t *mod = calloc(1, sizeof(barny_module_t));

	mod->name           = name;
	mod->position       = pos;
	mod->init           = mock_init;
	mod->destroy        = mock_destroy;
	mod->update         = mock_update;
	mod->render         = mock_render;
	mod->width          = 100;
	mod->height         = 30;
	mod->dirty          = false;

	return mod;
}

void
test_module_register(void)
{
	TEST_SUITE_BEGIN("Module Registration");

	TEST("register single module")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		mod   = create_mock_module("test", BARNY_POS_LEFT);

		barny_module_register(&state, mod);

		ASSERT_EQ_INT(1, state.module_count);
		ASSERT_EQ(mod, state.modules[0]);

		free(mod);
	}

	TEST("register multiple modules")
	{
		barny_state_t   state;
		barny_module_t *mod1;
		barny_module_t *mod2;
		barny_module_t *mod3;

		state = (barny_state_t){ 0 };
		mod1  = create_mock_module("test1", BARNY_POS_LEFT);
		mod2  = create_mock_module("test2", BARNY_POS_CENTER);
		mod3  = create_mock_module("test3", BARNY_POS_RIGHT);

		barny_module_register(&state, mod1);
		barny_module_register(&state, mod2);
		barny_module_register(&state, mod3);

		ASSERT_EQ_INT(3, state.module_count);
		ASSERT_EQ(mod1, state.modules[0]);
		ASSERT_EQ(mod2, state.modules[1]);
		ASSERT_EQ(mod3, state.modules[2]);

		free(mod1);
		free(mod2);
		free(mod3);
	}

	TEST("respects max module limit")
	{
		barny_state_t   state;
		barny_module_t *modules[BARNY_MAX_MODULES + 5];
		int             i;

		state = (barny_state_t){ 0 };
		for (i = 0; i < BARNY_MAX_MODULES + 5; i++) {
			modules[i] = create_mock_module("test", BARNY_POS_LEFT);
			barny_module_register(&state, modules[i]);
		}

		ASSERT_EQ_INT(BARNY_MAX_MODULES, state.module_count);

		for (i = 0; i < BARNY_MAX_MODULES + 5; i++) {
			free(modules[i]);
		}
	}

	TEST_SUITE_END();
}

void
test_module_lifecycle(void)
{
	TEST_SUITE_BEGIN("Module Lifecycle");

	TEST("init calls module init function")
	{
		barny_state_t   state;
		barny_module_t *mod;

		reset_mock_counters();
		state = (barny_state_t){ 0 };
		mod   = create_mock_module("test", BARNY_POS_LEFT);

		barny_module_register(&state, mod);
		barny_modules_init(&state);

		ASSERT_EQ_INT(1, mock_init_called);

		free(mod);
	}

	TEST("init calls all registered modules")
	{
		barny_state_t   state;
		barny_module_t *mod1;
		barny_module_t *mod2;
		barny_module_t *mod3;

		reset_mock_counters();
		state = (barny_state_t){ 0 };
		mod1  = create_mock_module("test1", BARNY_POS_LEFT);
		mod2  = create_mock_module("test2", BARNY_POS_CENTER);
		mod3  = create_mock_module("test3", BARNY_POS_RIGHT);

		barny_module_register(&state, mod1);
		barny_module_register(&state, mod2);
		barny_module_register(&state, mod3);
		barny_modules_init(&state);

		ASSERT_EQ_INT(3, mock_init_called);

		free(mod1);
		free(mod2);
		free(mod3);
	}

	TEST("update calls module update function")
	{
		barny_state_t   state;
		barny_module_t *mod;

		reset_mock_counters();
		state = (barny_state_t){ 0 };
		mod   = create_mock_module("test", BARNY_POS_LEFT);

		barny_module_register(&state, mod);
		barny_modules_update(&state);

		ASSERT_EQ_INT(1, mock_update_called);

		free(mod);
	}

	TEST("mark_dirty sets all modules dirty")
	{
		barny_state_t   state;
		barny_module_t *mod1;
		barny_module_t *mod2;

		state       = (barny_state_t){ 0 };
		mod1        = create_mock_module("test1", BARNY_POS_LEFT);
		mod2        = create_mock_module("test2", BARNY_POS_CENTER);

		mod1->dirty = false;
		mod2->dirty = false;

		barny_module_register(&state, mod1);
		barny_module_register(&state, mod2);
		barny_modules_mark_dirty(&state);

		ASSERT_TRUE(mod1->dirty);
		ASSERT_TRUE(mod2->dirty);

		free(mod1);
		free(mod2);
	}

	TEST_SUITE_END();
}

void
test_module_factories(void)
{
	TEST_SUITE_BEGIN("Module Factories");

	TEST("clock module created with correct name")
	{
		barny_module_t *mod = barny_module_clock_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("clock", mod->name);
		ASSERT_NOT_NULL(mod->init);
		ASSERT_NOT_NULL(mod->render);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("clock module has right position")
	{
		barny_module_t *mod = barny_module_clock_create();

		ASSERT_EQ_INT(BARNY_POS_RIGHT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("workspace module created with correct name")
	{
		barny_module_t *mod = barny_module_workspace_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("workspace", mod->name);
		ASSERT_NOT_NULL(mod->init);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("workspace module has left position")
	{
		barny_module_t *mod = barny_module_workspace_create();

		ASSERT_EQ_INT(BARNY_POS_LEFT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("disk module created with correct name")
	{
		barny_module_t *mod = barny_module_disk_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("disk", mod->name);
		ASSERT_NOT_NULL(mod->init);
		ASSERT_NOT_NULL(mod->update);
		ASSERT_NOT_NULL(mod->render);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("disk module has right position")
	{
		barny_module_t *mod = barny_module_disk_create();

		ASSERT_EQ_INT(BARNY_POS_RIGHT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("sysinfo module created with correct name")
	{
		barny_module_t *mod = barny_module_sysinfo_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("sysinfo", mod->name);
		ASSERT_NOT_NULL(mod->init);
		ASSERT_NOT_NULL(mod->update);
		ASSERT_NOT_NULL(mod->render);
		if (mod->destroy)
			mod->destroy(mod);
	}

	TEST("sysinfo module has right position")
	{
		barny_module_t *mod = barny_module_sysinfo_create();

		ASSERT_EQ_INT(BARNY_POS_RIGHT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
	}

	TEST("ram module created with correct name")
	{
		barny_module_t *mod = barny_module_ram_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("ram", mod->name);
		ASSERT_NOT_NULL(mod->init);
		ASSERT_NOT_NULL(mod->update);
		ASSERT_NOT_NULL(mod->render);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("ram module has right position")
	{
		barny_module_t *mod = barny_module_ram_create();

		ASSERT_EQ_INT(BARNY_POS_RIGHT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("network module created with correct name")
	{
		barny_module_t *mod = barny_module_network_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("network", mod->name);
		ASSERT_NOT_NULL(mod->init);
		ASSERT_NOT_NULL(mod->update);
		ASSERT_NOT_NULL(mod->render);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("network module has right position")
	{
		barny_module_t *mod = barny_module_network_create();

		ASSERT_EQ_INT(BARNY_POS_RIGHT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("fileread module created with correct name")
	{
		barny_module_t *mod = barny_module_fileread_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("fileread", mod->name);
		ASSERT_NOT_NULL(mod->init);
		ASSERT_NOT_NULL(mod->update);
		ASSERT_NOT_NULL(mod->render);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("fileread module has right position")
	{
		barny_module_t *mod = barny_module_fileread_create();

		ASSERT_EQ_INT(BARNY_POS_RIGHT, mod->position);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_module_positions(void)
{
	TEST_SUITE_BEGIN("Module Positions");

	TEST("position enum values are distinct")
	{
		ASSERT_TRUE(BARNY_POS_LEFT != BARNY_POS_CENTER);
		ASSERT_TRUE(BARNY_POS_CENTER != BARNY_POS_RIGHT);
		ASSERT_TRUE(BARNY_POS_LEFT != BARNY_POS_RIGHT);
	}

	TEST("modules maintain their position")
	{
		barny_module_t *left;
		barny_module_t *center;
		barny_module_t *right;

		left   = create_mock_module("left", BARNY_POS_LEFT);
		center = create_mock_module("center", BARNY_POS_CENTER);
		right  = create_mock_module("right", BARNY_POS_RIGHT);

		ASSERT_EQ_INT(BARNY_POS_LEFT, left->position);
		ASSERT_EQ_INT(BARNY_POS_CENTER, center->position);
		ASSERT_EQ_INT(BARNY_POS_RIGHT, right->position);

		free(left);
		free(center);
		free(right);
	}

	TEST_SUITE_END();
}

void
test_module_data(void)
{
	TEST_SUITE_BEGIN("Module Data Handling");

	TEST("module data pointer can be set")
	{
		barny_module_t *mod;
		int             test_data;

		mod       = create_mock_module("test", BARNY_POS_LEFT);
		test_data = 42;
		mod->data = &test_data;

		ASSERT_EQ_INT(42, *(int *)mod->data);

		free(mod);
	}

	TEST("module dimensions can be set")
	{
		barny_module_t *mod = create_mock_module("test", BARNY_POS_LEFT);

		mod->width          = 200;
		mod->height         = 50;

		ASSERT_EQ_INT(200, mod->width);
		ASSERT_EQ_INT(50, mod->height);

		free(mod);
	}

	TEST("module dirty flag toggles")
	{
		barny_module_t *mod = create_mock_module("test", BARNY_POS_LEFT);

		mod->dirty          = false;
		ASSERT_FALSE(mod->dirty);

		mod->dirty = true;
		ASSERT_TRUE(mod->dirty);

		free(mod);
	}

	TEST_SUITE_END();
}

static int
render_text_fresh(cairo_t *cr, PangoFontDescription *font, const char *text,
                  int x, int y, int h, const barny_config_t *cfg)
{
	PangoLayout *layout;
	int          tw;
	int          th;
	int          ty;

	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, font);
	pango_layout_set_text(layout, text, -1);
	pango_layout_get_pixel_size(layout, &tw, &th);
	ty = y + (h - th) / 2;
	cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
	barny_move_to_pixel(cr, x + 1, ty + 1);
	pango_cairo_show_layout(cr, layout);
	cairo_set_source_rgba(cr, cfg->text_color_r, cfg->text_color_g,
	                      cfg->text_color_b, 0.9);
	barny_move_to_pixel(cr, x, ty);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);

	return tw;
}

static void
surface_clear(cairo_t *cr)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);
}

static bool
surfaces_equal(cairo_surface_t *a, cairo_surface_t *b)
{
	cairo_surface_flush(a);
	cairo_surface_flush(b);

	return memcmp(cairo_image_surface_get_data(a),
	              cairo_image_surface_get_data(b),
	              (size_t)cairo_image_surface_get_stride(a)
	                      * cairo_image_surface_get_height(a))
	       == 0;
}

void
test_text_cache(void)
{
	static const char *const fonts[] = { "Sans 10", "Monospace Bold 13",
		                             "Serif 21" };
	static const char *const texts[] = { "12:34:56", "BTC $109234.56", "1",
		                             "",         "CPU 4.2 GHz",    "日本",
		                             "  wl0: 10.0.0.2" };
	static const double      scales[] = { 1.0, 1.25, 2.0 };
	PangoFontDescription    *fd[3];
	cairo_surface_t         *cs[3];
	cairo_surface_t         *rs[3];
	cairo_t                 *cc[3];
	cairo_t                 *rc[3];
	barny_config_t           cfg = { 0 };
	int                      pass;
	int                      s;
	int                      f;
	int                      t;
	int                      cw;
	int                      rw;
	int                      bad;

	TEST_SUITE_BEGIN("Text Layout Cache");

	cfg.text_color_set = true;
	cfg.text_color_r   = 0.9;
	cfg.text_color_g   = 0.8;
	cfg.text_color_b   = 0.7;
	for (f = 0; f < 3; f++)
		fd[f] = pango_font_description_from_string(fonts[f]);
	for (s = 0; s < 3; s++) {
		cs[s] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 80);
		rs[s] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 80);
		cc[s] = cairo_create(cs[s]);
		rc[s] = cairo_create(rs[s]);
		cairo_scale(cc[s], scales[s], scales[s]);
		cairo_scale(rc[s], scales[s], scales[s]);
	}

	TEST("cached text draws the pixels of a fresh layout")
	{
		bad = 0;
		for (pass = 0; pass < 3; pass++) {
			for (t = 0; t < 7; t++) {
				for (s = 0; s < 3; s++) {
					for (f = 0; f < 3; f++) {
						surface_clear(cc[s]);
						surface_clear(rc[s]);
						cw = barny_module_render_text(
						        cc[s], fd[f], texts[t], 3,
						        2, 30, &cfg, 1, 1, 1, 0.9);
						rw = render_text_fresh(
						        rc[s], fd[f], texts[t], 3, 2,
						        30, &cfg);
						if (cw != rw
						    || !surfaces_equal(cs[s],
						                       rs[s])) {
							printf("[pass %d '%s' "
							       "x%.2f %s] ",
							       pass, texts[t],
							       scales[s], fonts[f]);
							bad++;
						}
					}
				}
			}
		}
		ASSERT_EQ_INT(0, bad);
	}

	TEST("a cleared cache still draws the same pixels")
	{
		barny_text_cache_clear();
		surface_clear(cc[1]);
		surface_clear(rc[1]);
		barny_module_render_text(cc[1], fd[0], texts[0], 3, 2, 30, &cfg,
		                         1, 1, 1, 0.9);
		render_text_fresh(rc[1], fd[0], texts[0], 3, 2, 30, &cfg);
		ASSERT_TRUE(surfaces_equal(cs[1], rs[1]));
	}

	TEST("repeat draws reuse the shaped layout")
	{
		PangoLayout *l1;
		PangoLayout *l2;
		guint        serial;

		l1     = barny_text_layout(cc[1], fd[0], "12:34");
		pango_layout_get_pixel_size(l1, &cw, &rw);
		serial = pango_layout_get_serial(l1);
		l2     = barny_text_layout(cc[1], fd[0], "12:34");
		ASSERT_TRUE(l1 == l2);
		ASSERT_EQ_INT((int)serial, (int)pango_layout_get_serial(l2));
	}

	TEST("same cairo context, new CTM: fresh layout pixels")
	{
		barny_module_render_text(cc[0], fd[1], "CPU 4.2 GHz", 3, 2, 30,
		                         &cfg, 1, 1, 1, 0.9);
		cairo_scale(cc[0], 1.5, 1.5);
		cairo_scale(rc[0], 1.5, 1.5);
		surface_clear(cc[0]);
		surface_clear(rc[0]);
		barny_module_render_text(cc[0], fd[1], "CPU 4.2 GHz", 3, 2, 30,
		                         &cfg, 1, 1, 1, 0.9);
		render_text_fresh(rc[0], fd[1], "CPU 4.2 GHz", 3, 2, 30, &cfg);
		ASSERT_TRUE(surfaces_equal(cs[0], rs[0]));
	}

	TEST("same cairo context, new font options: fresh layout pixels")
	{
		cairo_font_options_t *fo = cairo_font_options_create();

		barny_module_render_text(cc[2], fd[2], "日本", 3, 2, 30, &cfg, 1,
		                         1, 1, 0.9);
		cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
		cairo_set_font_options(cc[2], fo);
		cairo_set_font_options(rc[2], fo);
		surface_clear(cc[2]);
		surface_clear(rc[2]);
		barny_module_render_text(cc[2], fd[2], "日本", 3, 2, 30, &cfg, 1,
		                         1, 1, 0.9);
		render_text_fresh(rc[2], fd[2], "日本", 3, 2, 30, &cfg);
		ASSERT_TRUE(surfaces_equal(cs[2], rs[2]));
		cairo_font_options_destroy(fo);
	}

	barny_text_cache_clear();
	for (s = 0; s < 3; s++) {
		cairo_destroy(cc[s]);
		cairo_destroy(rc[s]);
		cairo_surface_destroy(cs[s]);
		cairo_surface_destroy(rs[s]);
	}
	for (f = 0; f < 3; f++)
		pango_font_description_free(fd[f]);

	TEST_SUITE_END();
}
