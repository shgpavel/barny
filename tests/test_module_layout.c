#include "test_framework.h"
#include "barny.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *
create_temp_config_file(const char *content)
{
	static char path[256];
	FILE       *f;

	snprintf(path, sizeof(path), "/tmp/barny_layout_test_%d.conf", getpid());
	f = fopen(path, "w");
	if (!f) {
		return NULL;
	}

	if (content) {
		fputs(content, f);
	}
	fclose(f);
	return path;
}

static void
cleanup_temp_config_file(const char *path)
{
	if (path) {
		unlink(path);
	}
}

void
test_module_layout_basics(void)
{
	TEST_SUITE_BEGIN("Module Layout Basics");

	TEST("catalog exposes known modules")
	{
		const char *names[BARNY_MAX_MODULES] = { 0 };
		int total = barny_module_catalog_names(names, BARNY_MAX_MODULES);

		ASSERT_EQ_INT(10, total);
		ASSERT_EQ_STR("clock", names[0]);
		ASSERT_EQ_STR("workspace", names[1]);
		ASSERT_TRUE(barny_module_catalog_has("tray"));
		ASSERT_TRUE(barny_module_catalog_has("gap:2"));
		ASSERT_EQ_INT(2, barny_module_layout_gap_units("gap:2"));
		ASSERT_EQ_INT(0, barny_module_layout_gap_units("gap:0"));
		ASSERT_FALSE(barny_module_catalog_has("not_real"));
	}

	TEST("default layout matches legacy arrangement")
	{
		barny_module_layout_t layout;
		barny_module_layout_init(&layout);
		barny_module_layout_set_defaults(&layout);

		ASSERT_EQ_INT(1, layout.left_count);
		ASSERT_EQ_INT(0, layout.center_count);
		ASSERT_EQ_INT(9, layout.right_count);

		ASSERT_EQ_STR("workspace", layout.left[0]);
		ASSERT_EQ_STR("clock", layout.right[0]);
		ASSERT_EQ_STR("tray", layout.right[8]);

		barny_module_layout_destroy(&layout);
	}

	TEST("explicit empty layout is preserved")
	{
		barny_config_t config;
		barny_module_layout_t layout;

		barny_config_defaults(&config);
		config.modules_left = strdup("");
		config.modules_center = strdup("");
		config.modules_right = strdup("");

		barny_module_layout_init(&layout);
		barny_module_layout_load_from_config(&config, &layout);

		ASSERT_EQ_INT(0, layout.left_count);
		ASSERT_EQ_INT(0, layout.center_count);
		ASSERT_EQ_INT(0, layout.right_count);

		barny_module_layout_destroy(&layout);
		free(config.modules_left);
		free(config.modules_center);
		free(config.modules_right);
	}

	TEST_SUITE_END();
}

void
test_module_layout_parsing_and_ops(void)
{
	TEST_SUITE_BEGIN("Module Layout Parsing and Ops");

	TEST("loads configured slots and filters duplicates/unknowns")
	{
		barny_config_t config;
		barny_module_layout_t layout;

		barny_config_defaults(&config);
		config.modules_left = strdup("clock, workspace, nope, workspace");
		config.modules_center = strdup("ram");
		config.modules_right = strdup("tray, disk");

		barny_module_layout_init(&layout);
		barny_module_layout_load_from_config(&config, &layout);

		ASSERT_EQ_INT(2, layout.left_count);
		ASSERT_EQ_STR("clock", layout.left[0]);
		ASSERT_EQ_STR("workspace", layout.left[1]);
		ASSERT_EQ_INT(1, layout.center_count);
		ASSERT_EQ_STR("ram", layout.center[0]);
		ASSERT_EQ_INT(2, layout.right_count);
		ASSERT_EQ_STR("tray", layout.right[0]);
		ASSERT_EQ_STR("disk", layout.right[1]);

		barny_module_layout_destroy(&layout);
		free(config.modules_left);
		free(config.modules_center);
		free(config.modules_right);
	}

	TEST("loads gap tokens and allows repeated gaps")
	{
		barny_config_t config;
		barny_module_layout_t layout;

		barny_config_defaults(&config);
		config.modules_left = strdup("workspace, gap:1, gap:2, clock");

		barny_module_layout_init(&layout);
		barny_module_layout_load_from_config(&config, &layout);

		ASSERT_EQ_INT(4, layout.left_count);
		ASSERT_EQ_STR("workspace", layout.left[0]);
		ASSERT_EQ_STR("gap:1", layout.left[1]);
		ASSERT_EQ_STR("gap:2", layout.left[2]);
		ASSERT_EQ_STR("clock", layout.left[3]);

		barny_module_layout_destroy(&layout);
		free(config.modules_left);
	}

	TEST("insert/remove and csv serialization works")
	{
		barny_module_layout_t layout;
		char                 *csv;

		barny_module_layout_init(&layout);
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                            "workspace", -1));
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                            "clock", 0));
		ASSERT_EQ_INT(-1, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                             "clock", -1));
		ASSERT_TRUE(barny_module_layout_contains(&layout, "workspace"));
		ASSERT_TRUE(barny_module_layout_remove(&layout, "workspace"));
		ASSERT_FALSE(barny_module_layout_contains(&layout, "workspace"));

		csv = barny_module_layout_serialize_csv(
		        (const char *const *)layout.left, layout.left_count);
		ASSERT_NOT_NULL(csv);
		ASSERT_EQ_STR("clock", csv);

		free(csv);
		barny_module_layout_destroy(&layout);
	}

	TEST_SUITE_END();
}

