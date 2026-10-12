#include "test_framework.h"
#include "util.h"

void
test_spring_step(void)
{
	TEST_SUITE_BEGIN("Damped Spring Animation");

	TEST("motion is independent of frame partitioning")
	{
		double one_x = 0.0;
		double one_v = 0.0;
		double many_x = 0.0;
		double many_v = 0.0;
		int    i;

		barny_spring_step(&one_x, &one_v, 100.0, 500.0, 0.8, 0.375);
		for (i = 0; i < 45; i++)
			barny_spring_step(&many_x, &many_v, 100.0, 500.0, 0.8,
			                  0.375 / 45.0);

		ASSERT_EQ_DBL(one_x, many_x, 1e-9);
		ASSERT_EQ_DBL(one_v, many_v, 1e-9);
	}

	TEST("critical damping approaches without overshoot")
	{
		double x = 0.0;
		double v = 0.0;
		double previous = x;
		int    i;

		for (i = 0; i < 120; i++) {
			barny_spring_step(&x, &v, 1.0, 900.0, 1.0, 1.0 / 120.0);
			ASSERT_TRUE(x >= previous);
			ASSERT_TRUE(x <= 1.0);
			previous = x;
		}
		ASSERT_EQ_DBL(1.0, x, 1e-9);
		ASSERT_EQ_DBL(0.0, v, 1e-9);
	}

	TEST("underdamped motion keeps a restrained overshoot")
	{
		double x = 0.0;
		double v = 0.0;
		double peak = 0.0;
		int    i;

		for (i = 0; i < 120; i++) {
			barny_spring_step(&x, &v, 1.0, 150.0, 0.86, 1.0 / 120.0);
			if (x > peak)
				peak = x;
		}
		ASSERT_TRUE(peak > 1.0);
		ASSERT_TRUE(peak < 1.02);
		ASSERT_EQ_DBL(1.0, x, 0.0001);
	}

	TEST("zero elapsed time preserves spring state")
	{
		double x = 4.0;
		double v = -2.0;

		barny_spring_step(&x, &v, 10.0, 100.0, 0.8, 0.0);
		ASSERT_EQ_DBL(4.0, x, 0.0);
		ASSERT_EQ_DBL(-2.0, v, 0.0);
	}

	TEST_SUITE_END();
}
