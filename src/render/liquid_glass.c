#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <cairo/cairo.h>
#include <jpeglib.h>
#include <setjmp.h>

#include "barny.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Stack blur implementation for efficient Gaussian-like blur */
static void
stack_blur_line(uint8_t *src, uint8_t *dst, int width, int radius)
{
	if (radius < 1)
		return;

	int  div   = radius * 2 + 1;
	int *stack = malloc(div * sizeof(int) * 4);
	int  sum_r, sum_g, sum_b, sum_a;
	int  sum_in_r, sum_in_g, sum_in_b, sum_in_a;
	int  sum_out_r, sum_out_g, sum_out_b, sum_out_a;
	int  sp, stack_start;

	sum_r = sum_g = sum_b = sum_a = 0;
	sum_in_r = sum_in_g = sum_in_b = sum_in_a = 0;
	sum_out_r = sum_out_g = sum_out_b = sum_out_a = 0;

	/* Initialize stack */
	for (int i = -radius; i <= radius; i++) {
		int x               = i < 0 ? 0 : (i >= width ? width - 1 : i);
		int idx             = i + radius;
		stack[idx * 4 + 0]  = src[x * 4 + 0];
		stack[idx * 4 + 1]  = src[x * 4 + 1];
		stack[idx * 4 + 2]  = src[x * 4 + 2];
		stack[idx * 4 + 3]  = src[x * 4 + 3];

		int rbs             = radius + 1 - abs(i);
		sum_r              += stack[idx * 4 + 0] * rbs;
		sum_g              += stack[idx * 4 + 1] * rbs;
		sum_b              += stack[idx * 4 + 2] * rbs;
		sum_a              += stack[idx * 4 + 3] * rbs;

		if (i > 0) {
			sum_in_r += stack[idx * 4 + 0];
			sum_in_g += stack[idx * 4 + 1];
			sum_in_b += stack[idx * 4 + 2];
			sum_in_a += stack[idx * 4 + 3];
		} else {
			sum_out_r += stack[idx * 4 + 0];
			sum_out_g += stack[idx * 4 + 1];
			sum_out_b += stack[idx * 4 + 2];
			sum_out_a += stack[idx * 4 + 3];
		}
	}

	sp          = radius;
	int mul_sum = (radius + 1) * (radius + 1);

	for (int x = 0; x < width; x++) {
		dst[x * 4 + 0]  = sum_r / mul_sum;
		dst[x * 4 + 1]  = sum_g / mul_sum;
		dst[x * 4 + 2]  = sum_b / mul_sum;
		dst[x * 4 + 3]  = sum_a / mul_sum;

		sum_r          -= sum_out_r;
		sum_g          -= sum_out_g;
		sum_b          -= sum_out_b;
		sum_a          -= sum_out_a;

		stack_start     = sp + div - radius;
		if (stack_start >= div)
			stack_start -= div;

		sum_out_r -= stack[stack_start * 4 + 0];
		sum_out_g -= stack[stack_start * 4 + 1];
		sum_out_b -= stack[stack_start * 4 + 2];
		sum_out_a -= stack[stack_start * 4 + 3];

		int px     = x + radius + 1;
		if (px >= width)
			px = width - 1;

		stack[stack_start * 4 + 0]  = src[px * 4 + 0];
		stack[stack_start * 4 + 1]  = src[px * 4 + 1];
		stack[stack_start * 4 + 2]  = src[px * 4 + 2];
		stack[stack_start * 4 + 3]  = src[px * 4 + 3];

		sum_in_r                   += stack[stack_start * 4 + 0];
		sum_in_g                   += stack[stack_start * 4 + 1];
		sum_in_b                   += stack[stack_start * 4 + 2];
		sum_in_a                   += stack[stack_start * 4 + 3];

		sum_r                      += sum_in_r;
		sum_g                      += sum_in_g;
		sum_b                      += sum_in_b;
		sum_a                      += sum_in_a;

		sp++;
		if (sp >= div)
			sp = 0;

		sum_out_r += stack[sp * 4 + 0];
		sum_out_g += stack[sp * 4 + 1];
		sum_out_b += stack[sp * 4 + 2];
		sum_out_a += stack[sp * 4 + 3];

		sum_in_r  -= stack[sp * 4 + 0];
		sum_in_g  -= stack[sp * 4 + 1];
		sum_in_b  -= stack[sp * 4 + 2];
		sum_in_a  -= stack[sp * 4 + 3];
	}

	free(stack);
}

