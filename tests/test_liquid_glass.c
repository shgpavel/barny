#include "test_framework.h"
#include "barny.h"
#include <cairo/cairo.h>

#include "../src/render/liquid_glass.c"
#include "../src/modules/popup.h"

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

#define LENS_BAR_W  800
#define LENS_BAR_H  47
#define LENS_PAD_L  8
#define LENS_PAD_R  8
#define LENS_PAD_T  4
#define LENS_PAD_B  8

static void
lens_setup(barny_state_t *state, barny_output_t *out)
{
	memset(state, 0, sizeof(*state));
	memset(out, 0, sizeof(*out));

	state->config.dynamic_glass = true;
	state->config.glass_gleam   = 0.24;
	state->config.glass_bulge   = 15.0;
	state->config.glass_prism   = 0.8;
	state->config.border_radius = 28;
	state->config.position_top  = false;

	out->state                  = state;
	out->width                  = LENS_BAR_W;
	out->height                 = LENS_BAR_H;
	out->pad_left               = LENS_PAD_L;
	out->pad_right              = LENS_PAD_R;
	out->pad_top                = LENS_PAD_T;
	out->pad_bottom             = LENS_PAD_B;
	out->surf_width             = LENS_PAD_L + LENS_BAR_W + LENS_PAD_R;
	out->surf_height            = LENS_PAD_T + LENS_BAR_H + LENS_PAD_B;
	out->scale120               = BARNY_SCALE_UNIT;

	state->dyn_output           = out;
	state->lens_scale           = 1.0;
	state->lens_vx              = 0.0;
}

static cairo_surface_t *
lens_target(const barny_output_t *out)
{
	return cairo_image_surface_create(CAIRO_FORMAT_ARGB32, out->surf_width,
	                                  out->surf_height);
}

static void
lens_draw(barny_output_t *out, cairo_surface_t *surf, double lens_x,
          const barny_rect_t *clip)
{
	cairo_t *cr = cairo_create(surf);

	out->state->lens_x = lens_x;

	if (clip) {
		cairo_rectangle(cr, clip->x, clip->y, clip->w, clip->h);
		cairo_clip(cr);
	}
	barny_render_liquid_glass(out, cr);
	cairo_destroy(cr);
}

static int
lens_diff(cairo_surface_t *a, cairo_surface_t *b)
{
	int      w      = cairo_image_surface_get_width(a);
	int      h      = cairo_image_surface_get_height(a);
	int      stride = cairo_image_surface_get_stride(a);
	uint8_t *da;
	uint8_t *db;
	int      x;
	int      y;
	int      n = 0;

	cairo_surface_flush(a);
	cairo_surface_flush(b);
	da = cairo_image_surface_get_data(a);
	db = cairo_image_surface_get_data(b);

	for (y = 0; y < h; y++) {
		for (x = 0; x < w * 4; x++) {
			if (da[y * stride + x] != db[y * stride + x])
				n++;
		}
	}

	return n;
}

void
test_lens_opaque_fast_path(void)
{
	barny_state_t    state;
	barny_output_t   out;
	cairo_surface_t *wallpaper;
	cairo_surface_t *fast;
	cairo_surface_t *generic;
	cairo_t         *cr;

	TEST_SUITE_BEGIN("Opaque Lens Fast Path");

	lens_setup(&state, &out);
	wallpaper = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                       LENS_BAR_W, LENS_BAR_H);
	cr = cairo_create(wallpaper);
	cairo_set_source_rgb(cr, 0.1, 0.2, 0.3);
	cairo_paint(cr);
	cairo_destroy(cr);
	state.blurred_wallpaper = wallpaper;

	fast    = lens_target(&out);
	generic = lens_target(&out);
	lens_draw(&out, fast, 300.0, NULL);

	TEST("opaque wallpaper produces an opaque glass cache")
	{
		ASSERT_TRUE(out.glass_clean_opaque);
	}

	out.glass_clean_opaque = false;
	lens_draw(&out, generic, 300.0, NULL);

	TEST("opaque and generic sampling match byte for byte")
	{
		ASSERT_EQ_INT(0, lens_diff(fast, generic));
	}

	cairo_surface_destroy(fast);
	cairo_surface_destroy(generic);
	cairo_surface_destroy(wallpaper);
	barny_output_free_glass_caches(&out, false);

	TEST_SUITE_END();
}

