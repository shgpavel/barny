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

	TEST("crypto pairs default to unset")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_NULL(config.crypto_pairs);
		ASSERT_EQ_INT(0, config.crypto_pair_count);
	}

	TEST("popup gaps default to 0")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, config.crypto_popup_gap);
		ASSERT_EQ_INT(0, config.sysinfo_popup_gap);
		ASSERT_EQ_INT(0, config.network_popup_gap);
		ASSERT_EQ_INT(0, config.weather_popup_gap);
	}

	TEST("popup content defaults match docs")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, (int)config.sysinfo_popup_per_core);
		ASSERT_EQ_INT(1, (int)config.network_popup_show_ssid);
		ASSERT_EQ_INT(0, (int)config.network_popup_show_ipv6);
		ASSERT_EQ_INT(0, (int)config.network_popup_show_mac);
		ASSERT_EQ_INT(1, (int)config.weather_popup_show_humidity);
		ASSERT_EQ_INT(1, (int)config.weather_popup_show_wind);
		ASSERT_EQ_INT(0, (int)config.weather_popup_show_pressure);
		ASSERT_EQ_INT(1, (int)config.weather_popup_show_feels_like);
	}

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

	TEST("disk default path is NULL")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_NULL(config.disk_path);
	}

	TEST("disk default mode is NULL (used_total)")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_NULL(config.disk_mode);
	}

	TEST("disk default unit_space is false")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_FALSE(config.disk_unit_space);
	}

	TEST("disk default decimals is 0")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_EQ_INT(0, config.disk_decimals);
	}

	TEST("sysinfo_temp default path is NULL")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_NULL(config.sysinfo_temp_path);
	}

	TEST("sysinfo_temp default zone is 0")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_EQ_INT(-1, config.sysinfo_temp_zone);
	}

	TEST("sysinfo_temp default show_unit is true")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_TRUE(config.sysinfo_temp_show_unit);
	}

	TEST("ram default mode is NULL (used_total)")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_NULL(config.ram_mode);
	}

	TEST("ram default unit_space is false")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		ASSERT_FALSE(config.ram_unit_space);
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
		int            result;

		barny_config_defaults(&config);
		result = barny_config_load(&config, "/nonexistent/path/config.conf");
		ASSERT_EQ_INT(-1, result);
	}

	TEST("parses height correctly")
	{
		barny_config_t config;
		const char    *path;
		int            result;

		barny_config_defaults(&config);
		path   = create_temp_config("height = 64\n");
		result = barny_config_load(&config, path);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_INT(64, config.height);
		cleanup_temp_config(path);
	}

	TEST("parses margins correctly")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("margin_top = 10\n"
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

	TEST("exclusive zone includes bar height and both vertical margins")
	{
		barny_config_t config;

		barny_config_defaults(&config);
		config.height        = 47;
		config.margin_top    = 8;
		config.margin_bottom = 10;
		config.position_top  = false;
		ASSERT_EQ_INT(65, barny_config_exclusive_zone(&config));

		config.position_top = true;
		ASSERT_EQ_INT(65, barny_config_exclusive_zone(&config));
	}

	TEST("parses position top")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		config.position_top = false;
		path                = create_temp_config("position = top\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.position_top);
		cleanup_temp_config(path);
	}

	TEST("parses position bottom")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("position = bottom\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.position_top);
		cleanup_temp_config(path);
	}

	TEST("parses font with quotes")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("font = \"Inter Bold 14\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.font);
		ASSERT_EQ_STR("Inter Bold 14", config.font);
		free(config.font);
		cleanup_temp_config(path);
	}

	TEST("parses blur_radius as float")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("blur_radius = 5.5\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(5.5, config.blur_radius, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses brightness as float")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("brightness = 1.25\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(1.25, config.brightness, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses refraction mode none")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("refraction = none\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(BARNY_REFRACT_NONE, config.refraction_mode);
		cleanup_temp_config(path);
	}

	TEST("parses refraction mode lens")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		config.refraction_mode = BARNY_REFRACT_NONE;
		path                   = create_temp_config("refraction = lens\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(BARNY_REFRACT_LENS, config.refraction_mode);
		cleanup_temp_config(path);
	}

	TEST("parses refraction mode liquid")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("refraction = liquid\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(BARNY_REFRACT_LIQUID, config.refraction_mode);
		cleanup_temp_config(path);
	}

	TEST("parses displacement_scale")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("displacement_scale = 25.5\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(25.5, config.displacement_scale, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses chromatic_aberration")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("chromatic_aberration = 3.0\n");
		barny_config_load(&config, path);
		ASSERT_EQ_DBL(3.0, config.chromatic_aberration, 0.001);
		cleanup_temp_config(path);
	}

	TEST("parses noise_octaves")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("noise_octaves = 4\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(4, config.noise_octaves);
		cleanup_temp_config(path);
	}

	TEST("ignores comments")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("# This is a comment\n"
		                          "height = 100\n"
		                          "# Another comment\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(100, config.height);
		cleanup_temp_config(path);
	}

	TEST("ignores empty lines")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("\n"
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
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("  height   =   100   \n"
		                          "  border_radius=50\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(100, config.height);
		ASSERT_EQ_INT(50, config.border_radius);
		cleanup_temp_config(path);
	}

	TEST("parses complete config file")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("# Complete test config\n"
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

	TEST("parses clock_show_time")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("clock_show_time = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.clock_show_time);
		cleanup_temp_config(path);
	}

	TEST("parses clock_24h_format")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("clock_24h_format = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.clock_24h_format);
		cleanup_temp_config(path);
	}

	TEST("parses clock_show_date")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("clock_show_date = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.clock_show_date);
		cleanup_temp_config(path);
	}

	TEST("parses clock_date_order")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("clock_date_order = 2\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.clock_date_order);
		cleanup_temp_config(path);
	}

	TEST("clock_date_order clamps to 0-2")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("clock_date_order = 5\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.clock_date_order);
		cleanup_temp_config(path);
	}

	TEST("parses clock_date_separator")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("clock_date_separator = -\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT('-', config.clock_date_separator);
		cleanup_temp_config(path);
	}

	TEST("parses disk_path")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("disk_path = \"/home\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.disk_path);
		ASSERT_EQ_STR("/home", config.disk_path);
		free(config.disk_path);
		cleanup_temp_config(path);
	}

	TEST("parses disk_mode")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("disk_mode = free\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.disk_mode);
		ASSERT_EQ_STR("free", config.disk_mode);
		free(config.disk_mode);
		cleanup_temp_config(path);
	}

	TEST("parses disk_unit_space")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("disk_unit_space = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.disk_unit_space);
		cleanup_temp_config(path);
	}

	TEST("parses disk_decimals")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("disk_decimals = 2\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.disk_decimals);
		cleanup_temp_config(path);
	}

	TEST("parses sysinfo_temp_path")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config(
		        "sysinfo_temp_path = \"/sys/class/thermal/thermal_zone0/temp\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.sysinfo_temp_path);
		ASSERT_EQ_STR("/sys/class/thermal/thermal_zone0/temp",
		              config.sysinfo_temp_path);
		free(config.sysinfo_temp_path);
		cleanup_temp_config(path);
	}

	TEST("parses sysinfo_temp_zone")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("sysinfo_temp_zone = 3\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(3, config.sysinfo_temp_zone);
		cleanup_temp_config(path);
	}

	TEST("parses sysinfo_temp_show_unit")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("sysinfo_temp_show_unit = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.sysinfo_temp_show_unit);
		cleanup_temp_config(path);
	}

	TEST("parses ram_mode")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("ram_mode = used\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.ram_mode);
		ASSERT_EQ_STR("used", config.ram_mode);
		free(config.ram_mode);
		cleanup_temp_config(path);
	}

	TEST("parses ram_unit_space")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("ram_unit_space = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.ram_unit_space);
		cleanup_temp_config(path);
	}

	TEST("parses ram_decimals")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("ram_decimals = 2\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.ram_decimals);
		cleanup_temp_config(path);
	}

	TEST("parses ram_used_method")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("ram_used_method = \"free\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.ram_used_method);
		ASSERT_EQ_STR("free", config.ram_used_method);
		free(config.ram_used_method);
		cleanup_temp_config(path);
	}

	TEST("parses network_interface")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("network_interface = \"wlan0\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.network_interface);
		ASSERT_EQ_STR("wlan0", config.network_interface);
		free(config.network_interface);
		cleanup_temp_config(path);
	}

	TEST("parses network_show_ip")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("network_show_ip = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.network_show_ip);
		cleanup_temp_config(path);
	}

	TEST("parses network_show_interface")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("network_show_interface = true\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.network_show_interface);
		cleanup_temp_config(path);
	}

	TEST("parses network_prefer_ipv4")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("network_prefer_ipv4 = false\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.network_prefer_ipv4);
		cleanup_temp_config(path);
	}

	TEST("parses fileread_path")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("fileread_path = \"/tmp/status\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.fileread_path);
		ASSERT_EQ_STR("/tmp/status", config.fileread_path);
		free(config.fileread_path);
		cleanup_temp_config(path);
	}

	TEST("parses fileread_title")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("fileread_title = \"Status\"\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.fileread_title);
		ASSERT_EQ_STR("Status", config.fileread_title);
		free(config.fileread_title);
		cleanup_temp_config(path);
	}

	TEST("parses fileread_max_chars")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("fileread_max_chars = 128\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(128, config.fileread_max_chars);
		cleanup_temp_config(path);
	}

	TEST("fileread_max_chars clamps to 1-256")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("fileread_max_chars = 500\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(256, config.fileread_max_chars);
		cleanup_temp_config(path);
	}

	TEST_SUITE_END();
}

void
test_config_edge_cases(void)
{
	TEST_SUITE_BEGIN("Config Edge Cases");

	TEST("parses hex color via config load")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("text_color = \"#FF5500\"\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.text_color_set);
		ASSERT_EQ_DBL(1.0, config.text_color_r, 0.01);
		ASSERT_EQ_DBL(85.0 / 255.0, config.text_color_g, 0.01);
		ASSERT_EQ_DBL(0.0, config.text_color_b, 0.01);
		free(config.text_color);
		cleanup_temp_config(path);
	}

	TEST("parses named color 'white'")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("text_color = white\n");
		barny_config_load(&config, path);
		ASSERT_TRUE(config.text_color_set);
		ASSERT_EQ_DBL(1.0, config.text_color_r, 0.01);
		ASSERT_EQ_DBL(1.0, config.text_color_g, 0.01);
		ASSERT_EQ_DBL(1.0, config.text_color_b, 0.01);
		free(config.text_color);
		cleanup_temp_config(path);
	}

	TEST("invalid color leaves text_color_set false")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("text_color = invalid\n");
		barny_config_load(&config, path);
		ASSERT_FALSE(config.text_color_set);
		free(config.text_color);
		cleanup_temp_config(path);
	}

	TEST("'default' resets text_color")
	{
		barny_config_t config;
		const char    *path1;
		const char    *path2;

		barny_config_defaults(&config);

		path1 = create_temp_config("text_color = white\n");
		barny_config_load(&config, path1);
		ASSERT_TRUE(config.text_color_set);
		free(config.text_color);
		cleanup_temp_config(path1);

		barny_config_defaults(&config);
		path2 = create_temp_config("text_color = default\n");
		barny_config_load(&config, path2);
		ASSERT_FALSE(config.text_color_set);
		ASSERT_NULL(config.text_color);
		cleanup_temp_config(path2);
	}

	TEST("parses workspace_names CSV")
	{
		barny_config_t config;
		const char    *path;
		int            i;

		barny_config_defaults(&config);
		path = create_temp_config(
		        "workspace_names = term, code, web, music\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(4, config.workspace_name_count);
		ASSERT_EQ_STR("term", config.workspace_names[0]);
		ASSERT_EQ_STR("code", config.workspace_names[1]);
		ASSERT_EQ_STR("web", config.workspace_names[2]);
		ASSERT_EQ_STR("music", config.workspace_names[3]);
		for (i = 0; i < config.workspace_name_count; i++)
			free(config.workspace_names[i]);
		free((void *)config.workspace_names);
		cleanup_temp_config(path);
	}

	TEST("workspace_names handles empty entries")
	{
		barny_config_t config;
		const char    *path;
		int            i;

		barny_config_defaults(&config);

		path = create_temp_config("workspace_names = one, , three\n");
		barny_config_load(&config, path);

		ASSERT_EQ_INT(2, config.workspace_name_count);
		ASSERT_EQ_STR("one", config.workspace_names[0]);
		ASSERT_EQ_STR("three", config.workspace_names[1]);
		for (i = 0; i < config.workspace_name_count; i++)
			free(config.workspace_names[i]);
		free((void *)config.workspace_names);
		cleanup_temp_config(path);
	}

	TEST("workspace_names single name")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("workspace_names = single\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(1, config.workspace_name_count);
		ASSERT_EQ_STR("single", config.workspace_names[0]);
		free(config.workspace_names[0]);
		free((void *)config.workspace_names);
		cleanup_temp_config(path);
	}

	TEST("parses crypto_pairs CSV")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config(
		        "crypto_pairs = BTC-USDT-SWAP, ETH-USDT-SWAP, SOL-USDT-SWAP\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(3, config.crypto_pair_count);
		ASSERT_EQ_STR("BTC-USDT-SWAP", config.crypto_pairs[0]);
		ASSERT_EQ_STR("ETH-USDT-SWAP", config.crypto_pairs[1]);
		ASSERT_EQ_STR("SOL-USDT-SWAP", config.crypto_pairs[2]);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("crypto_pairs skips empty entries")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config(
		        "crypto_pairs = BTC-USDT-SWAP, , DOGE-USDT-SWAP\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(2, config.crypto_pair_count);
		ASSERT_EQ_STR("BTC-USDT-SWAP", config.crypto_pairs[0]);
		ASSERT_EQ_STR("DOGE-USDT-SWAP", config.crypto_pairs[1]);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("parses sysinfo_popup_gap and clamps")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("sysinfo_popup_gap = 12\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(12, config.sysinfo_popup_gap);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("parses sysinfo_popup_per_core true")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("sysinfo_popup_per_core = true\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(1, (int)config.sysinfo_popup_per_core);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("parses network popup options")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("network_popup_gap = 6\n"
		                          "network_popup_show_ssid = false\n"
		                          "network_popup_show_ipv6 = true\n"
		                          "network_popup_show_mac = true\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(6, config.network_popup_gap);
		ASSERT_EQ_INT(0, (int)config.network_popup_show_ssid);
		ASSERT_EQ_INT(1, (int)config.network_popup_show_ipv6);
		ASSERT_EQ_INT(1, (int)config.network_popup_show_mac);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("parses weather popup options")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("weather_popup_gap = 8\n"
		                          "weather_popup_show_humidity = false\n"
		                          "weather_popup_show_pressure = true\n"
		                          "weather_popup_show_feels_like = false\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(8, config.weather_popup_gap);
		ASSERT_EQ_INT(0, (int)config.weather_popup_show_humidity);
		ASSERT_EQ_INT(1, (int)config.weather_popup_show_pressure);
		ASSERT_EQ_INT(0, (int)config.weather_popup_show_feels_like);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("popup_gap clamps to 64")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("network_popup_gap = 999\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(64, config.network_popup_gap);
		barny_config_cleanup(&config);
		cleanup_temp_config(path);
	}

	TEST("parses workspace_shape")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("workspace_shape = square\n");
		barny_config_load(&config, path);
		ASSERT_NOT_NULL(config.workspace_shape);
		ASSERT_EQ_STR("square", config.workspace_shape);
		free(config.workspace_shape);
		cleanup_temp_config(path);
	}

	TEST("duplicate key uses last value")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("height = 30\n"
		                          "height = 50\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(50, config.height);
		cleanup_temp_config(path);
	}

	TEST("handles inline comment")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("height = 42 # this is a comment\n");
		barny_config_load(&config, path);
		ASSERT_EQ_INT(42, config.height);
		cleanup_temp_config(path);
	}

	TEST("config reload replaces values")
	{
		barny_config_t config;
		const char    *path1;
		const char    *path2;

		barny_config_defaults(&config);

		path1 = create_temp_config("height = 30\n");
		barny_config_load(&config, path1);
		ASSERT_EQ_INT(30, config.height);
		cleanup_temp_config(path1);

		path2 = create_temp_config("height = 60\n");
		barny_config_load(&config, path2);
		ASSERT_EQ_INT(60, config.height);
		cleanup_temp_config(path2);
	}

	TEST("negative value for margin")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("margin_top = -10\n");
		barny_config_load(&config, path);

		ASSERT_EQ_INT(-10, config.margin_top);
		cleanup_temp_config(path);
	}

	TEST("empty value for string field")
	{
		barny_config_t config;
		const char    *path;

		barny_config_defaults(&config);
		path = create_temp_config("disk_mode = \"\"\n");
		barny_config_load(&config, path);

		ASSERT_NOT_NULL(config.disk_mode);
		ASSERT_EQ_STR("", config.disk_mode);
		free(config.disk_mode);
		cleanup_temp_config(path);
	}

	TEST_SUITE_END();
}
