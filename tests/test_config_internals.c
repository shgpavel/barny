/*
 * Tests for static functions in config.c
 * We #include the source file directly to access static functions.
 */

#include "test_framework.h"

/* Include config.c directly to access static functions */
#include "../src/config.c"

void
test_parse_hex_color(void)
{
	TEST_SUITE_BEGIN("parse_hex_color");

	double r, g, b;

	/* #RRGGBB format tests */
	TEST("parses #FFFFFF as white")
	{
		int result = parse_hex_color("#FFFFFF", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(1.0, r, 0.001);
		ASSERT_EQ_DBL(1.0, g, 0.001);
		ASSERT_EQ_DBL(1.0, b, 0.001);
	}

	TEST("parses #000000 as black")
	{
		int result = parse_hex_color("#000000", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(0.0, r, 0.001);
		ASSERT_EQ_DBL(0.0, g, 0.001);
		ASSERT_EQ_DBL(0.0, b, 0.001);
	}

	TEST("parses #FF0000 as red")
	{
		int result = parse_hex_color("#FF0000", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(1.0, r, 0.001);
		ASSERT_EQ_DBL(0.0, g, 0.001);
		ASSERT_EQ_DBL(0.0, b, 0.001);
	}

	TEST("parses #80FF40 as mixed color")
	{
		int result = parse_hex_color("#80FF40", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(128.0 / 255.0, r, 0.01);
		ASSERT_EQ_DBL(1.0, g, 0.001);
		ASSERT_EQ_DBL(64.0 / 255.0, b, 0.01);
	}

	/* Named color tests */
	TEST("parses named 'black'")
	{
		int result = parse_hex_color("black", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(0.0, r, 0.001);
		ASSERT_EQ_DBL(0.0, g, 0.001);
		ASSERT_EQ_DBL(0.0, b, 0.001);
	}

	TEST("parses named 'white'")
	{
		int result = parse_hex_color("white", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(1.0, r, 0.001);
		ASSERT_EQ_DBL(1.0, g, 0.001);
		ASSERT_EQ_DBL(1.0, b, 0.001);
	}

	TEST("case insensitive named 'BLACK'")
	{
		int result = parse_hex_color("BLACK", &r, &g, &b);
		ASSERT_EQ_INT(0, result);
		ASSERT_EQ_DBL(0.0, r, 0.001);
	}

	/* Invalid format tests */
	TEST("rejects missing hash")
	{
		int result = parse_hex_color("FFFFFF", &r, &g, &b);
		ASSERT_EQ_INT(-1, result);
	}

	TEST("rejects too short")
	{
		int result = parse_hex_color("#FFF", &r, &g, &b);
		ASSERT_EQ_INT(-1, result);
	}

	TEST("rejects too long")
	{
		int result = parse_hex_color("#FFFFFFFF", &r, &g, &b);
		ASSERT_EQ_INT(-1, result);
	}

	TEST("rejects bad characters")
	{
		int result = parse_hex_color("#GGGGGG", &r, &g, &b);
		ASSERT_EQ_INT(-1, result);
	}

	TEST("rejects empty string")
	{
		int result = parse_hex_color("", &r, &g, &b);
		ASSERT_EQ_INT(-1, result);
	}

	TEST_SUITE_END();
}

void
test_trim(void)
{
	TEST_SUITE_BEGIN("trim");

	TEST("trims leading whitespace")
	{
		char str[] = "   hello";
		char *result = trim(str);
		ASSERT_EQ_STR("hello", result);
	}

	TEST("trims trailing whitespace")
	{
		char str[] = "hello   ";
		char *result = trim(str);
		ASSERT_EQ_STR("hello", result);
	}

	TEST("trims both sides")
	{
		char str[] = "   hello   ";
		char *result = trim(str);
		ASSERT_EQ_STR("hello", result);
	}

	TEST("trims tabs and newlines")
	{
		char str[] = "\t\n hello \t\n";
		char *result = trim(str);
		ASSERT_EQ_STR("hello", result);
	}

	TEST("handles empty string")
	{
		char str[] = "";
		char *result = trim(str);
		ASSERT_EQ_STR("", result);
	}

	TEST("handles all whitespace")
	{
		char str[] = "   \t\n  ";
		char *result = trim(str);
		ASSERT_EQ_STR("", result);
	}

	TEST("preserves string without whitespace")
	{
		char str[] = "hello";
		char *result = trim(str);
		ASSERT_EQ_STR("hello", result);
	}

	TEST_SUITE_END();
}

void
test_parse_bool(void)
{
	TEST_SUITE_BEGIN("parse_bool");

	TEST("parses 'true' as true")
	{
		ASSERT_TRUE(parse_bool("true"));
	}

	TEST("parses '1' as true")
	{
		ASSERT_TRUE(parse_bool("1"));
	}

	TEST("parses 'false' as false")
	{
		ASSERT_FALSE(parse_bool("false"));
	}

	TEST("parses '0' as false")
	{
		ASSERT_FALSE(parse_bool("0"));
	}

	TEST("parses empty as false")
	{
		ASSERT_FALSE(parse_bool(""));
	}

	TEST("parses 'yes' as false (not recognized)")
	{
		/* Function only recognizes "true" and "1" */
		ASSERT_FALSE(parse_bool("yes"));
	}

	TEST_SUITE_END();
}

void
test_parse_int_clamped(void)
{
	TEST_SUITE_BEGIN("parse_int_clamped");

	TEST("parses normal value")
	{
		int result = parse_int_clamped("50", 0, 100);
		ASSERT_EQ_INT(50, result);
	}

	TEST("value at min")
	{
		int result = parse_int_clamped("10", 10, 100);
		ASSERT_EQ_INT(10, result);
	}

	TEST("value at max")
	{
		int result = parse_int_clamped("100", 10, 100);
		ASSERT_EQ_INT(100, result);
	}

	TEST("clamps below min")
	{
		int result = parse_int_clamped("5", 10, 100);
		ASSERT_EQ_INT(10, result);
	}

	TEST("clamps above max")
	{
		int result = parse_int_clamped("200", 10, 100);
		ASSERT_EQ_INT(100, result);
	}

	TEST("non-numeric defaults to 0 then clamps")
	{
		int result = parse_int_clamped("abc", 10, 100);
		/* atoi("abc") = 0, which is below min 10, so clamped to 10 */
		ASSERT_EQ_INT(10, result);
	}

	TEST_SUITE_END();
}
