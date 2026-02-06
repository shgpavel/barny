#include "test_framework.h"
#include "barny.h"
#include <string.h>

static int mock_init_called    = 0;
static int mock_destroy_called = 0;
static int mock_update_called  = 0;
static int mock_render_called  = 0;

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
	mock_destroy_called++;
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
	mock_render_called++;
}

static void
reset_mock_counters(void)
{
	mock_init_called    = 0;
	mock_destroy_called = 0;
	mock_update_called  = 0;
	mock_render_called  = 0;
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
		barny_state_t   state = { 0 };
		barny_module_t *mod   = create_mock_module("test", BARNY_POS_LEFT);

		barny_module_register(&state, mod);

		ASSERT_EQ_INT(1, state.module_count);
		ASSERT_EQ(mod, state.modules[0]);

		free(mod);
	}

	TEST("register multiple modules")
	{
		barny_state_t   state = { 0 };
		barny_module_t *mod1 = create_mock_module("test1", BARNY_POS_LEFT);
		barny_module_t *mod2
		        = create_mock_module("test2", BARNY_POS_CENTER);
		barny_module_t *mod3
		        = create_mock_module("test3", BARNY_POS_RIGHT);

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
		barny_state_t   state = { 0 };
		barny_module_t *modules[BARNY_MAX_MODULES + 5];

		for (int i = 0; i < BARNY_MAX_MODULES + 5; i++) {
			modules[i] = create_mock_module("test", BARNY_POS_LEFT);
			barny_module_register(&state, modules[i]);
		}

		ASSERT_EQ_INT(BARNY_MAX_MODULES, state.module_count);

		for (int i = 0; i < BARNY_MAX_MODULES + 5; i++) {
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
		reset_mock_counters();
		barny_state_t   state = { 0 };
		barny_module_t *mod   = create_mock_module("test", BARNY_POS_LEFT);

		barny_module_register(&state, mod);
		barny_modules_init(&state);

		ASSERT_EQ_INT(1, mock_init_called);

		free(mod);
	}

	TEST("init calls all registered modules")
	{
		reset_mock_counters();
		barny_state_t   state = { 0 };
		barny_module_t *mod1 = create_mock_module("test1", BARNY_POS_LEFT);
		barny_module_t *mod2
		        = create_mock_module("test2", BARNY_POS_CENTER);
		barny_module_t *mod3
		        = create_mock_module("test3", BARNY_POS_RIGHT);

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
		reset_mock_counters();
		barny_state_t   state = { 0 };
		barny_module_t *mod   = create_mock_module("test", BARNY_POS_LEFT);

		barny_module_register(&state, mod);
		barny_modules_update(&state);

		ASSERT_EQ_INT(1, mock_update_called);

		free(mod);
	}

	TEST("mark_dirty sets all modules dirty")
	{
		barny_state_t   state = { 0 };
		barny_module_t *mod1 = create_mock_module("test1", BARNY_POS_LEFT);
		barny_module_t *mod2
		        = create_mock_module("test2", BARNY_POS_CENTER);

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

	/* Disk module */
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

	/* Sysinfo module (includes freq, power, temp) */
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

	/* RAM module */
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

	/* Network module */
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

	/* Fileread module */
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
		barny_module_t *left = create_mock_module("left", BARNY_POS_LEFT);
		barny_module_t *center
		        = create_mock_module("center", BARNY_POS_CENTER);
		barny_module_t *right
		        = create_mock_module("right", BARNY_POS_RIGHT);

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
		barny_module_t *mod = create_mock_module("test", BARNY_POS_LEFT);
		int             test_data = 42;
		mod->data                 = &test_data;

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