void
barny_blur_surface(cairo_surface_t *surface, int radius)
{
	if (radius < 1)
		return;

	cairo_surface_flush(surface);

	int      width  = cairo_image_surface_get_width(surface);
	int      height = cairo_image_surface_get_height(surface);
	int      stride = cairo_image_surface_get_stride(surface);
	uint8_t *data   = cairo_image_surface_get_data(surface);

	uint8_t *temp   = malloc(width * 4);
	uint8_t *temp2  = malloc(width * 4);

	/* Horizontal pass */
	for (int y = 0; y < height; y++) {
		uint8_t *row = data + y * stride;
		memcpy(temp, row, width * 4);
		stack_blur_line(temp, row, width, radius);
	}

	/* Vertical pass (transpose, blur, transpose back) */
	uint8_t *col     = malloc(height * 4);
	uint8_t *col_out = malloc(height * 4);

	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			col[y * 4 + 0] = data[y * stride + x * 4 + 0];
			col[y * 4 + 1] = data[y * stride + x * 4 + 1];
			col[y * 4 + 2] = data[y * stride + x * 4 + 2];
			col[y * 4 + 3] = data[y * stride + x * 4 + 3];
		}
		stack_blur_line(col, col_out, height, radius);
		for (int y = 0; y < height; y++) {
			data[y * stride + x * 4 + 0] = col_out[y * 4 + 0];
			data[y * stride + x * 4 + 1] = col_out[y * 4 + 1];
			data[y * stride + x * 4 + 2] = col_out[y * 4 + 2];
			data[y * stride + x * 4 + 3] = col_out[y * 4 + 3];
		}
	}

	free(col);
	free(col_out);
	free(temp);
	free(temp2);

	cairo_surface_mark_dirty(surface);
}

void
barny_apply_brightness(cairo_surface_t *surface, double factor)
{
	cairo_surface_flush(surface);

	int      width  = cairo_image_surface_get_width(surface);
	int      height = cairo_image_surface_get_height(surface);
	int      stride = cairo_image_surface_get_stride(surface);
	uint8_t *data   = cairo_image_surface_get_data(surface);

	for (int y = 0; y < height; y++) {
		uint8_t *row = data + y * stride;
		for (int x = 0; x < width; x++) {
			int b          = row[x * 4 + 0];
			int g          = row[x * 4 + 1];
			int r          = row[x * 4 + 2];

			b              = (int)(b * factor);
			g              = (int)(g * factor);
			r              = (int)(r * factor);

			row[x * 4 + 0] = b > 255 ? 255 : b;
			row[x * 4 + 1] = g > 255 ? 255 : g;
			row[x * 4 + 2] = r > 255 ? 255 : r;
		}
	}

	cairo_surface_mark_dirty(surface);
}

/* Perlin noise implementation for displacement effect */
static double
perlin_fade(double t)
{
	return t * t * t * (t * (t * 6 - 15) + 10);
}

static double
perlin_lerp(double a, double b, double t)
{
	return a + t * (b - a);
}

