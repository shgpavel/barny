#include "test_framework.h"
#include <time.h>

#include "../src/modules/clock.c"

static struct tm
make_tm(int hour, int min, int sec, int mday, int mon, int year, int wday)
{
	struct tm tm = { 0 };

	tm.tm_hour   = hour;
	tm.tm_min    = min;
	tm.tm_sec    = sec;
	tm.tm_mday   = mday;
	tm.tm_mon    = mon - 1;
	tm.tm_year   = year - 1900;
	tm.tm_wday   = wday;

	return tm;
}

void
test_build_time_string(void)
{
	TEST_SUITE_BEGIN("build_time_string");

	char           buf[64];
	barny_config_t cfg;

	barny_config_defaults(&cfg);

	TEST("24h format with seconds")
	{
		struct tm tm           = make_tm(14, 30, 45, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = true;
		cfg.clock_show_seconds = true;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("14:30:45", buf);
	}

	TEST("24h format without seconds")
	{
		struct tm tm           = make_tm(14, 30, 45, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = true;
		cfg.clock_show_seconds = false;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("14:30", buf);
	}

	TEST("12h format with seconds")
	{
		struct tm tm           = make_tm(14, 30, 45, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = false;
		cfg.clock_show_seconds = true;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2:30:45 PM", buf);
	}

	TEST("12h format without seconds")
	{
		struct tm tm           = make_tm(14, 30, 45, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = false;
		cfg.clock_show_seconds = false;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2:30 PM", buf);
	}

	TEST("12h format at midnight")
	{
		struct tm tm           = make_tm(0, 0, 0, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = false;
		cfg.clock_show_seconds = false;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("12:00 AM", buf);
	}

	TEST("12h format at noon")
	{
		struct tm tm           = make_tm(12, 0, 0, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = false;
		cfg.clock_show_seconds = false;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("12:00 PM", buf);
	}

	TEST("24h format at midnight")
	{
		struct tm tm           = make_tm(0, 0, 0, 1, 1, 2024, 0);

		cfg.clock_show_time    = true;
		cfg.clock_24h_format   = true;
		cfg.clock_show_seconds = false;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("00:00", buf);
	}

	TEST("show_time=false returns empty")
	{
		struct tm tm        = make_tm(14, 30, 45, 1, 1, 2024, 0);

		cfg.clock_show_time = false;
		build_time_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("", buf);
	}

	TEST_SUITE_END();
}

void
test_build_date_string(void)
{
	TEST_SUITE_BEGIN("build_date_string");

	char           buf[64];
	barny_config_t cfg;

	barny_config_defaults(&cfg);

	TEST("date order 0 (dd/mm/yyyy)")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15/06/2024", buf);
	}

	TEST("date order 1 (mm/dd/yyyy)")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 1;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("06/15/2024", buf);
	}

	TEST("date order 2 (yyyy/mm/dd)")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 2;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2024/06/15", buf);
	}

	TEST("dash separator")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 2;
		cfg.clock_date_separator = '-';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("2024-06-15", buf);
	}

	TEST("dot separator")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '.';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15.06.2024", buf);
	}

	TEST("with weekday")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 6);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = true;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);

		ASSERT_TRUE(strlen(buf) > 10);
	}

	TEST("hide year")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = false;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15/06", buf);
	}

	TEST("hide month")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = false;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("15/2024", buf);
	}

	TEST("hide day")
	{
		struct tm tm             = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date      = true;
		cfg.clock_show_day       = false;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '/';
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("06/2024", buf);
	}

	TEST("show_date=false returns empty")
	{
		struct tm tm        = make_tm(12, 0, 0, 15, 6, 2024, 0);

		cfg.clock_show_date = false;
		build_date_string(buf, sizeof(buf), &tm, &cfg);
		ASSERT_EQ_STR("", buf);
	}

	TEST_SUITE_END();
}

void
test_clock_display_str(void)
{
	TEST_SUITE_BEGIN("clock display string combination");

	char           time_buf[64];
	char           date_buf[64];
	char           combined[128];
	barny_config_t cfg;
	struct tm      tm;

	barny_config_defaults(&cfg);
	tm = make_tm(14, 30, 0, 15, 6, 2024, 6);

	TEST("time only")
	{
		cfg.clock_show_time    = true;
		cfg.clock_show_seconds = false;
		cfg.clock_24h_format   = true;
		cfg.clock_show_date    = false;

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		ASSERT_EQ_STR("14:30", time_buf);
		ASSERT_EQ_STR("", date_buf);
	}

	TEST("date only")
	{
		cfg.clock_show_time      = false;
		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 0;
		cfg.clock_date_separator = '/';

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		ASSERT_EQ_STR("", time_buf);
		ASSERT_EQ_STR("15/06/2024", date_buf);
	}

	TEST("combined time and date")
	{
		cfg.clock_show_time      = true;
		cfg.clock_show_seconds   = false;
		cfg.clock_24h_format     = true;
		cfg.clock_show_date      = true;
		cfg.clock_show_day       = true;
		cfg.clock_show_month     = true;
		cfg.clock_show_year      = true;
		cfg.clock_show_weekday   = false;
		cfg.clock_date_order     = 2;
		cfg.clock_date_separator = '-';

		build_time_string(time_buf, sizeof(time_buf), &tm, &cfg);
		build_date_string(date_buf, sizeof(date_buf), &tm, &cfg);

		if (time_buf[0] && date_buf[0]) {
			snprintf(combined, sizeof(combined), "%s  %s", time_buf,
			         date_buf);
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
