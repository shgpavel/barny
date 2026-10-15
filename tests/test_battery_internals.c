#include "test_framework.h"

#include <unistd.h>

#include "../src/modules/battery.c"

static void
put_file(const char *dir, const char *name, const char *text)
{
	char  path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "w");
	if (!f)
		return;
	fputs(text, f);
	fclose(f);
}

static void
drop_file(const char *dir, const char *name)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	unlink(path);
}

static void
battery_show(const char *dir, const char *file, char *out, size_t outlen)
{
	barny_state_t   state = { 0 };
	barny_module_t *mod;
	char            path[512];

	snprintf(path, sizeof(path), "%s/%s", dir, file);
	state.config.battery_path        = path;
	state.config.battery_show_status = true;

	mod = barny_module_battery_create();
	mod->init(mod, &state);
	mod->update(mod);
	snprintf(out, outlen, "%s",
	         ((battery_data_t *)mod->data)->display_str);
	mod->destroy(mod);
	free(mod);
}

void
test_battery_update(void)
{
	const char *tmp = getenv("TMPDIR");
	char        dir[256];
	char        a[64];
	char        b[64];

	TEST_SUITE_BEGIN("battery_update");

	snprintf(dir, sizeof(dir), "%s/barny-battery-XXXXXX",
	         tmp && *tmp ? tmp : "/tmp");
	if (!g_mkdtemp(dir)) {
		printf("  [SKIP] no temp dir\n");
		TEST_SUITE_END();
		return;
	}

	TEST("reads the capacity and status attributes beside uevent")
	{
		put_file(dir, "uevent", "");
		put_file(dir, "capacity", "57\n");
		put_file(dir, "status", "Charging\n");
		battery_show(dir, "uevent", a, sizeof(a));
		ASSERT_EQ_STR("CHG 57%", a);
	}

	TEST("a missing status attribute reads as no status")
	{
		drop_file(dir, "status");
		battery_show(dir, "uevent", a, sizeof(a));
		ASSERT_EQ_STR("57%", a);
	}

	TEST("falls back to uevent without a capacity attribute")
	{
		drop_file(dir, "capacity");
		put_file(dir, "uevent",
		         "POWER_SUPPLY_STATUS=Discharging\n"
		         "POWER_SUPPLY_CAPACITY=41\n");
		battery_show(dir, "uevent", a, sizeof(a));
		ASSERT_EQ_STR("BAT 41%", a);
	}

	TEST("attributes and uevent agree on a long status")
	{
		put_file(dir, "uevent",
		         "POWER_SUPPLY_STATUS=A very long battery status\n"
		         "POWER_SUPPLY_CAPACITY=88\n");
		battery_show(dir, "uevent", a, sizeof(a));
		put_file(dir, "capacity", "88\n");
		put_file(dir, "status", "A very long battery status\n");
		put_file(dir, "uevent", "");
		battery_show(dir, "uevent", b, sizeof(b));
		ASSERT_EQ_STR(a, b);
	}

	TEST("a path not named uevent is parsed as a uevent file")
	{
		put_file(dir, "dump",
		         "POWER_SUPPLY_STATUS=Full\n"
		         "POWER_SUPPLY_CAPACITY=100\n");
		battery_show(dir, "dump", a, sizeof(a));
		ASSERT_EQ_STR("FULL 100%", a);
	}

	drop_file(dir, "uevent");
	drop_file(dir, "dump");
	drop_file(dir, "capacity");
	drop_file(dir, "status");
	rmdir(dir);

	TEST_SUITE_END();
}