static double
perlin_grad(int hash, double x, double y)
{
	int    h = hash & 3;
	double u = h < 2 ? x : y;
	double v = h < 2 ? y : x;
	return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static int perlin_perm[512];
static int perlin_initialized = 0;

static void
perlin_init(void)
{
	if (perlin_initialized)
		return;

	/* Standard permutation table */
	static const int p[] = {
		151, 160, 137, 91,  90,  15,  131, 13,  201, 95,  96,  53,  194,
		233, 7,   225, 140, 36,  103, 30,  69,  142, 8,   99,  37,  240,
		21,  10,  23,  190, 6,   148, 247, 120, 234, 75,  0,   26,  197,
		62,  94,  252, 219, 203, 117, 35,  11,  32,  57,  177, 33,  88,
		237, 149, 56,  87,  174, 20,  125, 136, 171, 168, 68,  175, 74,
		165, 71,  134, 139, 48,  27,  166, 77,  146, 158, 231, 83,  111,
		229, 122, 60,  211, 133, 230, 220, 105, 92,  41,  55,  46,  245,
		40,  244, 102, 143, 54,  65,  25,  63,  161, 1,   216, 80,  73,
		209, 76,  132, 187, 208, 89,  18,  169, 200, 196, 135, 130, 116,
		188, 159, 86,  164, 100, 109, 198, 173, 186, 3,   64,  52,  217,
		226, 250, 124, 123, 5,   202, 38,  147, 118, 126, 255, 82,  85,
		212, 207, 206, 59,  227, 47,  16,  58,  17,  182, 189, 28,  42,
		223, 183, 170, 213, 119, 248, 152, 2,   44,  154, 163, 70,  221,
		153, 101, 155, 167, 43,  172, 9,   129, 22,  39,  253, 19,  98,
		108, 110, 79,  113, 224, 232, 178, 185, 112, 104, 218, 246, 97,
		228, 251, 34,  242, 193, 238, 210, 144, 12,  191, 179, 162, 241,
		81,  51,  145, 235, 249, 14,  239, 107, 49,  192, 214, 31,  181,
		199, 106, 157, 184, 84,  204, 176, 115, 121, 50,  45,  127, 4,
		150, 254, 138, 236, 205, 93,  222, 114, 67,  29,  24,  72,  243,
		141, 128, 195, 78,  66,  215, 61,  156, 180
	};

	for (int i = 0; i < 256; i++) {
		perlin_perm[i]       = p[i];
		perlin_perm[256 + i] = p[i];
	}
	perlin_initialized = 1;
}

static double
perlin_noise2d(double x, double y)
{
	perlin_init();

	int    xi = (int)floor(x) & 255;
	int    yi = (int)floor(y) & 255;

	double xf = x - floor(x);
	double yf = y - floor(y);

	double u  = perlin_fade(xf);
	double v  = perlin_fade(yf);

	int    aa = perlin_perm[perlin_perm[xi] + yi];
	int    ab = perlin_perm[perlin_perm[xi] + yi + 1];
	int    ba = perlin_perm[perlin_perm[xi + 1] + yi];
	int    bb = perlin_perm[perlin_perm[xi + 1] + yi + 1];

	double x1 = perlin_lerp(perlin_grad(aa, xf, yf),
	                        perlin_grad(ba, xf - 1, yf), u);
	double x2 = perlin_lerp(perlin_grad(ab, xf, yf - 1),
	                        perlin_grad(bb, xf - 1, yf - 1), u);

	return perlin_lerp(x1, x2, v);
}

static double
perlin_fbm(double x, double y, int octaves, double persistence)
{
	double total     = 0;
	double amplitude = 1;
	double frequency = 1;
	double max_value = 0;

	for (int i = 0; i < octaves; i++) {
		total += perlin_noise2d(x * frequency, y * frequency) * amplitude;
		max_value += amplitude;
		amplitude *= persistence;
		frequency *= 2;
	}

	return total / max_value;
}

/*
 * Create a displacement map for liquid glass effect.
 * Red channel = X displacement, Green channel = Y displacement
 * Value of 128 = no displacement, <128 = negative, >128 = positive
 */
cairo_surface_t *
barny_create_displacement_map(int width, int height, barny_refraction_mode_t mode,
                              int border_radius, double edge_strength,
                              double noise_scale, int noise_octaves)
{
	cairo_surface_t *surface
	        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		return NULL;
	}

	cairo_surface_flush(surface);
	uint8_t *data   = cairo_image_surface_get_data(surface);
	int      stride = cairo_image_surface_get_stride(surface);

	double   cx     = width / 2.0;
	double   cy     = height / 2.0;

	for (int y = 0; y < height; y++) {
		uint8_t *row = data + y * stride;
		for (int x = 0; x < width; x++) {
			double dx     = (x - cx) / cx; /* Normalized -1 to 1 */
			double dy     = (y - cy) / cy;

			double disp_x = 0, disp_y = 0;

			if (mode == BARNY_REFRACT_LENS) {
				/*
				 * Lens/bubble effect: pixels are pushed away from center
				 * creating a magnifying glass look. The displacement increases
				 * near the edges for that characteristic liquid glass bulge.
				 */
				double dist = sqrt(dx * dx + dy * dy);
				if (dist > 0.001) {
					/* Smooth falloff from center to edge */
					double falloff
					        = 1.0 - pow(1.0 - dist, 2.0);
					/* Add extra edge refraction */
					double edge_factor
					        = pow(dist, edge_strength);

					/* Direction from center, scaled by distance */
					disp_x = (dx / dist)
					         * falloff
					         * edge_factor
					         * 0.5;
					disp_y = (dy / dist)
					         * falloff
					         * edge_factor
					         * 0.5;
				}
			} else if (mode == BARNY_REFRACT_LIQUID) {
				/*
				 * Liquid/turbulence effect: Use Perlin noise for organic
				 * distortion like looking through wavy glass or water.
				 */
				double nx = x * noise_scale;
				double ny = y * noise_scale;

				/* Use offset coordinates for X and Y to get different patterns */
				disp_x    = perlin_fbm(nx, ny, noise_octaves, 0.5)
				         * 0.5;
				disp_y = perlin_fbm(nx + 100.0, ny + 100.0,
				                    noise_octaves, 0.5)
				         * 0.5;

				/* Fade out near edges for smooth blending */
				double edge_x = fmin(x, width - x)
				                / (double)border_radius;
				double edge_y = fmin(y, height - y)
				                / (double)border_radius;
				double edge_fade = fmin(1.0, fmin(edge_x, edge_y));
				disp_x *= edge_fade;
				disp_y *= edge_fade;
			}

			/* Convert to 0-255 range (128 = no displacement) */
			int r          = (int)(128 + disp_x * 255);
			int g          = (int)(128 + disp_y * 255);
			r              = r < 0 ? 0 : (r > 255 ? 255 : r);
			g              = g < 0 ? 0 : (g > 255 ? 255 : g);

			/* BGRA format */
			row[x * 4 + 0] = 0;   /* B - unused */
			row[x * 4 + 1] = g;   /* G - Y displacement */
			row[x * 4 + 2] = r;   /* R - X displacement */
			row[x * 4 + 3] = 255; /* A */
		}
	}

	cairo_surface_mark_dirty(surface);
	return surface;
}

