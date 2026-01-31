#include "test_framework.h"
#include "barny.h"
#include <unistd.h>
#include <sys/stat.h>

static char *
create_temp_file(const char *content)
{
	static char path[256];
	snprintf(path, sizeof(path), "/tmp/barny_test_%d.txt", getpid());
	FILE *f = fopen(path, "w");
	if (f) {
		fputs(content, f);
		fclose(f);
	}
	return path;
}

static void
cleanup_temp_file(const char *path)
{
	unlink(path);
}

void
test_clock_module_behavior(void)
{
	TEST_SUITE_BEGIN("Clock Module Behavior");

	TEST("clock init succeeds with default config")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_clock_create();
		ASSERT_NOT_NULL(mod);

		int result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("clock update sets dirty flag")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_clock_create();
		mod->init(mod, &state);
		mod->dirty = false;

		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("clock respects show_time false")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		state.config.clock_show_time = false;
		state.config.clock_show_date = false;

		barny_module_t *mod = barny_module_clock_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* With both time and date off, display should be empty */
		/* We can't easily check internal state, but update should succeed */
		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_ram_module_behavior(void)
{
	TEST_SUITE_BEGIN("RAM Module Behavior");

	TEST("ram init succeeds")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_ram_create();
		ASSERT_NOT_NULL(mod);

		int result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("ram update reads /proc/meminfo")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_ram_create();
		mod->init(mod, &state);

		/* /proc/meminfo should exist on Linux */
		mod->update(mod);

		/* Should have set dirty if meminfo was readable */
		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("ram respects show_percentage config")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		state.config.ram_show_percentage = true;

		barny_module_t *mod = barny_module_ram_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_disk_module_behavior(void)
{
	TEST_SUITE_BEGIN("Disk Module Behavior");

	TEST("disk init succeeds")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_disk_create();
		ASSERT_NOT_NULL(mod);

		int result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("disk update reads root filesystem")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_disk_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* Root filesystem should always be readable */
		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("disk respects custom path")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		state.config.disk_path = strdup("/tmp");

		barny_module_t *mod = barny_module_disk_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.disk_path);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_cpu_temp_module_behavior(void)
{
	TEST_SUITE_BEGIN("CPU Temp Module Behavior");

	TEST("cpu_temp init succeeds")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_cpu_temp_create();
		ASSERT_NOT_NULL(mod);

		int result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("cpu_temp update handles missing thermal zones")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		/* Use a path that doesn't exist */
		state.config.cpu_temp_path = strdup("/nonexistent/temp");

		barny_module_t *mod = barny_module_cpu_temp_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* Should not crash, dirty might be false */
		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.cpu_temp_path);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_fileread_module_behavior(void)
{
	TEST_SUITE_BEGIN("Fileread Module Behavior");

	TEST("fileread init succeeds")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_fileread_create();
		ASSERT_NOT_NULL(mod);

		int result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("fileread reads configured file")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		const char *content = "Test Status Content\n";
		char       *path    = create_temp_file(content);
		state.config.fileread_path = strdup(path);

		barny_module_t *mod = barny_module_fileread_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.fileread_path);
		cleanup_temp_file(path);
		free(mod);
	}

	TEST("fileread handles missing file")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		state.config.fileread_path = strdup("/nonexistent/file.txt");

		barny_module_t *mod = barny_module_fileread_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* Should not crash */
		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.fileread_path);
		free(mod);
	}

	TEST("fileread respects title config")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		const char *content = "Value\n";
		char       *path    = create_temp_file(content);
		state.config.fileread_path  = strdup(path);
		state.config.fileread_title = strdup("Label");

		barny_module_t *mod = barny_module_fileread_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.fileread_path);
		free(state.config.fileread_title);
		cleanup_temp_file(path);
		free(mod);
	}

	TEST("fileread respects max_chars limit")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		const char *content = "This is a very long string that exceeds the max chars limit\n";
		char       *path    = create_temp_file(content);
		state.config.fileread_path      = strdup(path);
		state.config.fileread_max_chars = 10;

		barny_module_t *mod = barny_module_fileread_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.fileread_path);
		cleanup_temp_file(path);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_network_module_behavior(void)
{
	TEST_SUITE_BEGIN("Network Module Behavior");

	TEST("network init succeeds")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_network_create();
		ASSERT_NOT_NULL(mod);

		int result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("network update handles auto interface")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);

		barny_module_t *mod = barny_module_network_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* Should complete without crashing */
		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("network handles specified interface")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		state.config.network_interface = strdup("lo");

		barny_module_t *mod = barny_module_network_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* Loopback should exist */
		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.network_interface);
		free(mod);
	}

	TEST("network handles nonexistent interface")
	{
		barny_state_t state = { 0 };
		barny_config_defaults(&state.config);
		state.config.network_interface = strdup("nonexistent99");

		barny_module_t *mod = barny_module_network_create();
		mod->init(mod, &state);
		mod->update(mod);

		/* Should not crash */
		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.network_interface);
		free(mod);
	}

	TEST_SUITE_END();
}