void
test_lens_channel_tap(void)
{
	uint8_t data[4 * 4 * 4];
	double  coords[][2] = {
		{ -2.0, -1.0 }, { 0.25, 0.75 }, { 1.5, 2.25 }, { 4.0, 4.0 }
	};
	bool    equal = true;
	size_t  i;
	int     channel;

	TEST_SUITE_BEGIN("Lens Channel Sampling");

	for (i = 0; i < sizeof(data); i++)
		data[i] = (uint8_t)(i * 37U + 11U);

	for (i = 0; i < sizeof(coords) / sizeof(coords[0]); i++) {
		float full[4];

		lens_tap(data, 4 * 4, 4, 4, coords[i][0], coords[i][1], full);
		for (channel = 0; channel < 3; channel++) {
			float color;
			float alpha;
			float opaque_color;

			lens_tap_channel_alpha(data, 4 * 4, 4, 4,
			                       coords[i][0], coords[i][1], channel,
			                       &color, &alpha);
			lens_tap_channel_alpha(data, 4 * 4, 4, 4,
			                       coords[i][0], coords[i][1], channel,
			                       &opaque_color, NULL);
			if (color != full[channel] || alpha != full[3]
			    || opaque_color != full[channel])
				equal = false;
		}
	}

	TEST("specialized taps match full RGBA interpolation")
	{
		ASSERT_TRUE(equal);
	}

	TEST("opaque glass caches are detected once")
	{
		cairo_surface_t *surface;
		cairo_t         *cr;

		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 2, 2);
		cr      = cairo_create(surface);
		cairo_set_source_rgb(cr, 0.1, 0.2, 0.3);
		cairo_paint(cr);
		cairo_destroy(cr);
		ASSERT_TRUE(surface_is_opaque(surface));

		cr = cairo_create(surface);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cr);
		cairo_destroy(cr);
		ASSERT_FALSE(surface_is_opaque(surface));
		cairo_surface_destroy(surface);
	}

	TEST_SUITE_END();
}

#define PRISM_TEST_BAR_W 2544
#define PRISM_TEST_PAD   8

void
test_prism_stops(void)
{
	double  old_r[PRISM_STOPS + 1];
	double  old_g[PRISM_STOPS + 1];
	double  old_b[PRISM_STOPS + 1];
	double  cx         = PRISM_TEST_PAD;
	double  cw         = PRISM_TEST_BAR_W;
	int     pw         = PRISM_TEST_BAR_W + 2 * PRISM_TEST_PAD;
	bool    stops_same = true;
	bool    row_same   = true;
	double *prm;
	int     i;
	int     x;

	TEST_SUITE_BEGIN("Prism Rainbow");

	for (i = 0; i <= PRISM_STOPS; i++) {
		double hue = fmod((double)i / PRISM_STOPS * PRISM_CYCLES, 1.0);

		hsv2rgb(hue, PRISM_SAT, PRISM_VAL, &old_r[i], &old_g[i],
		        &old_b[i]);
	}

	prism_stops_init();
	for (i = 0; i <= PRISM_STOPS; i++) {
		const double *c = prism_stop[i % PRISM_HUES];

		if (c[0] != old_r[i] || c[1] != old_g[i] || c[2] != old_b[i])
			stops_same = false;
	}

	TEST("one hue cycle reproduces all 41 ramp stops bit for bit")
	{
		ASSERT_TRUE(stops_same);
	}

	prm = calloc((size_t)pw * 3, sizeof(*prm));

	TEST("prism_row matches the 41-stop ramp on every column of a bar")
	{
		ASSERT_TRUE(prm != NULL);
		prism_row(prm, pw, 0, cx, cw);
		for (x = 0; x < pw; x++) {
			double pos = ((double)x + 0.5 - cx) / cw;
			double f;
			double r;
			double g;
			double b;
			int    si;

			if (pos < 0.0)
				pos = 0.0;
			if (pos > 1.0)
				pos = 1.0;
			f  = pos * PRISM_STOPS;
			si = (int)f;
			if (si >= PRISM_STOPS)
				si = PRISM_STOPS - 1;
			f -= si;
			r  = old_r[si] + (old_r[si + 1] - old_r[si]) * f;
			g  = old_g[si] + (old_g[si + 1] - old_g[si]) * f;
			b  = old_b[si] + (old_b[si + 1] - old_b[si]) * f;
			if (prm[x * 3 + 0] != r || prm[x * 3 + 1] != g
			    || prm[x * 3 + 2] != b)
				row_same = false;
		}
		ASSERT_TRUE(row_same);
	}

	free(prm);

	TEST_SUITE_END();
}