/*
 * Sample a pixel from surface with bilinear interpolation.
 * Returns pixel in BGRA order.
 */
static void
sample_bilinear(uint8_t *data, int stride, int width, int height, double x,
                double y, uint8_t *out)
{
	/* Clamp coordinates */
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= width - 1)
		x = width - 1.001;
	if (y >= height - 1)
		y = height - 1.001;

	int      x0  = (int)x;
	int      y0  = (int)y;
	int      x1  = x0 + 1;
	int      y1  = y0 + 1;

	double   fx  = x - x0;
	double   fy  = y - y0;

	uint8_t *p00 = data + y0 * stride + x0 * 4;
	uint8_t *p10 = data + y0 * stride + x1 * 4;
	uint8_t *p01 = data + y1 * stride + x0 * 4;
	uint8_t *p11 = data + y1 * stride + x1 * 4;

	for (int i = 0; i < 4; i++) {
		double v00 = p00[i];
		double v10 = p10[i];
		double v01 = p01[i];
		double v11 = p11[i];

		double v0  = v00 + (v10 - v00) * fx;
		double v1  = v01 + (v11 - v01) * fx;
		double v   = v0 + (v1 - v0) * fy;

		out[i]     = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
	}
}

/*
 * Apply displacement map to source image with chromatic aberration.
 * This creates the actual liquid glass refraction effect.
 */
