#include "test_framework.h"

#include "../src/modules/ram.c"

void
test_format_size(void)
{
	TEST_SUITE_BEGIN("format_size");

	char buf[32];

	TEST("formats megabytes")
	{
		unsigned long kb = 500UL * 1024;

		format_size(buf, sizeof(buf), kb, 1, false);
		ASSERT_EQ_STR("500.0M", buf);
	}

	TEST("formats gigabytes with 0 decimals")
	{
		unsigned long kb = 8UL * 1024 * 1024;

		format_size(buf, sizeof(buf), kb, 0, false);
		ASSERT_EQ_STR("8G", buf);
	}

	TEST("formats gigabytes with 1 decimal")
	{
		unsigned long kb = (unsigned long)(8.5 * 1024 * 1024);

		format_size(buf, sizeof(buf), kb, 1, false);
		ASSERT_EQ_STR("8.5G", buf);
	}

	TEST("formats gigabytes with 2 decimals")
	{
		unsigned long kb = (unsigned long)(8.25 * 1024 * 1024);

		format_size(buf, sizeof(buf), kb, 2, false);
		ASSERT_EQ_STR("8.25G", buf);
	}

	TEST("formats with unit_space")
	{
		unsigned long kb = 8UL * 1024 * 1024;

		format_size(buf, sizeof(buf), kb, 0, true);
		ASSERT_EQ_STR("8 G", buf);
	}

	TEST("formats sub-GB (MB range)")
	{
		unsigned long kb = 256UL * 1024;

		format_size(buf, sizeof(buf), kb, 0, false);
		ASSERT_EQ_STR("256M", buf);
	}

	TEST("boundary at 1 GB")
	{
		unsigned long kb = 1UL * 1024 * 1024;

		format_size(buf, sizeof(buf), kb, 1, false);
		ASSERT_EQ_STR("1.0G", buf);
	}

	TEST_SUITE_END();
}
