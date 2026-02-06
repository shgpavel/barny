#include "test_framework.h"
#include "barny.h"
#include <cairo/cairo.h>

/*
 * For testing static functions, we include the source file directly.
 * This allows access to internal functions like perlin_* without
 * modifying the original source.
 */
#include "../src/render/liquid_glass.c"

#define perlin_fade_test    perlin_fade
#define perlin_lerp_test    perlin_lerp
#define perlin_grad_test    perlin_grad
#define perlin_noise2d_test perlin_noise2d
#define perlin_fbm_test     perlin_fbm

void
test_perlin_math(void)
{
	TEST_SUITE_BEGIN("Perlin Noise Math Functions");

	TEST("perlin_fade(0) = 0")
	{
		double result = perlin_fade_test(0.0);
		ASSERT_EQ_DBL(0.0, result, 0.0001);
	}

	TEST("perlin_fade(1) = 1")
	{
		double result = perlin_fade_test(1.0);
		ASSERT_EQ_DBL(1.0, result, 0.0001);
	}

	TEST("perlin_fade(0.5) is smooth midpoint")
	{
		double result = perlin_fade_test(0.5);
		/* 6t^5 - 15t^4 + 10t^3 at t=0.5 = 0.5 */
		ASSERT_EQ_DBL(0.5, result, 0.0001);
	}

	TEST("perlin_fade is monotonic increasing")
	{
		double prev = perlin_fade_test(0.0);
		for (double t = 0.1; t <= 1.0; t += 0.1) {
			double curr = perlin_fade_test(t);
			ASSERT_TRUE(curr >= prev);
			prev = curr;
		}
	}

	TEST("perlin_lerp basic interpolation")
	{
		ASSERT_EQ_DBL(0.0, perlin_lerp_test(0.0, 1.0, 0.0), 0.0001);
		ASSERT_EQ_DBL(1.0, perlin_lerp_test(0.0, 1.0, 1.0), 0.0001);
		ASSERT_EQ_DBL(0.5, perlin_lerp_test(0.0, 1.0, 0.5), 0.0001);
	}

	TEST("perlin_lerp with negative values")
	{
		ASSERT_EQ_DBL(-5.0, perlin_lerp_test(-10.0, 0.0, 0.5), 0.0001);
		ASSERT_EQ_DBL(0.0, perlin_lerp_test(-5.0, 5.0, 0.5), 0.0001);
	}

	TEST("perlin_grad returns bounded values")
	{
		/* Test several hash values */
		for (int hash = 0; hash < 256; hash++) {
			double result = perlin_grad_test(hash, 1.0, 1.0);
			/* Gradient should be bounded by the input magnitude */
			ASSERT_IN_RANGE(result, -2.0, 2.0);
		}
	}

	TEST_SUITE_END();
}

void
test_perlin_noise(void)
{
	TEST_SUITE_BEGIN("Perlin Noise 2D");

	TEST("noise at integer coordinates is zero")
	{
		/* At integer lattice points, Perlin noise should be 0 */
		double result = perlin_noise2d_test(0.0, 0.0);
		ASSERT_EQ_DBL(0.0, result, 0.0001);
	}

	TEST("noise is deterministic")
	{
		double r1 = perlin_noise2d_test(1.5, 2.5);
		double r2 = perlin_noise2d_test(1.5, 2.5);
		ASSERT_EQ_DBL(r1, r2, 0.0001);
	}

	TEST("noise is bounded [-1, 1]")
	{
		for (double x = -10.0; x <= 10.0; x += 0.37) {
			for (double y = -10.0; y <= 10.0; y += 0.41) {
				double noise = perlin_noise2d_test(x, y);
				ASSERT_IN_RANGE(noise, -1.0, 1.0);
			}
		}
	}

	TEST("noise varies spatially")
	{
		double n1 = perlin_noise2d_test(0.5, 0.5);
		double n2 = perlin_noise2d_test(5.5, 5.5);
		/* These should be different (extremely unlikely to be equal) */
		ASSERT_TRUE(fabs(n1 - n2) > 0.0001 || (n1 == 0 && n2 == 0));
	}

	TEST("noise is continuous")
	{
		double n1 = perlin_noise2d_test(1.0, 1.0);
		double n2 = perlin_noise2d_test(1.001, 1.001);
		/* Should be very close for nearby points */
		ASSERT_TRUE(fabs(n1 - n2) < 0.1);
	}

	TEST_SUITE_END();
}