void
test_damage_rects(void)
{
	barny_rect_t none = { 0 };
	barny_rect_t a    = { 10, 4, 20, 30 };
	barny_rect_t b    = { 50, -6, 8, 12 };
	barny_rect_t r;

	TEST_SUITE_BEGIN("Damage Rects");

	TEST("an empty rect adds nothing to a union")
	{
		r = barny_rect_union(a, none);
		ASSERT_TRUE(r.x == 10 && r.y == 4 && r.w == 20 && r.h == 30);
		r = barny_rect_union(none, a);
		ASSERT_TRUE(r.x == 10 && r.y == 4 && r.w == 20 && r.h == 30);
		ASSERT_TRUE(barny_rect_empty(barny_rect_union(none, none)));
	}

	TEST("a union is the bounding box of both rects")
	{
		r = barny_rect_union(a, b);
		ASSERT_TRUE(r.x == 10 && r.y == -6 && r.w == 48 && r.h == 40);
	}

	TEST("a clamp trims the rect to the surface")
	{
		r = barny_rect_clamp(barny_rect_union(a, b), 40, 20);
		ASSERT_TRUE(r.x == 10 && r.y == 0 && r.w == 30 && r.h == 20);
	}

	TEST("a rect off the surface clamps to empty")
	{
		ASSERT_TRUE(barny_rect_empty(barny_rect_clamp(b, 40, 20)));
		ASSERT_TRUE(barny_rect_empty(barny_rect_clamp(none, 40, 20)));
	}

	TEST_SUITE_END();
}

/* The frame loop redraws only the strip the droplet touches (the union of its
   rect this frame and last), trusting the rest of the persistent buffer. That
   is only sound if a strip-clipped render lands exactly the pixels a full
   render would. */
void
test_lens_partial_redraw(void)
{
	barny_state_t   state;
	barny_output_t  out;
	cairo_surface_t *full;
	cairo_surface_t *part;
	double           xa   = 300.0;
	double           xb   = 360.0;
	barny_rect_t     a    = { 0 };
	barny_rect_t     b    = { 0 };
	barny_rect_t     clip = { 0 };

	TEST_SUITE_BEGIN("Lens Partial Redraw");

	lens_setup(&state, &out);

	TEST("droplet strip is far narrower than the bar")
	{
		state.lens_x = xa;
		ASSERT_TRUE(barny_lens_rect(&out, &a.x, &a.y, &a.w, &a.h));
		state.lens_x = xb;
		ASSERT_TRUE(barny_lens_rect(&out, &b.x, &b.y, &b.w, &b.h));

		clip = barny_rect_union(a, b);
		ASSERT_TRUE(clip.w > 0 && clip.w < out.surf_width / 2);
	}

	full = lens_target(&out);
	part = lens_target(&out);

	/* reference: the whole bar drawn with the droplet at B */
	lens_draw(&out, full, xb, NULL);

	/* what the frame loop does: a full bar at A, then only the strip
	   redrawn with the droplet now at B */
	lens_draw(&out, part, xa, NULL);
	lens_draw(&out, part, xb, &clip);

	TEST("strip redraw matches a full redraw byte for byte")
	{
		ASSERT_EQ_INT(0, lens_diff(full, part));
	}

	TEST("the droplet actually moved (guards against a no-op test)")
	{
		cairo_surface_t *at_a = lens_target(&out);

		lens_draw(&out, at_a, xa, NULL);
		ASSERT_TRUE(lens_diff(full, at_a) > 0);
		cairo_surface_destroy(at_a);
	}

	cairo_surface_destroy(full);
	cairo_surface_destroy(part);
	barny_output_free_glass_caches(&out, false);

	TEST_SUITE_END();
}

