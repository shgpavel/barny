#include "test_framework.h"
#include "barny.h"
#include <unistd.h>
#include <sys/stat.h>

static char *
create_temp_file(const char *content)
{
	static char path[256];
	FILE       *f;

	snprintf(path, sizeof(path), "/tmp/barny_test_%d.txt", getpid());
	f = fopen(path, "w");
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
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_clock_create();
		ASSERT_NOT_NULL(mod);

		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("clock update sets dirty flag")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_clock_create();
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
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);
		state.config.clock_show_time = false;
		state.config.clock_show_date = false;

		mod                          = barny_module_clock_create();
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
test_ram_module_behavior(void)
{
	TEST_SUITE_BEGIN("RAM Module Behavior");

	TEST("ram init succeeds")
	{
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_ram_create();
		ASSERT_NOT_NULL(mod);

		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("ram update reads /proc/meminfo")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_ram_create();
		mod->init(mod, &state);

		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("ram respects percentage mode config")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);
		state.config.ram_mode = strdup("percentage");

		mod                   = barny_module_ram_create();
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
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_disk_create();
		ASSERT_NOT_NULL(mod);

		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("disk update reads root filesystem")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_disk_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_TRUE(mod->dirty);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("disk respects custom path")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);
		state.config.disk_path = strdup("/tmp");

		mod                    = barny_module_disk_create();
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
test_sysinfo_module_behavior(void)
{
	TEST_SUITE_BEGIN("Sysinfo Module Behavior");

	TEST("sysinfo init succeeds")
	{
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_sysinfo_create();
		ASSERT_NOT_NULL(mod);

		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
	}

	TEST("sysinfo update handles missing thermal zones")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		state.config.sysinfo_temp_path = strdup("/nonexistent/temp");

		mod                            = barny_module_sysinfo_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(state.config.sysinfo_temp_path);
	}

	TEST_SUITE_END();
}