void
test_perlin_fbm(void)
{
	TEST_SUITE_BEGIN("Fractional Brownian Motion (FBM)");

	TEST("fbm with 1 octave equals noise")
	{
		double noise = perlin_noise2d_test(2.5, 3.5);
		double fbm   = perlin_fbm_test(2.5, 3.5, 1, 0.5);
		ASSERT_EQ_DBL(noise, fbm, 0.0001);
	}

	TEST("fbm is deterministic")
	{
		double r1 = perlin_fbm_test(1.5, 2.5, 4, 0.5);
		double r2 = perlin_fbm_test(1.5, 2.5, 4, 0.5);
		ASSERT_EQ_DBL(r1, r2, 0.0001);
	}

	TEST("fbm is bounded")
	{
		for (double x = -5.0; x <= 5.0; x += 0.7) {
			for (double y = -5.0; y <= 5.0; y += 0.7) {
				double fbm = perlin_fbm_test(x, y, 4, 0.5);
				/* FBM is normalized, should be in [-1, 1] */
				ASSERT_IN_RANGE(fbm, -1.0, 1.0);
			}
		}
	}

	TEST("more octaves adds detail")
	{
		/* With more octaves, the variance should be similar
		 * but the signal contains more high-frequency content */
		double fbm1 = perlin_fbm_test(0.5, 0.5, 1, 0.5);
		double fbm4 = perlin_fbm_test(0.5, 0.5, 4, 0.5);
		/* They should generally be different due to additional octaves */
		/* (This is a weak test, mainly checking no crash) */
		ASSERT_IN_RANGE(fbm1, -1.0, 1.0);
		ASSERT_IN_RANGE(fbm4, -1.0, 1.0);
	}

	TEST_SUITE_END();
}

void
test_displacement_map(void)
{
	TEST_SUITE_BEGIN("Displacement Map Creation");

	TEST("creates valid surface for lens mode")
	{
		cairo_surface_t *map = barny_create_displacement_map(
		        100, 50, BARNY_REFRACT_LENS, 10, 1.0, 0.02, 2);
		ASSERT_NOT_NULL(map);
		ASSERT_EQ_INT(CAIRO_STATUS_SUCCESS, cairo_surface_status(map));
		ASSERT_EQ_INT(100, cairo_image_surface_get_width(map));
		ASSERT_EQ_INT(50, cairo_image_surface_get_height(map));
		cairo_surface_destroy(map);
	}

	TEST("creates valid surface for liquid mode")
	{
		cairo_surface_t *map = barny_create_displacement_map(
		        100, 50, BARNY_REFRACT_LIQUID, 10, 1.0, 0.02, 2);
		ASSERT_NOT_NULL(map);
		ASSERT_EQ_INT(CAIRO_STATUS_SUCCESS, cairo_surface_status(map));
		cairo_surface_destroy(map);
	}

	TEST("liquid mode handles zero border radius")
	{
		cairo_surface_t *map = barny_create_displacement_map(
		        64, 32, BARNY_REFRACT_LIQUID, 0, 1.0, 0.02, 2);
		ASSERT_NOT_NULL(map);
		ASSERT_EQ_INT(CAIRO_STATUS_SUCCESS, cairo_surface_status(map));
		cairo_surface_destroy(map);
	}

	TEST("lens mode center has neutral displacement")
	{
		cairo_surface_t *map = barny_create_displacement_map(
		        100, 100, BARNY_REFRACT_LENS, 10, 1.0, 0.02, 2);
		ASSERT_NOT_NULL(map);

		cairo_surface_flush(map);
		uint8_t *data   = cairo_image_surface_get_data(map);
		int      stride = cairo_image_surface_get_stride(map);

		/* Center pixel (50, 50) */
		int      cx = 50, cy = 50;
		uint8_t *pixel = data + cy * stride + cx * 4;
		/* R and G channels should be close to 128 (neutral) at center */
		int      r     = pixel[2];
		int      g     = pixel[1];
		/* Allow some tolerance since center isn't exactly at integer position */
		ASSERT_IN_RANGE(r, 120, 136);
		ASSERT_IN_RANGE(g, 120, 136);

		cairo_surface_destroy(map);
	}

	TEST("lens mode edges have stronger displacement")
	{
		cairo_surface_t *map = barny_create_displacement_map(
		        100, 100, BARNY_REFRACT_LENS, 10, 1.5, 0.02, 2);
		ASSERT_NOT_NULL(map);

		cairo_surface_flush(map);
		uint8_t *data        = cairo_image_surface_get_data(map);
		int      stride      = cairo_image_surface_get_stride(map);

		/* Center pixel */
		uint8_t *center      = data + 50 * stride + 50 * 4;
		int      center_r    = center[2];

		/* Edge pixel (near right edge) */
		uint8_t *edge        = data + 50 * stride + 95 * 4;
		int      edge_r      = edge[2];

		/* Edge should have larger displacement (further from 128) */
		int      center_disp = abs(center_r - 128);
		int      edge_disp   = abs(edge_r - 128);
		ASSERT_TRUE(edge_disp > center_disp);

		cairo_surface_destroy(map);
	}

	TEST_SUITE_END();
}