void
test_lens_map_lifetime(void)
{
	barny_state_t    state;
	barny_output_t   out;
	cairo_surface_t *first;
	cairo_surface_t *again;
	cairo_surface_t *wide;
	cairo_surface_t *map;

	TEST_SUITE_BEGIN("Bar Lens Map Lifetime");

	lens_setup(&state, &out);
	first = lens_target(&out);
	again = lens_target(&out);
	lens_draw(&out, first, 300.0, NULL);
	map = cairo_surface_reference(out.lens_map);

	barny_output_free_glass_caches(&out, true);

	TEST("a buffer rebuild drops the baked bar but keeps the lens map")
	{
		ASSERT_NOT_NULL(map);
		ASSERT_TRUE(out.lens_map == map);
		ASSERT_NULL(out.bg_cache);
		ASSERT_NULL(out.shadow_cache);
		ASSERT_NULL(out.glass_clean);
		ASSERT_FALSE(out.glass_clean_opaque);
	}

	lens_draw(&out, again, 300.0, NULL);

	TEST("a same-size rebuild reuses the map and bakes the same bar")
	{
		ASSERT_TRUE(out.lens_map == map);
		ASSERT_EQ_INT(0, lens_diff(first, again));
	}

	out.width      += 40;
	out.surf_width += 40;
	barny_output_free_glass_caches(&out, true);
	wide = lens_target(&out);
	lens_draw(&out, wide, 300.0, NULL);

	TEST("a resized bar gets a lens map of the new size")
	{
		ASSERT_NOT_NULL(out.lens_map);
		ASSERT_TRUE(out.lens_map != map);
		ASSERT_EQ_INT(LENS_BAR_W + 40,
		              cairo_image_surface_get_width(out.lens_map));
		ASSERT_EQ_INT(LENS_BAR_H,
		              cairo_image_surface_get_height(out.lens_map));
	}

	barny_output_free_glass_caches(&out, false);

	TEST("a full teardown frees the lens map as well")
	{
		ASSERT_NULL(out.lens_map);
		ASSERT_NULL(out.bg_cache);
		ASSERT_NULL(out.lens_patch);
	}

	cairo_surface_destroy(map);
	cairo_surface_destroy(first);
	cairo_surface_destroy(again);
	cairo_surface_destroy(wide);

	TEST_SUITE_END();
}

static cairo_surface_t *
rim_target(const barny_output_t *out)
{
	return cairo_image_surface_create(
	        CAIRO_FORMAT_ARGB32, barny_scale_len(out->surf_width, out->scale120),
	        barny_scale_len(out->surf_height, out->scale120));
}

static void
rim_draw(barny_output_t *out, cairo_surface_t *surf, double lens_x,
         uint64_t tick, const barny_rect_t *clip, int n)
{
	cairo_t *cr = cairo_create(surf);
	double   s  = (double)out->scale120 / BARNY_SCALE_UNIT;

	out->rim_tick      = tick;
	out->state->lens_x = lens_x;

	if (out->scale120 != BARNY_SCALE_UNIT)
		cairo_scale(cr, s, s);
	if (n > 0)
		barny_clip_buffer_rects(cr, clip, n, out->scale120);
	barny_render_liquid_glass(out, cr);
	cairo_destroy(cr);
}

static void
rim_setup(barny_state_t *state, barny_output_t *out, uint32_t scale120)
{
	lens_setup(state, out);
	state->config.glass_prism_flow = 12.0;
	state->config.glass_prism_fps  = 15;
	state->lens_scale              = 0.0;
	out->scale120                  = scale120;
}

