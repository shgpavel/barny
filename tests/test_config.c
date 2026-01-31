#include "test_framework.h"
#include "barny.h"
#include <unistd.h>

#define TEST_CONFIG_DIR "tests/fixtures/"

static char *
create_temp_config(const char *content)
{
	static char path[256];
	snprintf(path, sizeof(path), "/tmp/barny_test_config_%d.conf", getpid());
	FILE *f = fopen(path, "w");
	if (f) {
		fputs(content, f);
		fclose(f);
	}
	return path;
}

static void
cleanup_temp_config(const char *path)
{
	unlink(path);
}

void
test_config_defaults(void)
{
	TEST_SUITE_BEGIN("Config Defaults");

	TEST("default height is set")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(BARNY_DEFAULT_HEIGHT, config.height);
	}

	TEST("default margins are zero")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, config.margin_top);
		ASSERT_EQ_INT(0, config.margin_bottom);
		ASSERT_EQ_INT(0, config.margin_left);
		ASSERT_EQ_INT(0, config.margin_right);
	}

	TEST("default border radius is set")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(BARNY_BORDER_RADIUS, config.border_radius);
	}

	TEST("default position is top")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.position_top);
	}

	TEST("default blur radius is set")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_DBL(BARNY_BLUR_RADIUS, config.blur_radius, 0.001);
	}

	TEST("default brightness is 1.1")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_DBL(1.1, config.brightness, 0.001);
	}

	TEST("default refraction mode is lens")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(BARNY_REFRACT_LENS, config.refraction_mode);
	}

	TEST("default displacement scale is 8.0")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_DBL(8.0, config.displacement_scale, 0.001);
	}

	TEST("default chromatic aberration is 1.5")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_DBL(1.5, config.chromatic_aberration, 0.001);
	}

	TEST("default pointers are NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.font);
		ASSERT_NULL(config.wallpaper_path);
	}

	/* Clock module defaults */
	TEST("clock default show_time is true")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.clock_show_time);
	}

	TEST("clock default 24h_format is true")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.clock_24h_format);
	}

	TEST("clock default show_seconds is true")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.clock_show_seconds);
	}

	TEST("clock default show_date is false")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_FALSE(config.clock_show_date);
	}

	TEST("clock default date_order is 0")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, config.clock_date_order);
	}

	TEST("clock default date_separator is /")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT('/', config.clock_date_separator);
	}

	/* Disk module defaults */
	TEST("disk default path is NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.disk_path);
	}

	TEST("disk default show_percentage is false")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_FALSE(config.disk_show_percentage);
	}

	TEST("disk default decimals is 0")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, config.disk_decimals);
	}

	/* CPU temp module defaults */
	TEST("cpu_temp default path is NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.cpu_temp_path);
	}

	TEST("cpu_temp default zone is 0")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, config.cpu_temp_zone);
	}

	TEST("cpu_temp default show_unit is true")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.cpu_temp_show_unit);
	}

	/* RAM module defaults */
	TEST("ram default show_percentage is false")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_FALSE(config.ram_show_percentage);
	}

	TEST("ram default decimals is 1")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(1, config.ram_decimals);
	}

	TEST("ram default used_method is NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.ram_used_method);
	}

	/* Network module defaults */
	TEST("network default interface is NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.network_interface);
	}

	TEST("network default show_ip is true")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.network_show_ip);
	}

	TEST("network default show_interface is false")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_FALSE(config.network_show_interface);
	}

	TEST("network default prefer_ipv4 is true")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_TRUE(config.network_prefer_ipv4);
	}

	/* Fileread module defaults */
	TEST("fileread default path is NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.fileread_path);
	}

	TEST("fileread default title is NULL")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_NULL(config.fileread_title);
	}

	TEST("fileread default max_chars is 64")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		ASSERT_EQ_INT(64, config.fileread_max_chars);
	}

	TEST_SUITE_END();
}