void
test_blur_surface(void)
{
	TEST_SUITE_BEGIN("Surface Blur");

	TEST("blur with radius 0 is no-op")
	{
		/* Create a simple test surface */
		cairo_surface_t *surface
		        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cairo_t *cr = cairo_create(surface);
		cairo_set_source_rgba(cr, 1, 0, 0, 1); /* Red */
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		uint8_t *data     = cairo_image_surface_get_data(surface);
		uint8_t  before_r = data[2];

		barny_blur_surface(surface, 0);

		cairo_surface_flush(surface);
		data            = cairo_image_surface_get_data(surface);
		uint8_t after_r = data[2];

		ASSERT_EQ_INT(before_r, after_r);
		cairo_surface_destroy(surface);
	}

	TEST("blur reduces sharp edges")
	{
		/* Create surface with sharp edge (half white, half black) */
		int              width = 100, height = 10;
		cairo_surface_t *surface = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, width, height);
		cairo_t *cr = cairo_create(surface);

		/* Left half black */
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_rectangle(cr, 0, 0, width / 2, height);
		cairo_fill(cr);

		/* Right half white */
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_rectangle(cr, width / 2, 0, width / 2, height);
		cairo_fill(cr);
		cairo_destroy(cr);

		/* Apply blur */
		barny_blur_surface(surface, 5);

		/* Get value at edge after blur */
		cairo_surface_flush(surface);
		uint8_t *data   = cairo_image_surface_get_data(surface);
		int      stride = cairo_image_surface_get_stride(surface);
		int      edge_x = width / 2;
		uint8_t  after  = data[5 * stride + edge_x * 4];

		/* After blur, the edge should have intermediate values */
		/* Before was 0 (black) or 255 (white), After should be in between */
		ASSERT_TRUE(after > 10 && after < 245);
		cairo_surface_destroy(surface);
	}

	TEST("blur preserves surface dimensions")
	{
		cairo_surface_t *surface
		        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 50, 30);
		barny_blur_surface(surface, 3);
		ASSERT_EQ_INT(50, cairo_image_surface_get_width(surface));
		ASSERT_EQ_INT(30, cairo_image_surface_get_height(surface));
		cairo_surface_destroy(surface);
	}

	TEST_SUITE_END();
}

void
test_brightness(void)
{
	TEST_SUITE_BEGIN("Surface Brightness");

	TEST("brightness 1.0 is no-op")
	{
		cairo_surface_t *surface
		        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cairo_t *cr = cairo_create(surface);
		cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		uint8_t *data   = cairo_image_surface_get_data(surface);
		uint8_t  before = data[0];

		barny_apply_brightness(surface, 1.0);

		cairo_surface_flush(surface);
		data          = cairo_image_surface_get_data(surface);
		uint8_t after = data[0];

		ASSERT_EQ_INT(before, after);
		cairo_surface_destroy(surface);
	}

	TEST("brightness 2.0 doubles values")
	{
		cairo_surface_t *surface
		        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cairo_t *cr = cairo_create(surface);
		cairo_set_source_rgba(cr, 0.25, 0.25, 0.25, 1); /* ~64 in 0-255 */
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		uint8_t *data   = cairo_image_surface_get_data(surface);
		uint8_t  before = data[0];

		barny_apply_brightness(surface, 2.0);

		cairo_surface_flush(surface);
		data          = cairo_image_surface_get_data(surface);
		uint8_t after = data[0];

		/* Should be approximately doubled */
		ASSERT_IN_RANGE(after, before * 1.8, before * 2.2 + 1);
		cairo_surface_destroy(surface);
	}

	TEST("brightness clamps at 255")
	{
		cairo_surface_t *surface
		        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cairo_t *cr = cairo_create(surface);
		cairo_set_source_rgba(cr, 1, 1, 1, 1); /* White = 255 */
		cairo_paint(cr);
		cairo_destroy(cr);

		barny_apply_brightness(surface, 2.0);

		cairo_surface_flush(surface);
		uint8_t *data = cairo_image_surface_get_data(surface);
		/* Should be clamped at 255 */
		ASSERT_EQ_INT(255, data[0]);
		ASSERT_EQ_INT(255, data[1]);
		ASSERT_EQ_INT(255, data[2]);
		cairo_surface_destroy(surface);
	}

	TEST("brightness 0.5 halves values")
	{
		cairo_surface_t *surface
		        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cairo_t *cr = cairo_create(surface);
		cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1); /* ~204 in 0-255 */
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		uint8_t *data   = cairo_image_surface_get_data(surface);
		uint8_t  before = data[0];

		barny_apply_brightness(surface, 0.5);

		cairo_surface_flush(surface);
		data          = cairo_image_surface_get_data(surface);
		uint8_t after = data[0];

		/* Should be approximately halved */
		ASSERT_IN_RANGE(after, before * 0.4, before * 0.6);
		cairo_surface_destroy(surface);
	}

	TEST_SUITE_END();
}