static int
rim_partial_diff(uint32_t scale120, int shrink, bool droplet)
{
	static const uint64_t ticks[] = { 1, 7, 40, 133, 391 };
	barny_state_t         state;
	barny_output_t        out;
	cairo_surface_t      *full;
	cairo_surface_t      *part;
	barny_rect_t          rects[5];
	barny_rect_t          a = { 0 };
	barny_rect_t          b = { 0 };
	double                xa;
	double                xb;
	size_t                i;
	int                   n;
	int                   j;
	int                   diff = 0;

	rim_setup(&state, &out, scale120);
	if (droplet)
		state.lens_scale = 1.0;
	full = rim_target(&out);
	part = rim_target(&out);
	rim_draw(&out, part, 300.0, 0, NULL, 0);

	for (i = 0; i < sizeof(ticks) / sizeof(ticks[0]); i++) {
		xa = 300.0 + 23.0 * i;
		xb = xa + 23.0;
		n  = 0;
		if (droplet) {
			state.lens_x = xa;
			barny_lens_rect(&out, &a.x, &a.y, &a.w, &a.h);
			state.lens_x = xb;
			barny_lens_rect(&out, &b.x, &b.y, &b.w, &b.h);
			rects[n++] = barny_rect_union(a, b);
		} else {
			xb = 300.0;
		}
		n += barny_rim_rects(&out, rects + n);
		for (j = droplet ? 1 : 0; j < n; j++) {
			rects[j].x += shrink;
			rects[j].y += shrink;
			rects[j].w -= 2 * shrink;
			rects[j].h -= 2 * shrink;
		}
		rim_draw(&out, part, xb, ticks[i], rects, n);
		rim_draw(&out, full, xb, ticks[i], NULL, 0);
		diff += lens_diff(full, part);
	}

	cairo_surface_destroy(full);
	cairo_surface_destroy(part);
	barny_output_free_glass_caches(&out, false);

	return diff;
}