void
barny_apply_displacement(cairo_surface_t *src, cairo_surface_t *dst,
                         cairo_surface_t *displacement_map, double scale,
                         double chromatic)
{
	cairo_surface_flush(src);
	cairo_surface_flush(displacement_map);

	int      width       = cairo_image_surface_get_width(dst);
	int      height      = cairo_image_surface_get_height(dst);

	int      src_width   = cairo_image_surface_get_width(src);
	int      src_height  = cairo_image_surface_get_height(src);
	int      src_stride  = cairo_image_surface_get_stride(src);
	uint8_t *src_data    = cairo_image_surface_get_data(src);

	int      disp_width  = cairo_image_surface_get_width(displacement_map);
	int      disp_height = cairo_image_surface_get_height(displacement_map);
	int      disp_stride = cairo_image_surface_get_stride(displacement_map);
	uint8_t *disp_data   = cairo_image_surface_get_data(displacement_map);

	int      dst_stride  = cairo_image_surface_get_stride(dst);
	uint8_t *dst_data    = cairo_image_surface_get_data(dst);

	/* Scale factors for displacement map to output */
	double   scale_x     = (double)disp_width / width;
	double   scale_y     = (double)disp_height / height;

	/* Scale factors for source to output */
	double   src_scale_x = (double)src_width / width;
	double   src_scale_y = (double)src_height / height;

	for (int y = 0; y < height; y++) {
		uint8_t *dst_row = dst_data + y * dst_stride;

		for (int x = 0; x < width; x++) {
			/* Get displacement from map */
			int disp_x = (int)(x * scale_x);
			int disp_y = (int)(y * scale_y);
			if (disp_x >= disp_width)
				disp_x = disp_width - 1;
			if (disp_y >= disp_height)
				disp_y = disp_height - 1;

			uint8_t *disp_pixel
			        = disp_data + disp_y * disp_stride + disp_x * 4;
			double dx = ((disp_pixel[2] - 128) / 128.0)
			            * scale; /* R channel */
			double dy = ((disp_pixel[1] - 128) / 128.0)
			            * scale; /* G channel */

			/* Source coordinates with displacement */
			double src_x = x * src_scale_x + dx;
			double src_y = y * src_scale_y + dy;

			if (chromatic > 0.01) {
				/*
				 * Chromatic aberration: sample R, G, B at slightly different
				 * positions to simulate how different wavelengths of light
				 * refract differently through glass.
				 */
				uint8_t pixel_r[4], pixel_g[4], pixel_b[4];

				/* Red channel - displaced slightly more */
				sample_bilinear(src_data, src_stride, src_width,
				                src_height,
				                src_x + dx * chromatic * 0.1,
				                src_y + dy * chromatic * 0.1,
				                pixel_r);

				/* Green channel - base displacement */
				sample_bilinear(src_data, src_stride, src_width,
				                src_height, src_x, src_y, pixel_g);

				/* Blue channel - displaced slightly less */
				sample_bilinear(src_data, src_stride, src_width,
				                src_height,
				                src_x - dx * chromatic * 0.1,
				                src_y - dy * chromatic * 0.1,
				                pixel_b);

				/* Combine channels (BGRA format) */
				dst_row[x * 4 + 0]
				        = pixel_b[0]; /* B from blue sample */
				dst_row[x * 4 + 1]
				        = pixel_g[1]; /* G from green sample */
				dst_row[x * 4 + 2]
				        = pixel_r[2]; /* R from red sample */
				dst_row[x * 4 + 3]
				        = pixel_g[3]; /* A from green sample */
			} else {
				/* No chromatic aberration - simple displacement */
				uint8_t pixel[4];
				sample_bilinear(src_data, src_stride, src_width,
				                src_height, src_x, src_y, pixel);
				dst_row[x * 4 + 0] = pixel[0];
				dst_row[x * 4 + 1] = pixel[1];
				dst_row[x * 4 + 2] = pixel[2];
				dst_row[x * 4 + 3] = pixel[3];
			}
		}
	}

	cairo_surface_mark_dirty(dst);
}

/* Helper to create rounded rectangle path */
static void
create_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_path(cr);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_arc(cr, x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_close_path(cr);
}