void
test_apply_displacement(void)
{
	TEST_SUITE_BEGIN("Displacement Application");

	TEST("neutral displacement preserves image")
	{
		/* Create source with gradient */
		int              size = 50;
		cairo_surface_t *src  = cairo_image_surface_create(
                        CAIRO_FORMAT_ARGB32, size, size);
		cairo_t         *cr   = cairo_create(src);
		cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, size, 0);
		cairo_pattern_add_color_stop_rgba(grad, 0, 0, 0, 0, 1);
		cairo_pattern_add_color_stop_rgba(grad, 1, 1, 1, 1, 1);
		cairo_set_source(cr, grad);
		cairo_paint(cr);
		cairo_pattern_destroy(grad);
		cairo_destroy(cr);

		/* Create neutral displacement map (all 128) */
		cairo_surface_t *disp = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);
		cr = cairo_create(disp);
		/* RGB(128, 128, 128) = neutral displacement */
		cairo_set_source_rgba(
		        cr, 128.0 / 255, 128.0 / 255, 128.0 / 255, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		/* Create destination */
		cairo_surface_t *dst = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);

		/* Apply with scale 0 (no displacement) */
		barny_apply_displacement(src, dst, disp, 0, 0);

		/* Check center pixel matches */
		cairo_surface_flush(src);
		cairo_surface_flush(dst);
		uint8_t *src_data = cairo_image_surface_get_data(src);
		uint8_t *dst_data = cairo_image_surface_get_data(dst);
		int      stride   = cairo_image_surface_get_stride(src);

		int      cx       = size / 2;
		int      cy       = size / 2;
		for (int i = 0; i < 4; i++) {
			ASSERT_EQ_INT(src_data[cy * stride + cx * 4 + i],
			              dst_data[cy * stride + cx * 4 + i]);
		}

		cairo_surface_destroy(src);
		cairo_surface_destroy(dst);
		cairo_surface_destroy(disp);
	}

	TEST("displacement shifts pixels")
	{
		int              size = 100;
		/* Create red source */
		cairo_surface_t *src  = cairo_image_surface_create(
                        CAIRO_FORMAT_ARGB32, size, size);
		cairo_t *cr = cairo_create(src);
		cairo_set_source_rgba(cr, 1, 0, 0, 1);
		cairo_paint(cr);
		/* Add blue square in corner */
		cairo_set_source_rgba(cr, 0, 0, 1, 1);
		cairo_rectangle(cr, 0, 0, 20, 20);
		cairo_fill(cr);
		cairo_destroy(cr);

		/* Create displacement that shifts right (R > 128) */
		cairo_surface_t *disp = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);
		cr = cairo_create(disp);
		/* R=200 means shift sample point left (so output appears shifted right) */
		cairo_set_source_rgba(cr, 200.0 / 255, 128.0 / 255, 0, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_t *dst = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);

		barny_apply_displacement(src, dst, disp, 20.0, 0);

		/* The blue corner should appear shifted */
		/* Just verify the operation completes without error */
		ASSERT_EQ_INT(CAIRO_STATUS_SUCCESS, cairo_surface_status(dst));

		cairo_surface_destroy(src);
		cairo_surface_destroy(dst);
		cairo_surface_destroy(disp);
	}

	TEST_SUITE_END();
}

void
test_file_extension(void)
{
	TEST_SUITE_BEGIN("File Extension Detection");

	TEST("detects .jpg extension")
	{
		ASSERT_TRUE(has_extension("image.jpg", ".jpg"));
	}

	TEST("detects .jpeg extension")
	{
		ASSERT_TRUE(has_extension("image.jpeg", ".jpeg"));
	}

	TEST("detects .png extension")
	{
		ASSERT_TRUE(has_extension("image.png", ".png"));
	}

	TEST("case insensitive - JPG")
	{
		ASSERT_TRUE(has_extension("IMAGE.JPG", ".jpg"));
	}

	TEST("case insensitive - JpG")
	{
		ASSERT_TRUE(has_extension("image.JpG", ".jpg"));
	}

	TEST("rejects wrong extension")
	{
		ASSERT_FALSE(has_extension("image.png", ".jpg"));
	}

	TEST("handles path with directories")
	{
		ASSERT_TRUE(has_extension("/home/user/photos/image.jpg", ".jpg"));
	}

	TEST("handles short filename")
	{
		ASSERT_TRUE(has_extension("a.jpg", ".jpg"));
	}

	TEST("rejects if filename too short")
	{
		ASSERT_FALSE(has_extension(".jpg", ".jpeg"));
	}

	TEST_SUITE_END();
}