void
test_config_load(void)
{
	TEST_SUITE_BEGIN("Config File Loading");

	TEST("returns -1 for non-existent file")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		int result = barny_config_load(&config,
		                               "/nonexistent/path/config.conf");
		ASSERT_EQ_INT(-1, result);
	}

	TEST("parses height correctly")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path   = create_temp_config("height = 64\n");
		int         result = barny_config_load(&config, path);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_INT(64, config.height);
		cleanup_temp_config(path);
	}

	TEST("parses margins correctly")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("margin_top = 10\n"
		                                      "margin_bottom = 20\n"
		                                      "margin_left = 30\n"
		                                      "margin_right = 40\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(10, config.margin_top);
		ASSERT_EQ_INT(20, config.margin_bottom);
		ASSERT_EQ_INT(30, config.margin_left);
		ASSERT_EQ_INT(40, config.margin_right);
		cleanup_temp_config(path);
	}

	TEST("parses position top")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		config.position_top = false;
		const char *path    = create_temp_config("position = top\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.position_top);
		cleanup_temp_config(path);
	}

	TEST("parses position bottom")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("position = bottom\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.position_top);
		cleanup_temp_config(path);
	}

	TEST("parses font with quotes")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("font = \"Inter Bold 14\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.font);
		ASSERT_EQ_STR("Inter Bold 14", config.font);
		free(config.font);
		cleanup_temp_config(path);
	}

	TEST("parses blur_radius as float")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("blur_radius = 5.5\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(5.5, config.blur_radius, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses brightness as float")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("brightness = 1.25\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(1.25, config.brightness, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses refraction mode none")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("refraction = none\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(BARNY_REFRACT_NONE, config.refraction_mode);
		cleanup_temp_config(path);
	}

	TEST("parses refraction mode lens")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		config.refraction_mode = BARNY_REFRACT_NONE;
		const char *path       = create_temp_config("refraction = lens\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(BARNY_REFRACT_LENS, config.refraction_mode);
		cleanup_temp_config(path);
	}

	TEST("parses refraction mode liquid")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("refraction = liquid\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(BARNY_REFRACT_LIQUID, config.refraction_mode);
		cleanup_temp_config(path);
	}

	TEST("parses displacement_scale")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("displacement_scale = 25.5\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(25.5, config.displacement_scale, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses chromatic_aberration")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("chromatic_aberration = 3.0\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(3.0, config.chromatic_aberration, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses noise_octaves")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("noise_octaves = 4\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(4, config.noise_octaves);
		cleanup_temp_config(path);
	}

	TEST("ignores comments")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("# This is a comment\n"
		                                      "height = 100\n"
		                                      "# Another comment\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(100, config.height);
		cleanup_temp_config(path);
	}

	TEST("ignores empty lines")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("\n"
		                                      "height = 100\n"
		                                      "\n"
		                                      "\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(100, config.height);
		cleanup_temp_config(path);
	}

	TEST("handles whitespace around values")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("  height   =   100   \n"
		                                      "  border_radius=50\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(100, config.height);
		ASSERT_EQ_INT(50, config.border_radius);
		cleanup_temp_config(path);
	}

	TEST("parses complete config file")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("# Complete test config\n"
		                             "position = bottom\n"
		                             "height = 32\n"
		                             "margin_top = 5\n"
		                             "margin_bottom = 5\n"
		                             "margin_left = 10\n"
		                             "margin_right = 10\n"
		                             "border_radius = 16\n"
		                             "font = \"Monospace 10\"\n"
		                             "blur_radius = 3\n"
		                             "brightness = 1.2\n"
		                             "refraction = liquid\n"
		                             "displacement_scale = 15\n"
		                             "chromatic_aberration = 2.0\n"
		                             "edge_refraction = 1.5\n"
		                             "noise_scale = 0.05\n"
		                             "noise_octaves = 3\n");
		barny_config_load(&config, path);

		ASSERT_FALSE(config.position_top);
		ASSERT_EQ_INT(32, config.height);
		ASSERT_EQ_INT(5, config.margin_top);
		ASSERT_EQ_INT(5, config.margin_bottom);
		ASSERT_EQ_INT(10, config.margin_left);
		ASSERT_EQ_INT(10, config.margin_right);
		ASSERT_EQ_INT(16, config.border_radius);
		ASSERT_EQ_STR("Monospace 10", config.font);
		ASSERT_EQ_DBL(3.0, config.blur_radius, 0.001);
		ASSERT_EQ_DBL(1.2, config.brightness, 0.001);
		ASSERT_EQ_INT(BARNY_REFRACT_LIQUID, config.refraction_mode);
		ASSERT_EQ_DBL(15.0, config.displacement_scale, 0.001);
		ASSERT_EQ_DBL(2.0, config.chromatic_aberration, 0.001);
		ASSERT_EQ_DBL(1.5, config.edge_refraction, 0.001);
		ASSERT_EQ_DBL(0.05, config.noise_scale, 0.001);
		ASSERT_EQ_INT(3, config.noise_octaves);

		free(config.font);
		cleanup_temp_config(path);
	}

	/* Clock module parsing */
	TEST("parses clock_show_time")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("clock_show_time = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.clock_show_time);
		cleanup_temp_config(path);
	}

	TEST("parses clock_24h_format")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("clock_24h_format = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.clock_24h_format);
		cleanup_temp_config(path);
	}

	TEST("parses clock_show_date")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("clock_show_date = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.clock_show_date);
		cleanup_temp_config(path);
	}

	TEST("parses clock_date_order")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("clock_date_order = 2\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.clock_date_order);
		cleanup_temp_config(path);
	}

	TEST("clock_date_order clamps to 0-2")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("clock_date_order = 5\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.clock_date_order);
		cleanup_temp_config(path);
	}

	TEST("parses clock_date_separator")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("clock_date_separator = -\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT('-', config.clock_date_separator);
		cleanup_temp_config(path);
	}

	/* Disk module parsing */
	TEST("parses disk_path")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("disk_path = \"/home\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.disk_path);
		ASSERT_EQ_STR("/home", config.disk_path);
		free(config.disk_path);
		cleanup_temp_config(path);
	}

	TEST("parses disk_show_percentage")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("disk_show_percentage = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.disk_show_percentage);
		cleanup_temp_config(path);
	}

	TEST("parses disk_decimals")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("disk_decimals = 2\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.disk_decimals);
		cleanup_temp_config(path);
	}

	/* CPU temp module parsing */
	TEST("parses cpu_temp_path")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config(
		        "cpu_temp_path = \"/sys/class/thermal/thermal_zone0/temp\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.cpu_temp_path);
		ASSERT_EQ_STR("/sys/class/thermal/thermal_zone0/temp",
		              config.cpu_temp_path);
		free(config.cpu_temp_path);
		cleanup_temp_config(path);
	}

	TEST("parses cpu_temp_zone")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("cpu_temp_zone = 3\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(3, config.cpu_temp_zone);
		cleanup_temp_config(path);
	}

	TEST("parses cpu_temp_show_unit")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("cpu_temp_show_unit = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.cpu_temp_show_unit);
		cleanup_temp_config(path);
	}

	/* RAM module parsing */
	TEST("parses ram_show_percentage")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("ram_show_percentage = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.ram_show_percentage);
		cleanup_temp_config(path);
	}

	TEST("parses ram_decimals")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("ram_decimals = 2\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.ram_decimals);
		cleanup_temp_config(path);
	}

	TEST("parses ram_used_method")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("ram_used_method = \"free\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.ram_used_method);
		ASSERT_EQ_STR("free", config.ram_used_method);
		free(config.ram_used_method);
		cleanup_temp_config(path);
	}

	/* Network module parsing */
	TEST("parses network_interface")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("network_interface = \"wlan0\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.network_interface);
		ASSERT_EQ_STR("wlan0", config.network_interface);
		free(config.network_interface);
		cleanup_temp_config(path);
	}

	TEST("parses network_show_ip")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("network_show_ip = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.network_show_ip);
		cleanup_temp_config(path);
	}

	TEST("parses network_show_interface")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("network_show_interface = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.network_show_interface);
		cleanup_temp_config(path);
	}

	TEST("parses network_prefer_ipv4")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("network_prefer_ipv4 = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.network_prefer_ipv4);
		cleanup_temp_config(path);
	}

	/* Fileread module parsing */
	TEST("parses fileread_path")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("fileread_path = \"/tmp/status\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.fileread_path);
		ASSERT_EQ_STR("/tmp/status", config.fileread_path);
		free(config.fileread_path);
		cleanup_temp_config(path);
	}

	TEST("parses fileread_title")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path
		        = create_temp_config("fileread_title = \"Status\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.fileread_title);
		ASSERT_EQ_STR("Status", config.fileread_title);
		free(config.fileread_title);
		cleanup_temp_config(path);
	}

	TEST("parses fileread_max_chars")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("fileread_max_chars = 128\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(128, config.fileread_max_chars);
		cleanup_temp_config(path);
	}

	TEST("fileread_max_chars clamps to 1-256")
	{
		barny_config_t config;
		barny_config_defaults(&config);
		const char *path = create_temp_config("fileread_max_chars = 500\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(256, config.fileread_max_chars);
		cleanup_temp_config(path);
	}

	TEST_SUITE_END();
}