void
test_rim_flow(void)
{
	barny_state_t    state;
	barny_output_t   out;
	barny_config_t   cfg;
	cairo_surface_t *on;
	cairo_surface_t *off;
	barny_rect_t     rects[4];
	barny_rect_t     lr;
	uint8_t         *da;
	uint8_t         *db;
	int              stride;
	int              cols[2];
	int              bad;
	int              c;
	int              y;

	TEST_SUITE_BEGIN("Rim Flow");

	memset(&cfg, 0, sizeof(cfg));
	cfg.glass_prism      = 0.8;
	cfg.glass_prism_flow = 12.0;
	cfg.glass_prism_fps  = 15;

	TEST("the tick is wall time over the tick length")
	{
		ASSERT_TRUE(barny_rim_tick(&cfg, 0) == 0);
		ASSERT_TRUE(barny_rim_tick(&cfg, 66665) == 0);
		ASSERT_TRUE(barny_rim_tick(&cfg, 66666 * 5 + 3) == 5);
		cfg.glass_prism_fps = 30;
		ASSERT_TRUE(barny_rim_tick(&cfg, 33333 * 7) == 7);
		cfg.glass_prism_fps = 15;
	}

	TEST("the phase is the tick's time modulo the hue period")
	{
		ASSERT_EQ_DBL(0.0, barny_rim_phase(&cfg, 0), 0.0);
		ASSERT_EQ_DBL(66666.0 * 90 / 12e6, barny_rim_phase(&cfg, 90),
		              1e-12);
		ASSERT_EQ_DBL(66666.0 * 181 / 12e6 - 1.0,
		              barny_rim_phase(&cfg, 181), 1e-12);
		cfg.glass_prism_flow = -12.0;
		ASSERT_EQ_DBL(1.0 - 66666.0 * 90 / 12e6,
		              barny_rim_phase(&cfg, 90), 1e-12);
		cfg.glass_prism_flow = 12.0;
	}

	TEST("the timeout waits for the next tick, at most 200 ms")
	{
		ASSERT_EQ_INT(67, barny_rim_timeout_ms(&cfg, 0));
		ASSERT_EQ_INT(1, barny_rim_timeout_ms(&cfg, 66000));
		cfg.glass_prism_fps = 1;
		ASSERT_EQ_INT(200, barny_rim_timeout_ms(&cfg, 0));
		cfg.glass_prism_fps = 15;
	}

	TEST("a static or absent rim schedules no ticks")
	{
		cfg.glass_prism_flow = 0.0;
		ASSERT_FALSE(barny_rim_flowing(&cfg));
		ASSERT_EQ_INT(200, barny_rim_timeout_ms(&cfg, 0));
		cfg.glass_prism_flow = 12.0;
		cfg.glass_prism      = 0.0;
		ASSERT_FALSE(barny_rim_flowing(&cfg));
		ASSERT_EQ_INT(200, barny_rim_timeout_ms(&cfg, 0));
	}

	TEST("glass_prism 0 or flow 0 allocates no rim data")
	{
		rim_setup(&state, &out, 120);
		state.config.glass_prism = 0.0;
		on                       = rim_target(&out);
		rim_draw(&out, on, 300.0, 5, NULL, 0);
		ASSERT_NULL(out.rim);
		ASSERT_EQ_INT(0, barny_rim_rects(&out, rects));
		barny_output_free_glass_caches(&out, false);
		state.config.glass_prism      = 0.8;
		state.config.glass_prism_flow = 0.0;
		rim_draw(&out, on, 300.0, 5, NULL, 0);
		ASSERT_NULL(out.rim);
		barny_output_free_glass_caches(&out, false);
		state.config.glass_prism_flow = 12.0;
		rim_draw(&out, on, 300.0, 5, NULL, 0);
		ASSERT_NOT_NULL(out.rim);
		ASSERT_EQ_INT(4, barny_rim_rects(&out, rects));
		barny_output_free_glass_caches(&out, false);
		ASSERT_NULL(out.rim);
		cairo_surface_destroy(on);
	}

	TEST("rim-only redraw matches a full redraw at scale 1")
	{
		ASSERT_EQ_INT(0, rim_partial_diff(120, 0, false));
	}

	TEST("rim-only redraw matches a full redraw at scale 2")
	{
		ASSERT_EQ_INT(0, rim_partial_diff(240, 0, false));
	}

	TEST("rim-only redraw matches a full redraw at scale 1.25")
	{
		ASSERT_EQ_INT(0, rim_partial_diff(150, 0, false));
	}

	TEST("at scale 2 the rim rects need their 1 px grow")
	{
		ASSERT_TRUE(rim_partial_diff(240, 1, false) > 0);
	}

	TEST("at scale 1.25 the rim rects need their 1 px grow")
	{
		ASSERT_TRUE(rim_partial_diff(150, 1, false) > 0);
	}

	TEST("strip plus rim redraw matches a full redraw while moving")
	{
		ASSERT_EQ_INT(0, rim_partial_diff(120, 0, true));
		ASSERT_EQ_INT(0, rim_partial_diff(240, 0, true));
		ASSERT_EQ_INT(0, rim_partial_diff(150, 0, true));
	}

	TEST("the droplet's far columns match the droplet-free frame")
	{
		rim_setup(&state, &out, 120);
		on  = rim_target(&out);
		off = rim_target(&out);
		bad = 0;
		for (c = 0; c < 3; c++) {
			state.lens_scale = 0.0;
			rim_draw(&out, off, 0.0, 17 + 50 * c, NULL, 0);
			state.lens_scale = 1.0;
			rim_draw(&out, on, 200.0 + 200.0 * c, 17 + 50 * c, NULL,
			         0);
			barny_lens_rect(&out, &lr.x, &lr.y, &lr.w, &lr.h);
			lr = barny_rect_clamp(lr, out.surf_width,
			                      out.surf_height);
			cols[0] = lr.x;
			cols[1] = lr.x + lr.w - 1;
			cairo_surface_flush(on);
			cairo_surface_flush(off);
			da     = cairo_image_surface_get_data(on);
			db     = cairo_image_surface_get_data(off);
			stride = cairo_image_surface_get_stride(on);
			for (y = lr.y; y < lr.y + lr.h; y++) {
				bad += memcmp(da + y * stride + cols[0] * 4,
				              db + y * stride + cols[0] * 4,
				              4) != 0;
				bad += memcmp(da + y * stride + cols[1] * 4,
				              db + y * stride + cols[1] * 4,
				              4) != 0;
			}
		}
		ASSERT_EQ_INT(0, bad);
		cairo_surface_destroy(on);
		cairo_surface_destroy(off);
		barny_output_free_glass_caches(&out, false);
	}

	TEST_SUITE_END();
}