/* Render the liquid glass background */
void
barny_render_liquid_glass(barny_output_t *output, cairo_t *cr)
{
	barny_state_t *state  = output->state;
	int            width  = output->width;
	int            height = output->height;
	int            radius = state->config.border_radius;

	/* Clear to transparent */
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	double x = 0, y = 0, w = width, h = height;
	double r = radius;

	/* Create rounded rectangle clip */
	create_rounded_rect(cr, x, y, w, h, r);
	cairo_clip(cr);

	/* Draw wallpaper with displacement effect or fallback */
	cairo_surface_t *bg_surface = state->displaced_wallpaper ?
	                                      state->displaced_wallpaper :
	                                      state->blurred_wallpaper;

	if (bg_surface) {
		int    wp_width  = cairo_image_surface_get_width(bg_surface);
		int    wp_height = cairo_image_surface_get_height(bg_surface);

		/* Scale to fit */
		double scale_x   = (double)wp_width / width;
		double scale_y   = (double)wp_height / height;
		double scale     = scale_x < scale_y ? scale_x : scale_y;

		int    src_y     = state->config.position_top ?
		                           0 :
		                           (wp_height - (int)(height * scale));

		cairo_save(cr);
		cairo_scale(cr, 1.0 / scale, 1.0 / scale);
		cairo_set_source_surface(cr, bg_surface, 0, -src_y);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		/* Fallback: semi-transparent dark background with subtle gradient */
		cairo_pattern_t *bg_grad
		        = cairo_pattern_create_linear(0, 0, 0, height);
		cairo_pattern_add_color_stop_rgba(bg_grad, 0, 0.15, 0.15, 0.18,
		                                  0.85);
		cairo_pattern_add_color_stop_rgba(bg_grad, 1, 0.08, 0.08, 0.10,
		                                  0.85);
		cairo_set_source(cr, bg_grad);
		cairo_paint(cr);
		cairo_pattern_destroy(bg_grad);
	}

	cairo_reset_clip(cr);

	/*
	 * Liquid glass edge effects - multiple layers for depth:
	 * 1. Outer thin border (subtle)
	 * 2. Inner glow (diffuse white)
	 * 3. Top-left specular highlight
	 * 4. Bottom-right subtle shadow
	 */

	/* Layer 1: Outer border - thin white line with low opacity */
	create_rounded_rect(cr, x + 0.5, y + 0.5, w - 1, h - 1, r);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	/* Layer 2: Inner glow - slightly inset, diffuse */
	create_rounded_rect(cr, x + 1.5, y + 1.5, w - 3, h - 3, r - 1);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	/* Layer 3: Top-left specular highlight (simulates light source) */
	create_rounded_rect(cr, x, y, w, h, r);
	cairo_clip(cr);

	cairo_pattern_t *highlight
	        = cairo_pattern_create_linear(0, 0, width * 0.7, height * 0.7);
	cairo_pattern_add_color_stop_rgba(highlight, 0.0, 1, 1, 1, 0.35);
	cairo_pattern_add_color_stop_rgba(highlight, 0.3, 1, 1, 1, 0.08);
	cairo_pattern_add_color_stop_rgba(highlight, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, highlight);
	cairo_paint(cr);
	cairo_pattern_destroy(highlight);

	/* Layer 4: Bottom-right shadow for depth */
	cairo_pattern_t *shadow = cairo_pattern_create_linear(
	        width * 0.3, height * 0.3, width, height);
	cairo_pattern_add_color_stop_rgba(shadow, 0.0, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(shadow, 0.7, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(shadow, 1.0, 0, 0, 0, 0.15);
	cairo_set_source(cr, shadow);
	cairo_paint(cr);
	cairo_pattern_destroy(shadow);

	/* Layer 5: Edge refraction highlight - mimics light bending at glass edges */
	cairo_reset_clip(cr);
	create_rounded_rect(cr, x, y, w, h, r);
	cairo_clip(cr);

	/* Top edge highlight */
	cairo_pattern_t *top_refract = cairo_pattern_create_linear(0, 0, 0, 8);
	cairo_pattern_add_color_stop_rgba(top_refract, 0.0, 1, 1, 1, 0.2);
	cairo_pattern_add_color_stop_rgba(top_refract, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, top_refract);
	cairo_rectangle(cr, 0, 0, width, 8);
	cairo_fill(cr);
	cairo_pattern_destroy(top_refract);

	/* Left edge highlight */
	cairo_pattern_t *left_refract = cairo_pattern_create_linear(0, 0, 8, 0);
	cairo_pattern_add_color_stop_rgba(left_refract, 0.0, 1, 1, 1, 0.15);
	cairo_pattern_add_color_stop_rgba(left_refract, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, left_refract);
	cairo_rectangle(cr, 0, 0, 8, height);
	cairo_fill(cr);
	cairo_pattern_destroy(left_refract);

	cairo_reset_clip(cr);
}

/* JPEG error handling */
struct jpeg_error_mgr_ext {
	struct jpeg_error_mgr pub;
	jmp_buf               setjmp_buffer;
};

static void
jpeg_error_exit(j_common_ptr cinfo)
{
	struct jpeg_error_mgr_ext *err = (struct jpeg_error_mgr_ext *)cinfo->err;
	longjmp(err->setjmp_buffer, 1);
}

/* Load JPEG image into Cairo surface */
static cairo_surface_t *
load_jpeg(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}

	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr_ext     jerr;

	cinfo.err           = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = jpeg_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		fclose(f);
		return NULL;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, f);
	jpeg_read_header(&cinfo, TRUE);

	/* Request RGB output */
	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	int              width      = cinfo.output_width;
	int              height     = cinfo.output_height;
	int              row_stride = cinfo.output_width * cinfo.output_components;

	/* Create Cairo surface (ARGB32 format) */
	cairo_surface_t *surface
	        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(f);
		return NULL;
	}

	cairo_surface_flush(surface);
	uint8_t   *data         = cairo_image_surface_get_data(surface);
	int        cairo_stride = cairo_image_surface_get_stride(surface);

	/* Allocate row buffer */
	JSAMPARRAY buffer       = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                                                       JPOOL_IMAGE, row_stride, 1);

	/* Read scanlines and convert RGB to BGRA */
	while (cinfo.output_scanline < cinfo.output_height) {
		int y = cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, buffer, 1);

		uint8_t *dst_row = data + y * cairo_stride;
		uint8_t *src_row = buffer[0];

		for (int x = 0; x < width; x++) {
			dst_row[x * 4 + 0] = src_row[x * 3 + 2]; /* B */
			dst_row[x * 4 + 1] = src_row[x * 3 + 1]; /* G */
			dst_row[x * 4 + 2] = src_row[x * 3 + 0]; /* R */
			dst_row[x * 4 + 3] = 255;                /* A */
		}
	}

	cairo_surface_mark_dirty(surface);

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(f);

	return surface;
}

