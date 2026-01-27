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

	TEST_SUITE_END();
}