void
test_hidpi_helpers(void)
{
	cairo_surface_t *dst;
	cairo_surface_t *src;
	cairo_t         *cr;
	barny_rect_t     r;
	double           x;
	double           y;
	double           sx;
	double           sy;
	uint8_t         *sd;
	uint8_t         *dd;
	int              ss;
	int              ds;
	int              bad;
	int              i;
	int              j;

	TEST_SUITE_BEGIN("HiDPI Helpers");

	TEST("buffer lengths round like wp_fractional_scale_v1")
	{
		ASSERT_EQ_INT(2560, barny_scale_len(2048, 150));
		ASSERT_EQ_INT(101, barny_scale_len(81, 150));
		ASSERT_EQ_INT(162, barny_scale_len(81, 240));
		ASSERT_EQ_INT(81, barny_scale_len(81, BARNY_SCALE_UNIT));
	}

	TEST("logical rects grow outward to whole buffer pixels")
	{
		r = barny_rect_to_buffer((barny_rect_t){ 3, 3, 10, 10 }, 150);
		ASSERT_EQ_INT(3, r.x);
		ASSERT_EQ_INT(3, r.y);
		ASSERT_EQ_INT(14, r.w);
		ASSERT_EQ_INT(14, r.h);
		r = barny_rect_to_buffer((barny_rect_t){ -1, 0, 2, 1 }, 150);
		ASSERT_EQ_INT(-2, r.x);
		ASSERT_EQ_INT(0, r.y);
		ASSERT_EQ_INT(4, r.w);
		ASSERT_EQ_INT(2, r.h);
		r = barny_rect_to_buffer((barny_rect_t){ 3, 4, 5, 6 }, 240);
		ASSERT_EQ_INT(6, r.x);
		ASSERT_EQ_INT(8, r.y);
		ASSERT_EQ_INT(10, r.w);
		ASSERT_EQ_INT(12, r.h);
		r = barny_rect_to_buffer((barny_rect_t){ 3, 4, 0, 6 }, 150);
		ASSERT_TRUE(barny_rect_empty(r));
	}

	TEST("text origins land on whole device pixels at 1.25")
	{
		dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
		cr  = cairo_create(dst);
		cairo_scale(cr, 1.25, 1.25);
		for (i = 0; i < 8; i++) {
			barny_move_to_pixel(cr, 3 + i, 5 + 0.5 * i);
			cairo_get_current_point(cr, &x, &y);
			cairo_user_to_device(cr, &x, &y);
			ASSERT_EQ_DBL(round(x), x, 1e-9);
			ASSERT_EQ_DBL(round(y), y, 1e-9);
		}
		cairo_destroy(cr);
		cairo_surface_destroy(dst);
	}

	TEST("text caches are drawn at buffer size and composite 1:1")
	{
		cr = barny_popup_surface_begin(40, 20, 150);
		ASSERT_NOT_NULL(cr);
		for (i = 0; i < 40; i++)
			for (j = 0; j < 20; j++)
				if ((i + j) % 3 == 0)
					cairo_rectangle(cr, i, j, 1, 1);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_fill(cr);
		src = barny_popup_surface_end(cr, 150);
		ASSERT_EQ_INT(50, cairo_image_surface_get_width(src));
		ASSERT_EQ_INT(25, cairo_image_surface_get_height(src));
		cairo_surface_get_device_scale(src, &sx, &sy);
		ASSERT_EQ_DBL(1.25, sx, 0.0);
		ASSERT_EQ_DBL(1.25, sy, 0.0);

		dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 80, 80);
		cr  = cairo_create(dst);
		cairo_scale(cr, 1.25, 1.25);
		x = 3.0;
		y = 7.0;
		barny_snap_to_pixel(cr, &x, &y);
		cairo_set_source_surface(cr, src, x, y);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_flush(dst);

		sd  = cairo_image_surface_get_data(src);
		dd  = cairo_image_surface_get_data(dst);
		ss  = cairo_image_surface_get_stride(src);
		ds  = cairo_image_surface_get_stride(dst);
		bad = 0;
		for (j = 0; j < 25; j++)
			bad += memcmp(sd + j * ss, dd + (j + 9) * ds + 4 * 4,
			              50 * 4)
			       != 0;
		ASSERT_EQ_INT(0, bad);
		cairo_surface_destroy(dst);
		cairo_surface_destroy(src);
	}

	TEST_SUITE_END();
}