/* Check file extension (case-insensitive) */
static int
has_extension(const char *path, const char *ext)
{
	size_t path_len = strlen(path);
	size_t ext_len  = strlen(ext);
	if (path_len < ext_len)
		return 0;
	const char *path_ext = path + path_len - ext_len;
	for (size_t i = 0; i < ext_len; i++) {
		if (tolower((unsigned char)path_ext[i])
		    != tolower((unsigned char)ext[i])) {
			return 0;
		}
	}
	return 1;
}

cairo_surface_t *
barny_load_wallpaper(const char *path)
{
	cairo_surface_t *surface = NULL;

	/* Try to detect format by extension */
	if (has_extension(path, ".jpg") || has_extension(path, ".jpeg")) {
		surface = load_jpeg(path);
		if (surface) {
			printf("barny: loaded JPEG wallpaper: %s\n", path);
			return surface;
		}
	}

	/* Try PNG (default) */
	surface = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
		printf("barny: loaded PNG wallpaper: %s\n", path);
		return surface;
	}
	cairo_surface_destroy(surface);

	/* If PNG failed and we haven't tried JPEG yet, try it now */
	if (!has_extension(path, ".jpg") && !has_extension(path, ".jpeg")) {
		surface = load_jpeg(path);
		if (surface) {
			printf("barny: loaded JPEG wallpaper: %s\n", path);
			return surface;
		}
	}

	fprintf(stderr, "barny: failed to load wallpaper: %s\n", path);
	return NULL;
}
