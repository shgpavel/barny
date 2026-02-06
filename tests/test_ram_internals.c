/*
 * Tests for static functions in ram.c
 * We #include the source file directly to access static functions.
 */

#include "test_framework.h"

/* Include ram.c directly to access static functions */
#include "../src/modules/ram.c"

void
test_format_size(void)
{
	TEST_SUITE_BEGIN("format_size");

	char buf[32];

	TEST("formats megabytes")
	{
		/* 500 MB in KB */
		unsigned long kb = 500UL * 1024;
		format_size(buf, sizeof(buf), kb, 1, false);
		ASSERT_EQ_STR("500.0M", buf);
	}

	TEST("formats gigabytes with 0 decimals")
	{
		/* 8 GB in KB */
		unsigned long kb = 8UL * 1024 * 1024;
		format_size(buf, sizeof(buf), kb, 0, false);
		ASSERT_EQ_STR("8G", buf);
	}

	TEST("formats gigabytes with 1 decimal")
	{
		/* 8.5 GB in KB */
		unsigned long kb = (unsigned long)(8.5 * 1024 * 1024);
		format_size(buf, sizeof(buf), kb, 1, false);
		ASSERT_EQ_STR("8.5G", buf);
	}

	TEST("formats gigabytes with 2 decimals")
	{
		/* 8.25 GB in KB */
		unsigned long kb = (unsigned long)(8.25 * 1024 * 1024);
		format_size(buf, sizeof(buf), kb, 2, false);
		ASSERT_EQ_STR("8.25G", buf);
	}

	TEST("formats with unit_space")
	{
		/* 8 GB with space before unit */
		unsigned long kb = 8UL * 1024 * 1024;
		format_size(buf, sizeof(buf), kb, 0, true);
		ASSERT_EQ_STR("8 G", buf);
	}

	TEST("formats sub-GB (MB range)")
	{
		/* 256 MB in KB - should show in MB */
		unsigned long kb = 256UL * 1024;
		format_size(buf, sizeof(buf), kb, 0, false);
		ASSERT_EQ_STR("256M", buf);
	}

	TEST("boundary at 1 GB")
	{
		/* Exactly 1 GB in KB - should switch to GB */
		unsigned long kb = 1UL * 1024 * 1024;
		format_size(buf, sizeof(buf), kb, 1, false);
		ASSERT_EQ_STR("1.0G", buf);
	}

	TEST_SUITE_END();
}
