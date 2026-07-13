#include "test_framework.h"
#include "barny.h"
#include <string.h>

static sni_item_t *test_items      = NULL;
static int         activate_calls  = 0;
static int         secondary_calls = 0;
static sni_item_t *last_item       = NULL;
static int         last_x          = 0;
static int         last_y          = 0;

sni_item_t *
barny_sni_host_get_items(barny_state_t *state)
{
	(void)state;
	return test_items;
}

void
barny_sni_item_activate(barny_state_t *state, sni_item_t *item, int x, int y)
{
	(void)state;
	activate_calls++;
	last_item = item;
	last_x    = x;
	last_y    = y;
}

void
barny_sni_item_secondary_activate(barny_state_t *state, sni_item_t *item, int x,
                                  int y)
{
	(void)state;
	secondary_calls++;
	last_item = item;
	last_x    = x;
	last_y    = y;
}

bool
barny_sni_item_is_menu(barny_state_t *state, sni_item_t *item)
{
	(void)state;
	(void)item;
	return false;
}

char *
barny_sni_item_menu_path(barny_state_t *state, sni_item_t *item)
{
	(void)state;
	(void)item;
	return NULL;
}

void
barny_menu_open(barny_state_t *state, sni_item_t *item, int anchor_x)
{
	(void)state;
	(void)item;
	(void)anchor_x;
}

#include "../src/modules/tray.c"

static void
reset_tray_mocks(void)
{
	activate_calls  = 0;
	secondary_calls = 0;
	last_item       = NULL;
	last_x          = 0;
	last_y          = 0;
	test_items      = NULL;
}

void
test_tray_update_width_and_dirty(void)
{
	TEST_SUITE_BEGIN("tray_update");

	barny_state_t  state;
	tray_data_t    data;
	barny_module_t mod;

	memset(&state, 0, sizeof(state));
	barny_config_defaults(&state.config);
	state.config.tray_icon_size    = 24;
	state.config.tray_icon_spacing = 4;

	memset(&data, 0, sizeof(data));

	memset(&mod, 0, sizeof(mod));
	mod.data = &data;

	TEST("sets width and dirty when items appear")
	{
		sni_item_t item1;
		sni_item_t item2;

		reset_tray_mocks();

		item1        = (sni_item_t){ 0 };
		item2        = (sni_item_t){ 0 };
		item1.status = "Active";
		item2.status = "Active";
		item1.next   = &item2;

		test_items   = &item1;

		tray_init(&mod, &state);
		mod.dirty = false;

		tray_update(&mod);

		ASSERT_TRUE(mod.dirty);
		ASSERT_EQ_INT(60, mod.width);
	}

	TEST("sets width to zero when no items")
	{
		reset_tray_mocks();
		test_items = NULL;

		tray_init(&mod, &state);
		mod.width = 123;
		mod.dirty = false;

		tray_update(&mod);

		ASSERT_EQ_INT(0, mod.width);
	}

	TEST_SUITE_END();
}

void
test_tray_click_handling(void)
{
	TEST_SUITE_BEGIN("tray_on_click");

	barny_state_t  state;
	barny_output_t output;
	tray_data_t    data;
	barny_module_t mod;
	sni_item_t     item1;
	sni_item_t     item2;
	int            base_x = 100;

	memset(&state, 0, sizeof(state));
	barny_config_defaults(&state.config);

	memset(&data, 0, sizeof(data));
	data.icon_size    = 24;
	data.icon_spacing = 4;
	data.state        = &state;

	memset(&mod, 0, sizeof(mod));
	mod.data     = &data;

	memset(&output, 0, sizeof(output));
	output.state         = &state;
	output.mod_x[0]      = base_x;
	output.mod_w[0]      = 64;
	state.modules[0]     = &mod;
	state.module_count   = 1;
	state.pointer_output = &output;

	item1        = (sni_item_t){ 0 };
	item2        = (sni_item_t){ 0 };
	item1.status = "Active";
	item2.status = "Active";
	item1.next   = &item2;
	test_items   = &item1;

	TEST("left click activates first icon")
	{
		reset_tray_mocks();
		test_items = &item1;
		tray_on_click(&mod, 272, base_x + 5, 10);
		ASSERT_EQ_INT(1, activate_calls);
		ASSERT_EQ_INT(0, secondary_calls);
		ASSERT_TRUE(last_item == &item1);
	}

	TEST("right click activates second icon")
	{
		int second_x;

		reset_tray_mocks();
		test_items = &item1;

		second_x   = base_x
		             + 4
		             + data.icon_size
		             + data.icon_spacing
		             + 1;
		tray_on_click(&mod, 273, second_x, 10);
		ASSERT_EQ_INT(0, activate_calls);
		ASSERT_EQ_INT(1, secondary_calls);
		ASSERT_TRUE(last_item == &item2);
	}

	TEST("click outside does nothing")
	{
		reset_tray_mocks();
		test_items = &item1;
		tray_on_click(&mod, 272, base_x + 500, 10);
		ASSERT_EQ_INT(0, activate_calls);
		ASSERT_EQ_INT(0, secondary_calls);
		ASSERT_NULL(last_item);
	}

	TEST("tray absent from the pointer's output ignores clicks")
	{
		reset_tray_mocks();
		test_items      = &item1;
		output.mod_x[0] = -1;
		tray_on_click(&mod, 272, base_x + 5, 10);
		ASSERT_EQ_INT(0, activate_calls);
		ASSERT_NULL(last_item);
		output.mod_x[0] = base_x;
	}

	TEST_SUITE_END();
}
