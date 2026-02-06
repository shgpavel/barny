/*
 * Tests for static functions in clock.c
 * We #include the source file directly to access static functions.
 */

#include "test_framework.h"
#include <time.h>

/* Include clock.c directly to access static functions */
#include "../src/modules/clock.c"

/* Helper to create a tm struct for testing */
static struct tm
make_tm(int hour, int min, int sec, int mday, int mon, int year, int wday)
{
	struct tm tm = { 0 };
	tm.tm_hour = hour;
	tm.tm_min = min;
	tm.tm_sec = sec;
	tm.tm_mday = mday;
	tm.tm_mon = mon - 1;  /* 0-based month */
	tm.tm_year = year - 1900;
	tm.tm_wday = wday;
	return tm;
}

void
test_build_time_string(void)
{
	TEST_SUITE_BEGIN("build_time_string");

	char buf[64];
	barny_config_t cfg;
	barny_config_defaults(&cfg);

	TEST("24h format with seconds")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = true;
		cfg.clock_show_seconds = true;
		struct tm tm = make_tm(14, 30, 45, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("14:30:45", buf);
	}

	TEST("24h format without seconds")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = true;
		cfg.clock_show_seconds = false;
		struct tm tm = make_tm(14, 30, 45, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("14:30", buf);
	}

	TEST("12h format with seconds")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = false;
		cfg.clock_show_seconds = true;
		struct tm tm = make_tm(14, 30, 45, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2:30:45 PM", buf);
	}

	TEST("12h format without seconds")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = false;
		cfg.clock_show_seconds = false;
		struct tm tm = make_tm(14, 30, 45, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2:30 PM", buf);
	}

	TEST("12h format at midnight")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = false;
		cfg.clock_show_seconds = false;
		struct tm tm = make_tm(0, 0, 0, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("12:00 AM", buf);
	}

	TEST("12h format at noon")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = false;
		cfg.clock_show_seconds = false;
		struct tm tm = make_tm(12, 0, 0, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("12:00 PM", buf);
	}

	TEST("24h format at midnight")
	{
		cfg.clock_show_time = true;
		cfg.clock_24h_format = true;
		cfg.clock_show_seconds = false;
		struct tm tm = make_tm(0, 0, 0, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("00:00", buf);
	}

	TEST("show_time=false returns empty")
	{
		cfg.clock_show_time = false;
		struct tm tm = make_tm(14, 30, 45, 1, 1, 2024, 0);
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("", buf);
	}

	TEST_SUITE_END();
}

void
test_build_date_string(void)
{
	TEST_SUITE_BEGIN("build_date_string");

	char buf[64];
	barny_config_t cfg;
	barny_config_defaults(&cfg);

	TEST("date order 0 (dd/mm/yyyy)")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '/';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15/06/2024", buf);
	}

	TEST("date order 1 (mm/dd/yyyy)")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 1;
		cfg.clock_date_separator = '/';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("06/15/2024", buf);
	}

	TEST("date order 2 (yyyy/mm/dd)")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 2;
		cfg.clock_date_separator = '/';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2024/06/15", buf);
	}

	TEST("dash separator")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 2;
		cfg.clock_date_separator = '-';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2024-06-15", buf);
	}

	TEST("dot separator")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '.';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15.06.2024", buf);
	}

	TEST("with weekday")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = true;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '/';
		/* Create a time struct and let strftime handle the weekday */
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 6);  /* Saturday */
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		/* Should start with weekday abbreviation followed by space */
		ASSERT_TRUE(strlen(buf) > 10);
	}

	TEST("hide year")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = false;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '/';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15/06", buf);
	}

	TEST("hide month")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = false;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '/';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15/2024", buf);
	}

	TEST("hide day")
	{
		cfg.clock_show_date = true;
		cfg.clock_show_day = false;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '/';
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("06/2024", buf);
	}

	TEST("show_date=false returns empty")
	{
		cfg.clock_show_date = false;
		struct tm tm = make_tm(12, 0, 0, 15, 6, 2024, 0);
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("", buf);
	}

	TEST_SUITE_END();
}

void
test_clock_display_str(void)
{
	TEST_SUITE_BEGIN("clock display string combination");

	char time_buf[64];
	char date_buf[64];
	char combined[128];
	barny_config_t cfg;
	barny_config_defaults(&cfg);
	struct tm tm = make_tm(14, 30, 0, 15, 6, 2024, 6);

	TEST("time only")
	{
		cfg.clock_show_time = true;
		cfg.clock_show_seconds = false;
		cfg.clock_24h_format = true;
		cfg.clock_show_date = false;

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		ASSERT_EQ_STR("14:30", time_buf);
		ASSERT_EQ_STR("", date_buf);
	}

	TEST("date only")
	{
		cfg.clock_show_time = false;
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 0;
		cfg.clock_date_separator = '/';

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		ASSERT_EQ_STR("", time_buf);
		ASSERT_EQ_STR("15/06/2024", date_buf);
	}

	TEST("combined time and date")
	{
		cfg.clock_show_time = true;
		cfg.clock_show_seconds = false;
		cfg.clock_24h_format = true;
		cfg.clock_show_date = true;
		cfg.clock_show_day = true;
		cfg.clock_show_month = true;
		cfg.clock_show_year = true;
		cfg.clock_show_weekday = false;
		cfg.clock_date_order = 2;
		cfg.clock_date_separator = '-';

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		/* Combine as the update function does */
		if (time_buf[0] && date_buf[0]) {
			snprintf(combined, sizeof(combined), "%s  %s", time_buf, date_buf);
		} else if (time_buf[0]) {
			snprintf(combined, sizeof(combined), "%s", time_buf);
		} else if (date_buf[0]) {
			snprintf(combined, sizeof(combined), "%s", date_buf);
		} else {
			combined[0] = '\0';
		}

		ASSERT_EQ_STR("14:30  2024-06-15", combined);
	}

	TEST("both off returns empty")
	{
		cfg.clock_show_time = false;
		cfg.clock_show_date = false;

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		ASSERT_EQ_STR("", time_buf);
		ASSERT_EQ_STR("", date_buf);
	}

	TEST_SUITE_END();
}