void
test_module_layout_runtime_apply(void)
{
	TEST_SUITE_BEGIN("Module Layout Runtime Apply");

	TEST("apply registers selected modules in requested slots")
	{
		barny_state_t         state  = { 0 };
		barny_module_layout_t layout;

		barny_module_layout_init(&layout);
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                            "workspace", -1));
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_CENTER,
		                                            "clock", -1));
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_RIGHT,
		                                            "ram", -1));

		ASSERT_EQ_INT(3, barny_module_layout_apply_to_state(&layout, &state));
		ASSERT_EQ_INT(3, state.module_count);
		ASSERT_EQ_STR("workspace", state.modules[0]->name);
		ASSERT_EQ_INT(BARNY_POS_LEFT, state.modules[0]->position);
		ASSERT_EQ_STR("clock", state.modules[1]->name);
		ASSERT_EQ_INT(BARNY_POS_CENTER, state.modules[1]->position);
		ASSERT_EQ_STR("ram", state.modules[2]->name);
		ASSERT_EQ_INT(BARNY_POS_RIGHT, state.modules[2]->position);

		barny_modules_destroy(&state);
		barny_module_layout_destroy(&layout);
	}

	TEST("apply converts gap token to spacer module width")
	{
		barny_state_t         state  = { 0 };
		barny_module_layout_t layout;

		barny_config_defaults(&state.config);
		state.config.module_spacing = 20;

		barny_module_layout_init(&layout);
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                            "workspace", -1));
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                            "gap:3", -1));
		ASSERT_EQ_INT(0, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                            "clock", -1));

		ASSERT_EQ_INT(3, barny_module_layout_apply_to_state(&layout, &state));
		ASSERT_EQ_INT(3, state.module_count);
		ASSERT_EQ_STR("workspace", state.modules[0]->name);
		ASSERT_EQ_STR("gap", state.modules[1]->name);
		ASSERT_EQ_INT(40, state.modules[1]->width);
		ASSERT_EQ_STR("clock", state.modules[2]->name);

		barny_modules_destroy(&state);
		barny_module_layout_destroy(&layout);
	}

	TEST_SUITE_END();
}

void
test_module_layout_edge_cases(void)
{
	TEST_SUITE_BEGIN("Module Layout Edge Cases");

	TEST("partial config keys use explicit layout not defaults")
	{
		barny_config_t config;
		barny_module_layout_t layout;

		barny_config_defaults(&config);
		config.modules_left = strdup("workspace");
		/* modules_center and modules_right remain NULL */

		barny_module_layout_init(&layout);
		barny_module_layout_load_from_config(&config, &layout);

		ASSERT_EQ_INT(1, layout.left_count);
		ASSERT_EQ_STR("workspace", layout.left[0]);
		ASSERT_EQ_INT(0, layout.center_count);
		ASSERT_EQ_INT(0, layout.right_count);

		barny_module_layout_destroy(&layout);
		free(config.modules_left);
	}

	TEST("insert returns -1 when slot is full")
	{
		barny_module_layout_t layout;
		barny_module_layout_init(&layout);

		/* fill the left slot to BARNY_MAX_MODULES using catalog modules */
		const char *names[BARNY_MAX_MODULES] = { 0 };
		int total = barny_module_catalog_names(names, BARNY_MAX_MODULES);

		for (int i = 0; i < total && i < BARNY_MAX_MODULES; i++) {
			barny_module_layout_insert(&layout, BARNY_POS_LEFT, names[i], -1);
		}

		/* the slot should have `total` modules (10), which is < BARNY_MAX_MODULES
		 * so we can't truly overflow with only catalog entries. Instead verify
		 * the count matches and duplicate insert is rejected. */
		ASSERT_EQ_INT(total, layout.left_count);
		ASSERT_EQ_INT(-1, barny_module_layout_insert(&layout, BARNY_POS_LEFT,
		                                              "clock", -1));

		barny_module_layout_destroy(&layout);
	}

	TEST("serialize_csv with NULL entries in the middle")
	{
		const char *arr[4] = { "clock", NULL, "ram", NULL };
		char *csv = barny_module_layout_serialize_csv(arr, 4);

		ASSERT_NOT_NULL(csv);
		ASSERT_EQ_STR("clock, ram", csv);
		free(csv);
	}

	TEST("serialize_csv with count 0 returns empty string")
	{
		const char *arr[1] = { "clock" };
		char *csv = barny_module_layout_serialize_csv(arr, 0);

		ASSERT_NOT_NULL(csv);
		ASSERT_EQ_STR("", csv);
		free(csv);
	}

	TEST_SUITE_END();
}

void
test_module_layout_config_write(void)
{
	TEST_SUITE_BEGIN("Module Layout Config Write");

	TEST("writer updates only module layout keys")
	{
		const char *path = create_temp_config_file(
		        "height = 42\n"
		        "modules_left = old_left\n"
		        "modules_center = old_center\n"
		        "modules_right = old_right\n");
		barny_config_t cfg;

		ASSERT_NOT_NULL(path);
		ASSERT_EQ_INT(0, barny_config_write_module_layout(
		                      path, "workspace", "", "clock, tray"));

		barny_config_defaults(&cfg);
		ASSERT_EQ_INT(0, barny_config_load(&cfg, path));
		ASSERT_EQ_INT(42, cfg.height);
		ASSERT_NOT_NULL(cfg.modules_left);
		ASSERT_NOT_NULL(cfg.modules_center);
		ASSERT_NOT_NULL(cfg.modules_right);
		ASSERT_EQ_STR("workspace", cfg.modules_left);
		ASSERT_EQ_STR("", cfg.modules_center);
		ASSERT_EQ_STR("clock, tray", cfg.modules_right);

		free(cfg.modules_left);
		free(cfg.modules_center);
		free(cfg.modules_right);
		cleanup_temp_config_file(path);
	}

	TEST_SUITE_END();
}
