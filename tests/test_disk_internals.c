/*
 * Tests for static functions in disk.c
 * We #include the source file directly to access static functions.
 */

#include "test_framework.h"

/* Include disk.c directly to access static functions */
#include "../src/modules/disk.c"

void
test_format_bytes(void)
{
	TEST_SUITE_BEGIN("format_bytes");

	char buf[32];

	TEST("formats megabytes")
	{
		/* 500 MB */
		unsigned long long bytes = 500ULL * 1024 * 1024;
		format_bytes(buf, sizeof(buf), bytes, 1, false);
		ASSERT_EQ_STR("500.0M", buf);
	}

	TEST("formats gigabytes with 0 decimals")
	{
		/* 10 GB */
		unsigned long long bytes = 10ULL * 1024 * 1024 * 1024;
		format_bytes(buf, sizeof(buf), bytes, 0, false);
		ASSERT_EQ_STR("10G", buf);
	}

	TEST("formats gigabytes with 1 decimal")
	{
		/* 10.5 GB */
		unsigned long long bytes = (unsigned long long)(10.5 * 1024 * 1024 * 1024);
		format_bytes(buf, sizeof(buf), bytes, 1, false);
		ASSERT_EQ_STR("10.5G", buf);
	}

	TEST("formats gigabytes with 2 decimals")
	{
		/* 10.25 GB */
		unsigned long long bytes = (unsigned long long)(10.25 * 1024 * 1024 * 1024);
		format_bytes(buf, sizeof(buf), bytes, 2, false);
		ASSERT_EQ_STR("10.25G", buf);
	}

	TEST("formats terabytes")
	{
		/* 2 TB */
		unsigned long long bytes = 2ULL * 1024 * 1024 * 1024 * 1024;
		format_bytes(buf, sizeof(buf), bytes, 1, false);
		ASSERT_EQ_STR("2.0T", buf);
	}

	TEST("formats with unit_space")
	{
		/* 10 GB with space before unit */
		unsigned long long bytes = 10ULL * 1024 * 1024 * 1024;
		format_bytes(buf, sizeof(buf), bytes, 0, true);
		ASSERT_EQ_STR("10 G", buf);
	}

	TEST("formats zero bytes")
	{
		format_bytes(buf, sizeof(buf), 0, 1, false);
		ASSERT_EQ_STR("0.0M", buf);
	}

	TEST("boundary between GB and TB (999 GB)")
	{
		/* 999 GB - should still be GB */
		unsigned long long bytes = 999ULL * 1024 * 1024 * 1024;
		format_bytes(buf, sizeof(buf), bytes, 0, false);
		ASSERT_EQ_STR("999G", buf);
	}

	TEST_SUITE_END();
}
