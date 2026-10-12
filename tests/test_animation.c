#include "test_framework.h"
#include "barny.h"
#include "util.h"
#include "../src/modules/popup.h"

#include <string.h>

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

static barny_state_t  lens_state;
static barny_output_t lens_out;

static void
lens_setup(double from_x, double pointer_x, double scale, double target)
{
	memset(&lens_state, 0, sizeof(lens_state));
	memset(&lens_out, 0, sizeof(lens_out));
	lens_out.state                = &lens_state;
	lens_out.pad_left             = 10;
	lens_out.width                = 1000;
	lens_state.pointer_output     = &lens_out;
	lens_state.pointer_x          = pointer_x;
	lens_state.lens_x             = from_x;
	lens_state.lens_scale         = scale;
	lens_state.lens_target_scale  = target;
	lens_state.lens_animating     = true;
}

void
test_lens_advance(void)
{
	TEST_SUITE_BEGIN("Bar Droplet Stepper");

	TEST("droplet converges, snaps and stays in its travel range")
	{
		double bmin = 10 + 172.0 / 2.0;
		double bmax = 10 + 1000 - 172.0 / 2.0;
		int    in_range = 1;
		int    in_unit  = 1;
		int    i;

		lens_setup(200.0, 900.0, 0.0, 1.0);
		for (i = 0; i < 300 && barny_lens_advance(&lens_out, 0.01); i++) {
			in_range &= lens_state.lens_x >= bmin
			            && lens_state.lens_x <= bmax;
			in_unit &= lens_state.lens_scale >= 0.0
			           && lens_state.lens_scale <= 1.0;
		}
		ASSERT_TRUE(i < 300);
		ASSERT_TRUE(in_range);
		ASSERT_TRUE(in_unit);
		ASSERT_FALSE(lens_state.lens_animating);
		ASSERT_EQ_DBL(900.0, lens_state.lens_x, 0.0);
		ASSERT_EQ_DBL(0.0, lens_state.lens_vx, 0.0);
		ASSERT_EQ_DBL(1.0, lens_state.lens_scale, 0.0);
	}

	TEST("pointer past the edge parks the droplet at the end of travel")
	{
		int i;

		lens_setup(500.0, 5000.0, 1.0, 1.0);
		for (i = 0; i < 300 && barny_lens_advance(&lens_out, 0.01); i++)
			;
		ASSERT_TRUE(i < 300);
		ASSERT_EQ_DBL(10 + 1000 - 172.0 / 2.0, lens_state.lens_x, 0.0);
	}

	TEST("leaving the bar dissolves the droplet in place")
	{
		int in_unit = 1;
		int i;

		lens_setup(400.0, 0.0, 1.0, 0.0);
		lens_state.pointer_output = NULL;
		for (i = 0; i < 300 && barny_lens_advance(&lens_out, 0.01); i++)
			in_unit &= lens_state.lens_scale >= 0.0
			           && lens_state.lens_scale <= 1.0;
		ASSERT_TRUE(i < 300);
		ASSERT_TRUE(in_unit);
		ASSERT_EQ_DBL(400.0, lens_state.lens_x, 0.0);
		ASSERT_EQ_DBL(0.0, lens_state.lens_scale, 0.0);
	}

	TEST("zero elapsed time does not move the droplet")
	{
		lens_setup(200.0, 900.0, 0.5, 1.0);
		ASSERT_TRUE(barny_lens_advance(&lens_out, 0.0));
		ASSERT_EQ_DBL(200.0, lens_state.lens_x, 0.0);
		ASSERT_EQ_DBL(0.0, lens_state.lens_vx, 0.0);
		ASSERT_EQ_DBL(0.5, lens_state.lens_scale, 0.0);
	}

	TEST_SUITE_END();
}

void
test_glass_panel_advance(void)
{
	TEST_SUITE_BEGIN("Glass Panel Morph Stepper");

	TEST("opening settles exactly at 1.0")
	{
		double m = 0.0;
		double v = 0.0;
		int    i;

		for (i = 0; i < 300 && !barny_glass_panel_advance(&m, &v, false,
		                                                  0.01);
		     i++)
			;
		ASSERT_TRUE(i < 300);
		ASSERT_EQ_DBL(1.0, m, 0.0);
		ASSERT_EQ_DBL(0.0, v, 0.0);
	}

	TEST("closing settles exactly at 0.0")
	{
		double m = 1.0;
		double v = 0.0;
		int    i;

		for (i = 0; i < 300 && !barny_glass_panel_advance(&m, &v, true,
		                                                  0.01);
		     i++)
			;
		ASSERT_TRUE(i < 300);
		ASSERT_EQ_DBL(0.0, m, 0.0);
		ASSERT_EQ_DBL(0.0, v, 0.0);
	}

	TEST("zero elapsed time does not move the panel")
	{
		double m = 0.25;
		double v = 1.5;

		ASSERT_FALSE(barny_glass_panel_advance(&m, &v, false, 0.0));
		ASSERT_EQ_DBL(0.25, m, 0.0);
		ASSERT_EQ_DBL(1.5, v, 0.0);
	}

	TEST_SUITE_END();
}
