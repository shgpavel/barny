#include "test_framework.h"
#include "barny.h"
#include <cairo/cairo.h>

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

		ASSERT_EQ_DBL(0.5, result, 0.0001);
	}

	TEST("perlin_fade is monotonic increasing")
	{
		double prev = perlin_fade_test(0.0);
		double t;
		double curr;

		for (t = 0.1; t <= 1.0; t += 0.1) {
			curr = perlin_fade_test(t);
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
		int    hash;
		double result;

		for (hash = 0; hash < 256; hash++) {
			result = perlin_grad_test(hash, 1.0, 1.0);

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
		double x;
		double y;
		double noise;

		for (x = -10.0; x <= 10.0; x += 0.37) {
			for (y = -10.0; y <= 10.0; y += 0.41) {
				noise = perlin_noise2d_test(x, y);
				ASSERT_IN_RANGE(noise, -1.0, 1.0);
			}
		}
	}

	TEST("noise varies spatially")
	{
		double n1 = perlin_noise2d_test(0.5, 0.5);
		double n2 = perlin_noise2d_test(5.5, 5.5);

		ASSERT_TRUE(fabs(n1 - n2) > 0.0001 || (n1 == 0 && n2 == 0));
	}

	TEST("noise is continuous")
	{
		double n1 = perlin_noise2d_test(1.0, 1.0);
		double n2 = perlin_noise2d_test(1.001, 1.001);

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
		double x;
		double y;
		double fbm;

		for (x = -5.0; x <= 5.0; x += 0.7) {
			for (y = -5.0; y <= 5.0; y += 0.7) {
				fbm = perlin_fbm_test(x, y, 4, 0.5);

				ASSERT_IN_RANGE(fbm, -1.0, 1.0);
			}
		}
	}

	TEST("more octaves adds detail")
	{
		double fbm1 = perlin_fbm_test(0.5, 0.5, 1, 0.5);
		double fbm4 = perlin_fbm_test(0.5, 0.5, 4, 0.5);

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
		cairo_surface_t *map;
		uint8_t         *data;
		int              stride;
		int              cx;
		int              cy;
		uint8_t         *pixel;
		int              r;
		int              g;

		map = barny_create_displacement_map(
		        100, 100, BARNY_REFRACT_LENS, 10, 1.0, 0.02, 2);
		ASSERT_NOT_NULL(map);

		cairo_surface_flush(map);
		data   = cairo_image_surface_get_data(map);
		stride = cairo_image_surface_get_stride(map);

		cx     = 50;
		cy     = 50;
		pixel  = data + cy * stride + cx * 4;

		r      = pixel[2];
		g      = pixel[1];

		ASSERT_IN_RANGE(r, 120, 136);
		ASSERT_IN_RANGE(g, 120, 136);

		cairo_surface_destroy(map);
	}

	TEST("lens mode edges have stronger displacement")
	{
		cairo_surface_t *map;
		uint8_t         *data;
		int              stride;
		uint8_t         *center;
		int              center_r;
		uint8_t         *edge;
		int              edge_r;
		int              center_disp;
		int              edge_disp;

		map = barny_create_displacement_map(
		        100, 100, BARNY_REFRACT_LENS, 10, 1.5, 0.02, 2);
		ASSERT_NOT_NULL(map);

		cairo_surface_flush(map);
		data        = cairo_image_surface_get_data(map);
		stride      = cairo_image_surface_get_stride(map);

		center      = data + 50 * stride + 50 * 4;
		center_r    = center[2];

		edge        = data + 50 * stride + 95 * 4;
		edge_r      = edge[2];

		center_disp = abs(center_r - 128);
		edge_disp   = abs(edge_r - 128);
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
		cairo_surface_t *surface;
		cairo_t         *cr;
		uint8_t         *data;
		uint8_t          before_r;
		uint8_t          after_r;

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cr      = cairo_create(surface);
		cairo_set_source_rgba(cr, 1, 0, 0, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		data     = cairo_image_surface_get_data(surface);
		before_r = data[2];

		barny_blur_surface(surface, 0);

		cairo_surface_flush(surface);
		data    = cairo_image_surface_get_data(surface);
		after_r = data[2];

		ASSERT_EQ_INT(before_r, after_r);
		cairo_surface_destroy(surface);
	}

	TEST("blur reduces sharp edges")
	{
		int              width;
		int              height;
		cairo_surface_t *surface;
		cairo_t         *cr;
		uint8_t         *data;
		int              stride;
		int              edge_x;
		uint8_t          after;

		width   = 100;
		height  = 10;
		surface = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, width, height);
		cr = cairo_create(surface);

		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_rectangle(cr, 0, 0, width / 2, height);
		cairo_fill(cr);

		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_rectangle(cr, width / 2, 0, width / 2, height);
		cairo_fill(cr);
		cairo_destroy(cr);

		barny_blur_surface(surface, 5);

		cairo_surface_flush(surface);
		data   = cairo_image_surface_get_data(surface);
		stride = cairo_image_surface_get_stride(surface);
		edge_x = width / 2;
		after  = data[5 * stride + edge_x * 4];

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
		cairo_surface_t *surface;
		cairo_t         *cr;
		uint8_t         *data;
		uint8_t          before;
		uint8_t          after;

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cr      = cairo_create(surface);
		cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		data   = cairo_image_surface_get_data(surface);
		before = data[0];

		barny_apply_brightness(surface, 1.0);

		cairo_surface_flush(surface);
		data  = cairo_image_surface_get_data(surface);
		after = data[0];

		ASSERT_EQ_INT(before, after);
		cairo_surface_destroy(surface);
	}

	TEST("brightness 2.0 doubles values")
	{
		cairo_surface_t *surface;
		cairo_t         *cr;
		uint8_t         *data;
		uint8_t          before;
		uint8_t          after;

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cr      = cairo_create(surface);
		cairo_set_source_rgba(cr, 0.25, 0.25, 0.25, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		data   = cairo_image_surface_get_data(surface);
		before = data[0];

		barny_apply_brightness(surface, 2.0);

		cairo_surface_flush(surface);
		data  = cairo_image_surface_get_data(surface);
		after = data[0];

		ASSERT_IN_RANGE(after, before * 1.8, before * 2.2 + 1);
		cairo_surface_destroy(surface);
	}

	TEST("brightness clamps at 255")
	{
		cairo_surface_t *surface;
		cairo_t         *cr;
		uint8_t         *data;

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cr      = cairo_create(surface);
		cairo_set_source_rgba(cr, 1, 1, 1, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		barny_apply_brightness(surface, 2.0);

		cairo_surface_flush(surface);
		data = cairo_image_surface_get_data(surface);

		ASSERT_EQ_INT(255, data[0]);
		ASSERT_EQ_INT(255, data[1]);
		ASSERT_EQ_INT(255, data[2]);
		cairo_surface_destroy(surface);
	}

	TEST("brightness 0.5 halves values")
	{
		cairo_surface_t *surface;
		cairo_t         *cr;
		uint8_t         *data;
		uint8_t          before;
		uint8_t          after;

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
		cr      = cairo_create(surface);
		cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		cairo_surface_flush(surface);
		data   = cairo_image_surface_get_data(surface);
		before = data[0];

		barny_apply_brightness(surface, 0.5);

		cairo_surface_flush(surface);
		data  = cairo_image_surface_get_data(surface);
		after = data[0];

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
		int              size;
		cairo_surface_t *src;
		cairo_t         *cr;
		cairo_pattern_t *grad;
		cairo_surface_t *disp;
		cairo_surface_t *dst;
		uint8_t         *src_data;
		uint8_t         *dst_data;
		int              stride;
		int              cx;
		int              cy;
		int              i;

		size = 50;
		src  = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);
		cr   = cairo_create(src);
		grad = cairo_pattern_create_linear(0, 0, size, 0);
		cairo_pattern_add_color_stop_rgba(grad, 0, 0, 0, 0, 1);
		cairo_pattern_add_color_stop_rgba(grad, 1, 1, 1, 1, 1);
		cairo_set_source(cr, grad);
		cairo_paint(cr);
		cairo_pattern_destroy(grad);
		cairo_destroy(cr);

		disp = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);
		cr = cairo_create(disp);

		cairo_set_source_rgba(
		        cr, 128.0 / 255, 128.0 / 255, 128.0 / 255, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		dst = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);

		barny_apply_displacement(src, dst, disp, 0, 0);

		cairo_surface_flush(src);
		cairo_surface_flush(dst);
		src_data = cairo_image_surface_get_data(src);
		dst_data = cairo_image_surface_get_data(dst);
		stride   = cairo_image_surface_get_stride(src);

		cx       = size / 2;
		cy       = size / 2;
		for (i = 0; i < 4; i++) {
			ASSERT_EQ_INT(src_data[cy * stride + cx * 4 + i],
			              dst_data[cy * stride + cx * 4 + i]);
		}

		cairo_surface_destroy(src);
		cairo_surface_destroy(dst);
		cairo_surface_destroy(disp);
	}

	TEST("displacement shifts pixels")
	{
		int              size;
		cairo_surface_t *src;
		cairo_t         *cr;
		cairo_surface_t *disp;
		cairo_surface_t *dst;

		size = 100;

		src  = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);
		cr = cairo_create(src);
		cairo_set_source_rgba(cr, 1, 0, 0, 1);
		cairo_paint(cr);

		cairo_set_source_rgba(cr, 0, 0, 1, 1);
		cairo_rectangle(cr, 0, 0, 20, 20);
		cairo_fill(cr);
		cairo_destroy(cr);

		disp = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);
		cr = cairo_create(disp);

		cairo_set_source_rgba(cr, 200.0 / 255, 128.0 / 255, 0, 1);
		cairo_paint(cr);
		cairo_destroy(cr);

		dst = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, size, size);

		barny_apply_displacement(src, dst, disp, 20.0, 0);

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