void
test_fileread_module_behavior(void)
{
	TEST_SUITE_BEGIN("Fileread Module Behavior");

	TEST("fileread init succeeds")
	{
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_fileread_create();
		ASSERT_NOT_NULL(mod);

		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("fileread reads configured file")
	{
		barny_state_t   state;
		const char     *content;
		char           *path;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		content                    = "Test Status Content\n";
		path                       = create_temp_file(content);
		state.config.fileread_path = strdup(path);

		mod                        = barny_module_fileread_create();
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
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);
		state.config.fileread_path = strdup("/nonexistent/file.txt");

		mod                        = barny_module_fileread_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.fileread_path);
		free(mod);
	}

	TEST("fileread respects title config")
	{
		barny_state_t   state;
		const char     *content;
		char           *path;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		content                     = "Value\n";
		path                        = create_temp_file(content);
		state.config.fileread_path  = strdup(path);
		state.config.fileread_title = strdup("Label");

		mod                         = barny_module_fileread_create();
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
		barny_state_t   state;
		const char     *content;
		char           *path;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		content                         = "This is a very long string that exceeds the max chars limit\n";
		path                            = create_temp_file(content);
		state.config.fileread_path      = strdup(path);
		state.config.fileread_max_chars = 10;

		mod                             = barny_module_fileread_create();
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
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_network_create();
		ASSERT_NOT_NULL(mod);

		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("network update handles auto interface")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_network_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(mod);
	}

	TEST("network handles specified interface")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);
		state.config.network_interface = strdup("lo");

		mod                            = barny_module_network_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.network_interface);
		free(mod);
	}

	TEST("network handles nonexistent interface")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);
		state.config.network_interface = strdup("nonexistent99");

		mod                            = barny_module_network_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod->data);
		free(state.config.network_interface);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_weather_module_behavior(void)
{
	TEST_SUITE_BEGIN("Weather Module Behavior");

	TEST("weather create succeeds")
	{
		barny_module_t *mod = barny_module_weather_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("weather", mod->name);
		free(mod->data);
		free(mod);
	}

	TEST("weather init succeeds")
	{
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod    = barny_module_weather_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("weather update handles missing data file")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_weather_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("weather destroy is safe")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_weather_create();
		mod->init(mod, &state);

		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_crypto_module_behavior(void)
{
	TEST_SUITE_BEGIN("Crypto Module Behavior");

	TEST("crypto create succeeds")
	{
		barny_module_t *mod = barny_module_crypto_create();

		ASSERT_NOT_NULL(mod);
		ASSERT_EQ_STR("crypto", mod->name);
		free(mod->data);
		free(mod);
	}

	TEST("crypto init succeeds")
	{
		barny_state_t   state;
		barny_module_t *mod;
		int             result;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod    = barny_module_crypto_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("crypto update handles missing price data")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_crypto_create();
		mod->init(mod, &state);
		mod->update(mod);

		ASSERT_NOT_NULL(mod);

		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("crypto destroy is safe")
	{
		barny_state_t   state;
		barny_module_t *mod;

		state = (barny_state_t){ 0 };
		barny_config_defaults(&state.config);

		mod = barny_module_crypto_create();
		mod->init(mod, &state);

		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_module_destroy_safety(void)
{
	TEST_SUITE_BEGIN("Module Destroy Safety");

	barny_state_t state;

	state = (barny_state_t){ 0 };
	barny_config_defaults(&state.config);

	TEST("clock lifecycle is complete")
	{
		barny_module_t *mod = barny_module_clock_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("disk lifecycle is complete")
	{
		barny_module_t *mod = barny_module_disk_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("ram lifecycle is complete")
	{
		barny_module_t *mod = barny_module_ram_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("network lifecycle is complete")
	{
		barny_module_t *mod = barny_module_network_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("fileread lifecycle is complete")
	{
		barny_module_t *mod = barny_module_fileread_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("sysinfo lifecycle is complete")
	{
		barny_module_t *mod = barny_module_sysinfo_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);

		free(mod);
	}

	TEST("weather lifecycle is complete")
	{
		barny_module_t *mod = barny_module_weather_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("crypto lifecycle is complete")
	{
		barny_module_t *mod = barny_module_crypto_create();

		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("workspace lifecycle is complete")
	{
		barny_module_t *mod = barny_module_workspace_create();

		state.sway_ipc_fd   = -1;
		mod->init(mod, &state);
		mod->update(mod);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("data is NULL after destroy")
	{
		barny_module_t *mod = barny_module_clock_create();

		mod->init(mod, &state);
		ASSERT_NOT_NULL(mod->data);
		if (mod->destroy)
			mod->destroy(mod);
		ASSERT_NULL(mod->data);
		free(mod);
	}

	TEST("double destroy does not crash")
	{
		barny_module_t *mod = barny_module_clock_create();

		mod->init(mod, &state);
		if (mod->destroy) {
			mod->destroy(mod);

			mod->destroy(mod);
		}
		free(mod);
	}

	TEST_SUITE_END();
}

void
test_module_null_font(void)
{
	TEST_SUITE_BEGIN("Module NULL Font Handling");

	barny_state_t state;

	state = (barny_state_t){ 0 };
	barny_config_defaults(&state.config);
	state.config.font = NULL;

	TEST("clock init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_clock_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("disk init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_disk_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("ram init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_ram_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("network init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_network_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("fileread init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_fileread_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("sysinfo init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_sysinfo_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("weather init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_weather_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("crypto init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod    = barny_module_crypto_create();
		result = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST("workspace init with NULL font")
	{
		barny_module_t *mod;
		int             result;

		mod               = barny_module_workspace_create();
		state.sway_ipc_fd = -1;
		result            = mod->init(mod, &state);
		ASSERT_EQ_INT(0, result);
		if (mod->destroy)
			mod->destroy(mod);
		free(mod);
	}

	TEST_SUITE_END();
}
