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
#include "glass_shade.h"
#include "util.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BAR_LENS_EDGE  20.0
#define BAR_LENS_DISP  17.0
#define BAR_LENS_CHROMA 8.0

#define BUBBLE_W       172.0

#define BULGE_NECK      16     /* horizontal patch margin for neck fillets */
#define BULGE_SMIN      9.0    /* metaball neck fillet scale (surface tension) */
#define BULGE_REFRACT   2.30   /* lens refraction / magnification gain */
#define BULGE_BIAS      4.0    /* min inward sample so the rim isn't starved */
#define BULGE_CHROMA    9.0    /* chromatic dispersion at the lens edge (px) */
#define BULGE_VEIL      0.06   /* frosted brightness lift inside the bubble */
#define BULGE_VEIL_FADE 7.0    /* px the veil fades in from the bubble edge */
#define BULGE_RIM_W     5.0    /* inner glow width of the bubble rim */
#define BULGE_RIM_STR   0.30   /* soft inner glow of the bubble rim */
#define BULGE_RIM_CORE  0.55   /* crisp bright line at the bubble outline */
#define BULGE_CORE_W    1.4    /* px the crisp outline reaches past the rim */
#define BULGE_CHROMA_MIN 0.25  /* px: below this the 3 taps land on one pixel */

#define LENS_SPRING_K    500.0 /* position spring stiffness, s^-2 */
#define LENS_SPRING_ZETA 0.80  /* responsive with a restrained overshoot */
#define LENS_POP_K       900.0 /* enter/leave pop spring, critically damped */
#define LENS_SETTLE_X    0.4   /* px */
#define LENS_SETTLE_V    8.0   /* px/s */
#define LENS_STRETCH_GAIN 3.2e-4 /* velocity -> horizontal stretch */
#define LENS_STRETCH_MAX 0.18
#define LENS_SKEW_GAIN   1.2e-4 /* velocity -> trailing-edge shear */
#define LENS_SKEW_MAX    0.10

#define LENS_PINCH_MAX   5.5   /* px of bar-edge recession under the droplet */
#define LENS_PINCH_FADE  24.0  /* px over which the pinch dies near corners */
#define LENS_FAR_MARGIN  12.0  /* droplet influence radius: beyond it, 1:1 copy */

#define PRISM_STOPS  40
#define PRISM_HUES   16
#define PRISM_CYCLES ((double)PRISM_STOPS / PRISM_HUES)
#define PRISM_SAT    0.9
#define PRISM_VAL    1.0
#define PRISM_LINE_W 1.6

#define SPEC_W       7.0   /* px band the specular hugs inside the rim */
#define SPEC_GAIN    2.4   /* glass_gleam multiplier */
#define SPEC_COUNTER 0.22  /* strength of the cool counter-arc */

static void
stack_blur_line(uint8_t *src, uint8_t *dst, int width, int radius, int *stack)
{
	int div = radius * 2 + 1;
	int sum_r, sum_g, sum_b, sum_a;
	int sum_in_r, sum_in_g, sum_in_b, sum_in_a;
	int sum_out_r, sum_out_g, sum_out_b, sum_out_a;
	int sp, stack_start;
	int mul_sum = (radius + 1) * (radius + 1);
	int i;
	int x;
	int idx;
	int rbs;
	int px;

	if (radius < 1)
		return;

	sum_r = sum_g = sum_b = sum_a = 0;
	sum_in_r = sum_in_g = sum_in_b = sum_in_a = 0;
	sum_out_r = sum_out_g = sum_out_b = sum_out_a = 0;

	for (i = -radius; i <= radius; i++) {
		x                   = i < 0 ? 0 : (i >= width ? width - 1 : i);
		idx                 = i + radius;
		stack[idx * 4 + 0]  = src[x * 4 + 0];
		stack[idx * 4 + 1]  = src[x * 4 + 1];
		stack[idx * 4 + 2]  = src[x * 4 + 2];
		stack[idx * 4 + 3]  = src[x * 4 + 3];

		rbs                 = radius + 1 - abs(i);
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

	sp = radius;

	for (x = 0; x < width; x++) {
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

		px         = x + radius + 1;
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
}

void
barny_blur_surface(cairo_surface_t *surface, int radius)
{
	int      width;
	int      height;
	int      stride;
	uint8_t *data;
	int      div;
	int     *stack;
	uint8_t *temp;
	uint8_t *col;
	uint8_t *col_out;
	uint8_t *row;
	int      x;
	int      y;

	if (radius < 1)
		return;

	cairo_surface_flush(surface);

	width  = cairo_image_surface_get_width(surface);
	height = cairo_image_surface_get_height(surface);

	if (width <= 0 || height <= 0)
		return;

	stride = cairo_image_surface_get_stride(surface);
	data   = cairo_image_surface_get_data(surface);

	div    = radius * 2 + 1;
	stack  = malloc(div * sizeof(int) * 4);
	if (!stack)
		return;

	temp = malloc(width * 4);
	if (!temp) {
		free(stack);
		return;
	}

	for (y = 0; y < height; y++) {
		row = data + y * stride;
		memcpy(temp, row, width * 4);
		stack_blur_line(temp, row, width, radius, stack);
	}

	col     = malloc(height * 4);
	col_out = malloc(height * 4);

	if (col && col_out) {
		for (x = 0; x < width; x++) {
			for (y = 0; y < height; y++) {
				col[y * 4 + 0] = data[y * stride + x * 4 + 0];
				col[y * 4 + 1] = data[y * stride + x * 4 + 1];
				col[y * 4 + 2] = data[y * stride + x * 4 + 2];
				col[y * 4 + 3] = data[y * stride + x * 4 + 3];
			}
			stack_blur_line(col, col_out, height, radius, stack);
			for (y = 0; y < height; y++) {
				data[y * stride + x * 4 + 0] = col_out[y * 4 + 0];
				data[y * stride + x * 4 + 1] = col_out[y * 4 + 1];
				data[y * stride + x * 4 + 2] = col_out[y * 4 + 2];
				data[y * stride + x * 4 + 3] = col_out[y * 4 + 3];
			}
		}
	}

	free(col);
	free(col_out);
	free(temp);
	free(stack);

	cairo_surface_mark_dirty(surface);
}

void
barny_apply_vibrancy(cairo_surface_t *surface, double saturation,
                     double brightness)
{
	int      width;
	int      height;
	int      stride;
	uint8_t *data;
	uint8_t *row;
	int      x;
	int      y;
	double   b;
	double   g;
	double   r;
	double   luma;

	cairo_surface_flush(surface);

	width  = cairo_image_surface_get_width(surface);
	height = cairo_image_surface_get_height(surface);
	stride = cairo_image_surface_get_stride(surface);
	data   = cairo_image_surface_get_data(surface);

	for (y = 0; y < height; y++) {
		row = data + y * stride;
		for (x = 0; x < width; x++) {
			b    = row[x * 4 + 0];
			g    = row[x * 4 + 1];
			r    = row[x * 4 + 2];

			luma = 0.114 * b + 0.587 * g + 0.299 * r;

			b    = (luma + (b - luma) * saturation) * brightness;
			g    = (luma + (g - luma) * saturation) * brightness;
			r    = (luma + (r - luma) * saturation) * brightness;

			row[x * 4 + 0] = b < 0 ? 0 : (b > 255 ? 255 : (uint8_t)b);
			row[x * 4 + 1] = g < 0 ? 0 : (g > 255 ? 255 : (uint8_t)g);
			row[x * 4 + 2] = r < 0 ? 0 : (r > 255 ? 255 : (uint8_t)r);
		}
	}

	cairo_surface_mark_dirty(surface);
}

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
	static const int p[] = {
		151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194,
		233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240,
		21, 10, 23, 190, 6, 148, 247, 120, 234, 75, 0, 26, 197,
		62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33, 88,
		237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74,
		165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111,
		229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245,
		40, 244, 102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73,
		209, 76, 132, 187, 208, 89, 18, 169, 200, 196, 135, 130, 116,
		188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217,
		226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85,
		212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42,
		223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221,
		153, 101, 155, 167, 43, 172, 9, 129, 22, 39, 253, 19, 98,
		108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97,
		228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
		81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181,
		199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4,
		150, 254, 138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243,
		141, 128, 195, 78, 66, 215, 61, 156, 180
	};
	int i;

	if (perlin_initialized)
		return;

	for (i = 0; i < 256; i++) {
		perlin_perm[i]       = p[i];
		perlin_perm[256 + i] = p[i];
	}
	perlin_initialized = 1;
}

static double
perlin_noise2d(double x, double y)
{
	int    xi;
	int    yi;
	double xf;
	double yf;
	double u;
	double v;
	int    aa;
	int    ab;
	int    ba;
	int    bb;
	double x1;
	double x2;

	perlin_init();

	xi = (int)floor(x) & 255;
	yi = (int)floor(y) & 255;

	xf = x - floor(x);
	yf = y - floor(y);

	u  = perlin_fade(xf);
	v  = perlin_fade(yf);

	aa = perlin_perm[perlin_perm[xi] + yi];
	ab = perlin_perm[perlin_perm[xi] + yi + 1];
	ba = perlin_perm[perlin_perm[xi + 1] + yi];
	bb = perlin_perm[perlin_perm[xi + 1] + yi + 1];

	x1 = perlin_lerp(perlin_grad(aa, xf, yf), perlin_grad(ba, xf - 1, yf), u);
	x2 = perlin_lerp(perlin_grad(ab, xf, yf - 1),
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
	int    i;

	for (i = 0; i < octaves; i++) {
		total     += perlin_noise2d(x * frequency, y * frequency) * amplitude;
		max_value += amplitude;
		amplitude *= persistence;
		frequency *= 2;
	}

	return total / max_value;
}

cairo_surface_t *
barny_create_displacement_map(int width, int height, barny_refraction_mode_t mode,
                              int border_radius, double edge_strength,
                              double noise_scale, int noise_octaves)
{
	cairo_surface_t *surface;
	uint8_t         *data;
	int              stride;
	double           cx;
	double           cy;
	int              x;
	int              y;
	uint8_t         *row;
	double           dx;
	double           dy;
	double           disp_x;
	double           disp_y;
	double           dist;
	double           falloff;
	double           edge_factor;
	double           nx;
	double           ny;
	double           br;
	double           edge_x;
	double           edge_y;
	double           edge_fade;
	int              r;
	int              g;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		return NULL;
	}

	cairo_surface_flush(surface);
	data   = cairo_image_surface_get_data(surface);
	stride = cairo_image_surface_get_stride(surface);

	cx     = width / 2.0;
	cy     = height / 2.0;

	for (y = 0; y < height; y++) {
		row = data + y * stride;
		for (x = 0; x < width; x++) {
			dx     = (x - cx) / cx;
			dy     = (y - cy) / cy;

			disp_x = 0, disp_y = 0;

			if (mode == BARNY_REFRACT_LENS) {
				dist = sqrt(dx * dx + dy * dy);
				if (dist > 0.001) {
					falloff     = 1.0 - pow(1.0 - dist, 2.0);

					edge_factor = pow(dist, edge_strength);

					disp_x      = (dx / dist)
					              * falloff
					              * edge_factor
					              * 0.5;
					disp_y      = (dy / dist)
					              * falloff
					              * edge_factor
					              * 0.5;
				}
			} else if (mode == BARNY_REFRACT_LIQUID) {
				nx         = x * noise_scale;
				ny         = y * noise_scale;

				disp_x     = perlin_fbm(nx, ny, noise_octaves, 0.5)
				             * 0.5;
				disp_y     = perlin_fbm(nx + 100.0, ny + 100.0,
				                        noise_octaves, 0.5)
				             * 0.5;

				br         = border_radius > 0 ?
				                     (double)border_radius :
				                     1.0;
				edge_x     = fmin(x, width - x) / br;
				edge_y     = fmin(y, height - y) / br;
				edge_fade  = fmin(1.0, fmin(edge_x, edge_y));
				disp_x    *= edge_fade;
				disp_y    *= edge_fade;
			}

			r              = (int)(128 + disp_x * 255);
			g              = (int)(128 + disp_y * 255);
			r              = r < 0 ? 0 : (r > 255 ? 255 : r);
			g              = g < 0 ? 0 : (g > 255 ? 255 : g);

			row[x * 4 + 0] = 0;
			row[x * 4 + 1] = g;
			row[x * 4 + 2] = r;
			row[x * 4 + 3] = 255;
		}
	}

	cairo_surface_mark_dirty(surface);

	return surface;
}

void
barny_apply_displacement(cairo_surface_t *src, cairo_surface_t *dst,
                         cairo_surface_t *displacement_map, double scale,
                         double chromatic)
{
	int      width;
	int      height;
	int      src_width;
	int      src_height;
	int      src_stride;
	uint8_t *src_data;
	int      disp_width;
	int      disp_height;
	int      disp_stride;
	uint8_t *disp_data;
	int      dst_stride;
	uint8_t *dst_data;
	double   scale_x;
	double   scale_y;
	double   src_scale_x;
	double   src_scale_y;
	int      x;
	int      y;
	uint8_t *dst_row;
	int      disp_x;
	int      disp_y;
	uint8_t *disp_pixel;
	double   dx;
	double   dy;
	double   src_x;
	double   src_y;
	uint8_t  pixel_r[4], pixel_g[4], pixel_b[4];
	uint8_t  pixel[4];

	cairo_surface_flush(src);
	cairo_surface_flush(displacement_map);

	width       = cairo_image_surface_get_width(dst);
	height      = cairo_image_surface_get_height(dst);

	src_width   = cairo_image_surface_get_width(src);
	src_height  = cairo_image_surface_get_height(src);
	src_stride  = cairo_image_surface_get_stride(src);
	src_data    = cairo_image_surface_get_data(src);

	disp_width  = cairo_image_surface_get_width(displacement_map);
	disp_height = cairo_image_surface_get_height(displacement_map);
	disp_stride = cairo_image_surface_get_stride(displacement_map);
	disp_data   = cairo_image_surface_get_data(displacement_map);

	dst_stride  = cairo_image_surface_get_stride(dst);
	dst_data    = cairo_image_surface_get_data(dst);

	scale_x     = (double)disp_width / width;
	scale_y     = (double)disp_height / height;

	src_scale_x = (double)src_width / width;
	src_scale_y = (double)src_height / height;

	for (y = 0; y < height; y++) {
		dst_row = dst_data + y * dst_stride;

		for (x = 0; x < width; x++) {
			disp_x = (int)(x * scale_x);
			disp_y = (int)(y * scale_y);
			if (disp_x >= disp_width)
				disp_x = disp_width - 1;
			if (disp_y >= disp_height)
				disp_y = disp_height - 1;

			disp_pixel = disp_data + disp_y * disp_stride + disp_x * 4;
			dx         = ((disp_pixel[2] - 128) / 128.0)
			             * scale;
			dy         = ((disp_pixel[1] - 128) / 128.0)
			             * scale;

			src_x      = x * src_scale_x + dx;
			src_y      = y * src_scale_y + dy;

			if (chromatic > 0.01) {
				barny_sample_bilinear(src_data, src_stride, src_width,
				                src_height,
				                src_x + dx * chromatic * 0.1,
				                src_y + dy * chromatic * 0.1,
				                pixel_r);

				barny_sample_bilinear(src_data, src_stride, src_width,
				                src_height, src_x, src_y, pixel_g);

				barny_sample_bilinear(src_data, src_stride, src_width,
				                src_height,
				                src_x - dx * chromatic * 0.1,
				                src_y - dy * chromatic * 0.1,
				                pixel_b);

				dst_row[x * 4 + 0]
				        = pixel_b[0];
				dst_row[x * 4 + 1]
				        = pixel_g[1];
				dst_row[x * 4 + 2]
				        = pixel_r[2];
				dst_row[x * 4 + 3]
				        = pixel_g[3];
			} else {
				barny_sample_bilinear(src_data, src_stride, src_width,
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

cairo_surface_t *
barny_create_edge_lens_map(int w, int h, int radius, double edge_w,
                           double max_disp)
{
	cairo_surface_t *surface;
	uint8_t         *data;
	int              stride;
	double           hw = w / 2.0;
	double           hh = h / 2.0;
	double           r  = radius;
	int              x;
	int              y;
	uint8_t         *row;

	if (r > hw)
		r = hw;
	if (r > hh)
		r = hh;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
		return NULL;

	cairo_surface_flush(surface);
	data   = cairo_image_surface_get_data(surface);
	stride = cairo_image_surface_get_stride(surface);

	for (y = 0; y < h; y++) {
		row = data + y * stride;
		for (x = 0; x < w; x++) {
			double px     = x - hw + 0.5;
			double py     = y - hh + 0.5;
			double d      = barny_sd_round_rect(px, py, hw, hh, r);
			double dispx  = 0;
			double dispy  = 0;
			int    rr;
			int    gg;

			if (d <= 0 && d >= -edge_w) {
				double depth = -d;
				double t     = 1.0 - depth / edge_w;
				double mag   = max_disp * t * t * (3.0 - 2.0 * t);
				double gx    = barny_sd_round_rect(px + 1, py, hw, hh, r)
				               - barny_sd_round_rect(px - 1, py, hw, hh, r);
				double gy    = barny_sd_round_rect(px, py + 1, hw, hh, r)
				               - barny_sd_round_rect(px, py - 1, hw, hh, r);
				double len   = sqrt(gx * gx + gy * gy);

				if (len > 1e-6) {
					gx /= len;
					gy /= len;
					dispx = gx * mag;
					dispy = gy * mag;
				}
			}

			dispx /= max_disp;
			dispy /= max_disp;
			if (dispx > 1)
				dispx = 1;
			if (dispx < -1)
				dispx = -1;
			if (dispy > 1)
				dispy = 1;
			if (dispy < -1)
				dispy = -1;

			rr             = 128 + (int)(127 * dispx);
			gg             = 128 + (int)(127 * dispy);
			row[x * 4 + 0] = 0;
			row[x * 4 + 1] = (uint8_t)(gg < 0 ? 0 : (gg > 255 ? 255 : gg));
			row[x * 4 + 2] = (uint8_t)(rr < 0 ? 0 : (rr > 255 ? 255 : rr));
			row[x * 4 + 3] = 255;
		}
	}

	cairo_surface_mark_dirty(surface);

	return surface;
}

/* Bilinear tap specialised for the droplet's inner loop, which is the hottest
   code in the bar: the public sampler works in double, widens 16 bytes per tap,
   and quantises back through uint8 -- together more expensive than the lens
   math it feeds. Straight float, and no output clamp, because a bilinear blend
   of bytes cannot leave 0..255. Bounds are clamped exactly as the public one
   does, so a tap can never leave the surface. */
static inline void
lens_tap(const uint8_t *data, int stride, int width, int height, double x,
         double y, float *out)
{
	const uint8_t *r0;
	const uint8_t *r1;
	int            x0;
	int            y0;
	float          fx;
	float          fy;
	int            i;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= width - 1)
		x = width - 1.001;
	if (y >= height - 1)
		y = height - 1.001;

	x0 = (int)x;
	y0 = (int)y;
	fx = (float)(x - x0);
	fy = (float)(y - y0);

	r0 = data + (size_t)y0 * stride + (size_t)x0 * 4;
	r1 = r0 + stride;

	for (i = 0; i < 4; i++) {
		float a = r0[i];
		float b = r0[4 + i];
		float c = r1[i];
		float d = r1[4 + i];
		float t = a + (b - a) * fx;
		float u = c + (d - c) * fx;

		out[i] = t + (u - t) * fy;
	}
}

/* Chromatic dispersion takes one colour channel from each tap, plus alpha.
   Avoid interpolating the six colour channels that cannot reach the output. */
static inline void
lens_tap_channel_alpha(const uint8_t *data, int stride, int width, int height,
                       double x, double y, int channel, float *color,
                       float *alpha)
{
	const uint8_t *r0;
	const uint8_t *r1;
	int            x0;
	int            y0;
	float          fx;
	float          fy;
	float          t;
	float          u;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= width - 1)
		x = width - 1.001;
	if (y >= height - 1)
		y = height - 1.001;

	x0 = (int)x;
	y0 = (int)y;
	fx = (float)(x - x0);
	fy = (float)(y - y0);

	r0 = data + (size_t)y0 * stride + (size_t)x0 * 4;
	r1 = r0 + stride;

	t      = r0[channel] + (r0[4 + channel] - r0[channel]) * fx;
	u      = r1[channel] + (r1[4 + channel] - r1[channel]) * fx;
	*color = t + (u - t) * fy;

	if (alpha) {
		t      = r0[3] + (r0[7] - r0[3]) * fx;
		u      = r1[3] + (r1[7] - r1[3]) * fx;
		*alpha = t + (u - t) * fy;
	}
}

static void
hsv2rgb(double hh, double s, double v, double *r, double *g, double *b)
{
	double i = floor(hh * 6.0);
	double f = hh * 6.0 - i;
	double p = v * (1.0 - s);
	double q = v * (1.0 - f * s);
	double t = v * (1.0 - (1.0 - f) * s);

	switch (((int)i) % 6) {
	case 0:  *r = v; *g = t; *b = p; break;
	case 1:  *r = q; *g = v; *b = p; break;
	case 2:  *r = p; *g = v; *b = t; break;
	case 3:  *r = p; *g = q; *b = v; break;
	case 4:  *r = t; *g = p; *b = v; break;
	default: *r = v; *g = p; *b = q; break;
	}
}

static double prism_stop[PRISM_HUES][3];
static bool   prism_stops_ready;

/* The baked stroke and the analytic rim both read this ramp, so the rim
   matches exactly at the patch borders. It only depends on compile-time
   constants, so it is built once rather than per frame. */
static void
prism_stops_init(void)
{
	int i;

	if (prism_stops_ready)
		return;

	for (i = 0; i < PRISM_HUES; i++)
		hsv2rgb((double)i / PRISM_HUES, PRISM_SAT, PRISM_VAL,
		        &prism_stop[i][0], &prism_stop[i][1], &prism_stop[i][2]);
	prism_stops_ready = true;
}

static void
prism_row(double *prm, int pw, int sox, double cx, double cw)
{
	int x;
	int c;

	prism_stops_init();

	for (x = 0; x < pw; x++) {
		double        pos = ((double)(x + sox) + 0.5 - cx) / cw;
		const double *a;
		const double *b;
		double        f;
		int           si;

		if (pos < 0.0)
			pos = 0.0;
		if (pos > 1.0)
			pos = 1.0;
		f  = pos * PRISM_STOPS;
		si = (int)f;
		if (si >= PRISM_STOPS)
			si = PRISM_STOPS - 1;
		f -= si;
		a  = prism_stop[si % PRISM_HUES];
		b  = prism_stop[(si + 1) % PRISM_HUES];
		for (c = 0; c < 3; c++)
			prm[x * 3 + c] = a[c] + (b[c] - a[c]) * f;
	}
}

/* The droplet patch and its distance-field scratch keep a fixed size for a
   given bar, so they live on the output and are reused every frame. */
static bool
lens_scratch(barny_output_t *output, int pw, int ph)
{
	int gw = pw + 2;
	int gh = ph + 2;

	if (output->lens_patch && output->lens_patch_w == pw
	    && output->lens_patch_h == ph)
		return true;

	barny_output_free_lens_cache(output);

	output->lens_patch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw,
	                                                ph);
	output->lens_field = calloc((size_t)gw * gh * 2, sizeof(float));
	output->lens_cols  = calloc((size_t)gw + (size_t)pw * 3,
	                            sizeof(double));

	if (cairo_surface_status(output->lens_patch) != CAIRO_STATUS_SUCCESS
	    || !output->lens_field || !output->lens_cols) {
		barny_output_free_lens_cache(output);
		return false;
	}

	output->lens_patch_w = pw;
	output->lens_patch_h = ph;

	return true;
}

void
barny_output_free_lens_cache(barny_output_t *output)
{
	if (output->lens_patch) {
		cairo_surface_destroy(output->lens_patch);
		output->lens_patch = NULL;
	}
	free(output->lens_field);
	free(output->lens_cols);
	output->lens_field   = NULL;
	output->lens_cols    = NULL;
	output->lens_patch_w = 0;
	output->lens_patch_h = 0;
}

void
barny_output_free_glass_caches(barny_output_t *output, bool keep_lens_map)
{
	if (output->bg_cache) {
		cairo_surface_destroy(output->bg_cache);
		output->bg_cache = NULL;
	}
	if (output->shadow_cache) {
		cairo_surface_destroy(output->shadow_cache);
		output->shadow_cache = NULL;
	}
	if (output->glass_clean) {
		cairo_surface_destroy(output->glass_clean);
		output->glass_clean = NULL;
	}
	output->glass_clean_opaque = false;
	if (output->lens_map && !keep_lens_map) {
		cairo_surface_destroy(output->lens_map);
		output->lens_map = NULL;
	}
	barny_output_free_lens_cache(output);
}

typedef struct {
	int    x0;
	int    y0;
	int    pw;
	int    ph;
	double cxp;
	double cyp;
	double hw;
	double hh;
	double br;
	double skew;
	double over;
	double pinch_amp;
	double s;
	bool   authoritative;
} lens_geom_t;

typedef struct {
	uint8_t *data;
	int      stride;
	int      w;
	int      h;
} lens_src_t;

static bool
lens_src(cairo_surface_t *surface, lens_src_t *src)
{
	cairo_surface_flush(surface);
	src->data   = cairo_image_surface_get_data(surface);
	src->stride = cairo_image_surface_get_stride(surface);
	src->w      = cairo_image_surface_get_width(surface);
	src->h      = cairo_image_surface_get_height(surface);

	return src->data != NULL;
}

static void
pinch_profile(double *pinch, const lens_geom_t *g)
{
	double cxp       = g->cxp;
	double pinch_amp = g->pinch_amp;
	double pinch_r   = g->pw / 2.0 - 6.0;
	int    gw        = g->pw + 2;
	int    x;

	for (x = 0; x < gw; x++) {
		double lx = x - 1 + 0.5;
		double u  = pinch_r > 0.0 ? fabs(lx - cxp) / pinch_r : 1.0;
		double w  = 0.0;

		if (u < 1.0) {
			w = 1.0 - u * u;
			w = w * w;
		}
		pinch[x] = pinch_amp * w;
	}
}

static void
lens_fields(float *df, float *ddf, const double *pinch, const lens_geom_t *g,
            double bar_top, double bar_bot)
{
	double cxp  = g->cxp;
	double cyp  = g->cyp;
	double hw   = g->hw;
	double hh   = g->hh;
	double br   = g->br;
	double skew = g->skew;
	int    gw   = g->pw + 2;
	int    gh   = g->ph + 2;
	int    x;
	int    y;

	for (y = 0; y < gh; y++) {
		double ly = y - 1 + 0.5;

		for (x = 0; x < gw; x++) {
			double lx = x - 1 + 0.5 - skew * (ly - cyp);
			double dd = barny_sd_round_rect(lx - cxp, ly - cyp, hw, hh, br);
			double db = fmax(bar_top + pinch[x] - ly,
			                 ly - (bar_bot - pinch[x]));

			ddf[y * gw + x] = (float)dd;
			df[y * gw + x]  = (float)barny_smin(db, dd, BULGE_SMIN);
		}
	}
}

static inline void
lens_refract(const uint8_t *data, int stride, int w, int h, bool opaque,
             double sx, double sy, double nnx, double nny, double cs,
             double *fr, double *fg, double *fb, double *fa)
{
	/* Split the taps only where the dispersion is wide enough to land on
	   different pixels: it dies with pedge, so outside the droplet the
	   three taps were reading one pixel thrice. */
	if (cs > BULGE_CHROMA_MIN) {
		float c1, a1, c2, a2, c3, a3;

		lens_tap_channel_alpha(data, stride, w, h, sx - nnx * cs,
		                       sy - nny * cs, 2, &c1,
		                       opaque ? NULL : &a1);
		lens_tap_channel_alpha(data, stride, w, h, sx, sy, 1, &c2,
		                       opaque ? NULL : &a2);
		lens_tap_channel_alpha(data, stride, w, h, sx + nnx * cs,
		                       sy + nny * cs, 0, &c3,
		                       opaque ? NULL : &a3);
		if (opaque) {
			*fb = c3;
			*fg = c2;
			*fr = c1;
			*fa = 1.0;
		} else {
			*fb = a3 > 0 ? c3 * 255.0 / a3 : 0.0;
			*fg = a2 > 0 ? c2 * 255.0 / a2 : 0.0;
			*fr = a1 > 0 ? c1 * 255.0 / a1 : 0.0;
			*fa = a2 * (1.0 / 255.0);
		}
	} else {
		float p2[4];

		lens_tap(data, stride, w, h, sx, sy, p2);
		if (opaque) {
			*fb = p2[0];
			*fg = p2[1];
			*fr = p2[2];
			*fa = 1.0;
		} else {
			double unp = p2[3] > 0 ? 255.0 / p2[3] : 0.0;

			*fb = p2[0] * unp;
			*fg = p2[1] * unp;
			*fr = p2[2] * unp;
			*fa = p2[3] * (1.0 / 255.0);
		}
	}
}

static inline double
lens_top_light(const float *df, int gi, int gw, double depth, double edge_h,
               double inv_edge_h)
{
	double wtop = 0.0;
	double atop = 0.0;

	/* Only the upper-facing contour contributes lighting.
	   Reject the lower half before calculating its normal. */
	if (depth < edge_h) {
		double mny = df[gi + gw] - df[gi - gw];

		if (mny < 0.0) {
			double mnx  = df[gi + 1] - df[gi - 1];
			double mlen = sqrt(mnx * mnx + mny * mny);

			if (mlen > 1e-4)
				wtop = -mny / mlen;
		}
		atop = BARNY_FRAME_EDGE_TOP_A * (1.0 - depth * inv_edge_h)
		       * wtop;
	}

	return atop;
}

static inline void
droplet_shade(double dd, double facing, double atop, double ty,
              double strength, double spec_g, double *fr, double *fg,
              double *fb)
{
	double aw;
	double veil;
	double ri;
	double core;
	double rim;
	double lit;
	double band;
	double spec;
	double cntr;

	/* frosted brightness lift inside the bubble */
	veil = -dd / BULGE_VEIL_FADE;
	if (veil < 0.0)
		veil = 0.0;
	if (veil > 1.0)
		veil = 1.0;
	veil *= BULGE_VEIL * strength;
	aw    = veil + atop - veil * atop;

	/* crisp bright rim tracing the droplet, lit from the key light instead
	   of a uniform ring */
	lit  = glass_lit(facing);
	ri   = 1.0 + dd / BULGE_RIM_W;
	if (ri < 0.0 || dd > 0.0)
		ri = 0.0;
	ri   = ri * ri;
	core = 1.0 - fabs(dd) / BULGE_CORE_W;
	if (core < 0.0)
		core = 0.0;
	rim = (BULGE_RIM_STR * ri + BULGE_RIM_CORE * core * lit) * strength;
	if (rim > 1.0)
		rim = 1.0;

	/* specular: an arc hugging the rim where the droplet surface faces
	   the light, plus a faint cool counter-arc opposite it */
	spec = 0.0;
	cntr = 0.0;
	if (dd < 0.0 && -dd < SPEC_W && spec_g > 0.001) {
		band = glass_smoothstep01(1.0 + dd / SPEC_W);
		if (facing > 0.0)
			spec = spec_g * band * glass_pow10(facing);
		else
			cntr = SPEC_COUNTER * spec_g * band
			       * glass_pow10(-facing);
	}

	glass_tint(fr, fg, fb, aw, rim, ty, spec, cntr);
}

static inline double
prism_cov(double d)
{
	double t   = -d;
	double cov = t < PRISM_LINE_W - t ? t : PRISM_LINE_W - t;

	cov += 0.5;
	if (cov < 0.0)
		cov = 0.0;
	if (cov > 1.0)
		cov = 1.0;

	return cov;
}

static inline void
px_put(uint8_t *p, uint8_t b, uint8_t g, uint8_t r, uint8_t a)
{
	p[0] = b;
	p[1] = g;
	p[2] = r;
	p[3] = a;
}

/* Render the merged bar+droplet blob into a fully self-contained premultiplied
   ARGB patch. Coverage comes from the deformed distance field: the bar slab is
   pinched toward the droplet (surface tension) and smooth-unioned with the
   pill, so the bar silhouette really recedes around the lens. The interior
   refracts the clean bar strip (glass_clean); the frame highlight and spectral
   rim are re-derived along the deformed contour; the drop shadow
   (shadow_cache) shows through wherever the silhouette vacates.
   In authoritative mode the patch replaces the cached bar pixels wholesale
   (painted with SOURCE); otherwise it is a plain overlay for the
   squircle-corner zones the analytic field cannot reproduce.

   The returned surface is owned by the output and reused next frame; the
   caller must not destroy it. Every pixel is written, so it needs no clear. */
static cairo_surface_t *
build_lens_patch(barny_output_t *output, const lens_geom_t *g)
{
	barny_state_t   *state         = output->state;
	double           prism         = state->config.glass_prism;
	double           gleam         = state->config.glass_gleam;
	double           strength      = g->s;
	double           disp          = BULGE_REFRACT * g->s;
	double           chroma        = BULGE_CHROMA * g->s;
	double           spec_g        = gleam * SPEC_GAIN * strength;
	bool             opaque        = output->glass_clean_opaque;
	bool             authoritative = g->authoritative;
	double           cx            = output->pad_left;
	double           bar_top       = g->over;
	double           bar_bot       = g->over + output->height;
	double           edge_h        = BARNY_FRAME_EDGE_TOP_STOP
	                                 * (bar_bot - bar_top);
	double           hh            = g->hh;
	double           cyp           = g->cyp;
	int              sox           = g->x0;
	int              soy           = g->y0;
	int              pw            = g->pw;
	int              ph            = g->ph;
	int              gw            = pw + 2;
	int              gh            = ph + 2;
	cairo_surface_t *dst;
	lens_src_t       gsrc;
	lens_src_t       hsrc;
	lens_src_t       bsrc;
	lens_src_t       dsrc;
	float           *df;
	float           *ddf;
	double          *pinch;
	double          *prm;
	/* Denominators fixed for the whole patch. IEEE rules stop the compiler
	   from folding x/k into x*(1/k) on its own, so each pixel was paying
	   several ~20-cycle divides for constants. */
	double           inv_hh;
	double           inv_edge_h;
	int              x;
	int              y;

	if (!output->glass_clean || !output->shadow_cache || !output->bg_cache)
		return NULL;

	if (!lens_scratch(output, pw, ph))
		return NULL;

	dst   = output->lens_patch;
	df    = output->lens_field;
	ddf   = df + (size_t)gw * gh;
	pinch = output->lens_cols;
	prm   = output->lens_cols + gw;

	if (!lens_src(dst, &dsrc) || !lens_src(output->glass_clean, &gsrc)
	    || !lens_src(output->shadow_cache, &hsrc)
	    || !lens_src(output->bg_cache, &bsrc))
		return NULL;

	inv_hh     = hh > 0.0 ? 1.0 / hh : 0.0;
	inv_edge_h = edge_h > 0.0 ? 1.0 / edge_h : 0.0;

	prism_row(prm, pw, sox, cx, output->width);
	pinch_profile(pinch, g);
	lens_fields(df, ddf, pinch, g, bar_top, bar_bot);

	for (y = 0; y < ph; y++) {
		uint8_t *drow = dsrc.data + y * dsrc.stride;
		int      sy   = y + soy;
		uint8_t *hrow = sy >= 0 && sy < bsrc.h
		                        ? hsrc.data + sy * hsrc.stride
		                        : NULL;
		uint8_t *brow = sy >= 0 && sy < bsrc.h
		                        ? bsrc.data + sy * bsrc.stride
		                        : NULL;

		for (x = 0; x < pw; x++) {
			int    gi  = (y + 1) * gw + (x + 1);
			double d   = df[gi];
			double dd  = ddf[gi];
			double ly  = y + 0.5;
			int    sx  = x + sox;
			bool   inb = brow && sx >= 0 && sx < bsrc.w;
			double aa;
			double cpr;
			double cpg;
			double cpb;
			double ap;
			double csr;
			double csg;
			double csb;
			double as;
			double outr;
			double outg;
			double outb;
			double outa;
			double pa;

			/* untouched by droplet and pinch: bar cache verbatim */
			if (dd > LENS_FAR_MARGIN && pinch[x + 1] < 0.004) {
				if (authoritative && inb)
					px_put(drow + x * 4, brow[sx * 4 + 0],
					       brow[sx * 4 + 1],
					       brow[sx * 4 + 2],
					       brow[sx * 4 + 3]);
				else
					px_put(drow + x * 4, 0, 0, 0, 0);
				continue;
			}

			aa = 0.5 - d;
			if (aa < 0.0)
				aa = 0.0;
			if (aa > 1.0)
				aa = 1.0;

			csr = csg = csb = as = 0.0;
			if (authoritative && inb && hrow) {
				csb = hrow[sx * 4 + 0];
				csg = hrow[sx * 4 + 1];
				csr = hrow[sx * 4 + 2];
				as  = hrow[sx * 4 + 3];
			}

			cpr = cpg = cpb = 0.0;
			ap  = 0.0;
			if (aa > 0.0) {
				double nnx    = 0.0;
				double nny    = 0.0;
				double pedge  = 0.0;
				double dispx  = 0.0;
				double dispy  = 0.0;
				double gx     = (double)sx + 0.5 - cx;
				double gy     = ly;
				double depth  = -d;
				double facing = 0.0;
				double cs     = 0.0;
				double fr;
				double fg;
				double fb;
				double fa;
				double atop;
				double ty;

				/* Plano-convex lens: sample the strip pulled
				   inward along the droplet normal. The interior
				   magnifies; the rim compresses the background
				   into a bright refractive ring. Offset -> 0 at
				   the outline, so the bubble blends seamlessly
				   into the bar.

				   Past BULGE_CORE_W outside the pill the normal
				   has no consumer left -- refraction and
				   dispersion are zero there, and both rim terms
				   have faded -- so the gradient and its sqrt are
				   skipped for the whole bar stretch of the
				   patch. */
				if (dd < BULGE_CORE_W) {
					double nx   = ddf[gi + 1] - ddf[gi - 1];
					double ny   = ddf[gi + gw] - ddf[gi - gw];
					double nlen = sqrt(nx * nx + ny * ny);

					if (nlen > 1e-6) {
						nnx = nx / nlen;
						nny = ny / nlen;
					}

					if (dd < 0.0) {
						double ddep = -dd;
						double zone = hh;
						double p    = zone > 0.0
						                      ? ddep * inv_hh
						                      : 1.0;
						double srcoff;

						if (p > 1.0)
							p = 1.0;
						srcoff = glass_dome(p, zone)
						         * disp
						         + BULGE_BIAS
						                   * (1.0 - p);
						dispx  = -nnx * srcoff;
						dispy  = -nny * srcoff;
						pedge  = (1.0 - p) * (1.0 - p);
					}

					facing = nnx * GLASS_SPEC_LX
					         + nny * GLASS_SPEC_LY;
					cs     = chroma * pedge;
				}

				lens_refract(gsrc.data, gsrc.stride, gsrc.w,
				             gsrc.h, opaque, gx + dispx,
				             gy + dispy, nnx, nny, cs, &fr, &fg,
				             &fb, &fa);

				atop = lens_top_light(df, gi, gw, depth, edge_h,
				                      inv_edge_h);

				ty = (ly - cyp) * inv_hh;
				if (ty < -1.0)
					ty = -1.0;
				if (ty > 1.0)
					ty = 1.0;

				droplet_shade(dd, facing, atop, ty, strength,
				              spec_g, &fr, &fg, &fb);

				ap  = fa * aa;
				cpr = fr * ap;
				cpg = fg * ap;
				cpb = fb * ap;
			}

			outb = cpb + csb * (1.0 - ap);
			outg = cpg + csg * (1.0 - ap);
			outr = cpr + csr * (1.0 - ap);
			outa = 255.0 * ap + as * (1.0 - ap);

			/* spectral rim: additive stroke tracing the deformed
			   contour, matching the baked stroke on the straight
			   stretches */
			pa = prism > 0.001 ? prism * prism_cov(d) : 0.0;
			if (!authoritative)
				pa *= aa;
			if (pa > 0.0) {
				outr += prm[x * 3 + 0] * 255.0 * pa;
				outg += prm[x * 3 + 1] * 255.0 * pa;
				outb += prm[x * 3 + 2] * 255.0 * pa;
				outa += 255.0 * pa;
			}
			if (outa > 255.0)
				outa = 255.0;
			if (outr > outa)
				outr = outa;
			if (outg > outa)
				outg = outa;
			if (outb > outa)
				outb = outa;

			px_put(drow + x * 4, (uint8_t)outb, (uint8_t)outg,
			       (uint8_t)outr, (uint8_t)outa);
		}
	}

	cairo_surface_mark_dirty(dst);

	return dst;
}

/* Advance the lens springs by one frame. The pill position chases the
   pointer with an underdamped spring (droplet inertia); the pop scale
   grows/dissolves the droplet on bar enter/leave. Returns true while the
   springs are still in motion, so the caller keeps the frame loop alive. */
bool
barny_lens_step(barny_output_t *output)
{
	barny_state_t *state = output->state;
	uint64_t       now;
	double         dt;
	double         tx;
	double         bmin;
	double         bmax;

	if (!state->config.dynamic_glass || output != state->dyn_output
	    || !state->lens_animating)
		return false;

	now                 = barny_now_us();
	dt                  = (double)(now - state->lens_prev_us) / 1000000.0;
	state->lens_prev_us = now;
	bmin = output->pad_left + BUBBLE_W / 2.0;
	bmax = output->pad_left + output->width - BUBBLE_W / 2.0;
	if (bmax < bmin)
		bmax = bmin;

	tx = state->pointer_output == output ? state->pointer_x
	                                     : state->lens_x;
	if (tx < bmin)
		tx = bmin;
	if (tx > bmax)
		tx = bmax;

	/* The analytic spring remains on the same motion curve when a compositor
	   frame is delayed; there is no large Euler step to overshoot the pointer. */
	barny_spring_step(&state->lens_x, &state->lens_vx, tx,
	                  LENS_SPRING_K, LENS_SPRING_ZETA, dt);
	barny_spring_step(&state->lens_scale, &state->lens_sv,
	                  state->lens_target_scale, LENS_POP_K, 1.0, dt);
	if (state->lens_scale < 0.0)
		state->lens_scale = 0.0;
	if (state->lens_scale > 1.0)
		state->lens_scale = 1.0;

	if (fabs(tx - state->lens_x) < LENS_SETTLE_X
	    && fabs(state->lens_vx) < LENS_SETTLE_V
	    && fabs(state->lens_target_scale - state->lens_scale) < 0.01
	    && fabs(state->lens_sv) < 0.1) {
		state->lens_x         = tx;
		state->lens_vx        = 0.0;
		state->lens_scale     = state->lens_target_scale;
		state->lens_sv        = 0.0;
		state->lens_animating = false;
	}

	return state->lens_animating;
}

/* Everything the droplet's placement implies for this frame. Both the renderer
   and the frame loop need it: the renderer to build the patch, the frame loop
   to know which strip of the bar the droplet dirtied (so it can repaint just
   that strip). Returns false when no droplet is drawn. */
static bool
lens_geom(barny_output_t *output, lens_geom_t *g)
{
	barny_state_t *state  = output->state;
	int            radius = state->config.border_radius;
	double         cx     = output->pad_left;
	double         cy     = output->pad_top;
	double         cw     = output->width;
	double         ch     = output->height;
	double         s      = state->lens_scale;
	double         bulge  = state->config.glass_bulge;
	double         over;
	double         bw;
	double         bh;
	double         stretch;
	double         bcx;
	double         straight_l;
	double         straight_r;
	double         margin;
	double         corner_ramp;

	/* Deliberately independent of the bar caches: the frame loop asks for
	   this rect before the first render has built them, and it must still
	   learn where the droplet landed, or the next frame will not know to
	   wipe it. build_lens_patch does the cache null-check. */
	if (!state->config.dynamic_glass)
		return false;
	if (output != state->dyn_output)
		return false;
	if (s < 0.005 || bulge <= 0.1)
		return false;

	over       = state->config.position_top ? output->pad_top
	                                        : output->pad_bottom;
	bw         = BUBBLE_W;
	bh         = ch + 2 * over;
	stretch    = 1.0
	             + fmin(LENS_STRETCH_GAIN * fabs(state->lens_vx),
	                    LENS_STRETCH_MAX);

	g->s       = s;
	g->over    = over;
	g->hw      = bw / 2.0 * stretch * s;
	g->hh      = bh / 2.0 * (1.0 - 0.55 * (stretch - 1.0)) * s;
	g->br      = g->hh > g->hw ? g->hw : g->hh;
	g->pw      = (int)(bw * (1.0 + LENS_STRETCH_MAX)) + 2 * BULGE_NECK;
	g->ph      = (int)bh;

	bcx        = state->lens_x;
	if (bcx < cx + bw / 2.0)
		bcx = cx + bw / 2.0;
	if (bcx > cx + cw - bw / 2.0)
		bcx = cx + cw - bw / 2.0;

	g->x0      = (int)(bcx - g->pw / 2.0);
	g->y0      = (int)(cy - over);
	g->cxp     = bcx - g->x0;
	g->cyp     = g->ph / 2.0;

	/* The analytic field cannot reproduce the squircle corners, so the
	   SOURCE rewrite is only allowed on the straight stretch; near the
	   corners the pinch dies off and the patch degrades to a plain
	   overlay. */
	straight_l       = cx + radius;
	straight_r       = cx + cw - radius;
	g->authoritative = g->x0 >= straight_l && g->x0 + g->pw <= straight_r;

	margin           = fmin(g->x0 - straight_l,
	                        straight_r - (g->x0 + g->pw));
	if (margin < 0.0)
		margin = 0.0;
	corner_ramp   = margin >= LENS_PINCH_FADE ? 1.0
	                                          : margin / LENS_PINCH_FADE;
	corner_ramp   = glass_smoothstep01(corner_ramp);
	g->pinch_amp  = LENS_PINCH_MAX * (bulge / 15.0) * s * corner_ramp;
	if (g->pinch_amp > ch * 0.2)
		g->pinch_amp = ch * 0.2;

	g->skew = LENS_SKEW_GAIN * state->lens_vx;
	if (g->skew > LENS_SKEW_MAX)
		g->skew = LENS_SKEW_MAX;
	if (g->skew < -LENS_SKEW_MAX)
		g->skew = -LENS_SKEW_MAX;

	return true;
}

bool
barny_lens_rect(barny_output_t *output, int *x, int *y, int *w, int *h)
{
	lens_geom_t g;

	if (!lens_geom(output, &g))
		return false;

	*x = g.x0;
	*y = g.y0;
	*w = g.pw;
	*h = g.ph;

	return true;
}

static void
render_dynamic(barny_output_t *output, cairo_t *cr)
{
	barny_state_t   *state  = output->state;
	int              radius = state->config.border_radius;
	double           cx     = output->pad_left;
	double           cy     = output->pad_top;
	double           cw     = output->width;
	double           ch     = output->height;
	lens_geom_t      g;
	cairo_surface_t *patch;

	if (!lens_geom(output, &g))
		return;

	patch = build_lens_patch(output, &g);
	if (!patch)
		return;

	cairo_save(cr);
	if (g.authoritative) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_rectangle(cr, g.x0, g.y0, g.pw, g.ph);
		cairo_clip(cr);
	} else {
		barny_rounded_rect_path(cr, cx, cy - g.over, cw,
		                        ch + 2 * g.over, radius);
		cairo_clip(cr);
	}
	cairo_set_source_surface(cr, patch, g.x0, g.y0);
	cairo_paint(cr);
	cairo_restore(cr);
}

static cairo_surface_t *
create_bar_shadow(int cx, int cy, int cw, int ch, int radius, int surf_w,
                  int surf_h)
{
	cairo_surface_t *shadow;
	cairo_t         *sc;

	shadow = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surf_w, surf_h);
	if (cairo_surface_status(shadow) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(shadow);
		return NULL;
	}

	sc = cairo_create(shadow);
	barny_rounded_rect_path(sc, cx, cy + 3, cw, ch, radius);
	cairo_set_source_rgba(sc, 0, 0, 0, 0.78);
	cairo_fill(sc);
	cairo_destroy(sc);

	barny_blur_surface(shadow, 12);

	return shadow;
}

static bool
surface_is_opaque(cairo_surface_t *surface)
{
	uint8_t *data;
	int      width;
	int      height;
	int      stride;
	int      x;
	int      y;

	if (!surface
	    || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE
	    || cairo_image_surface_get_format(surface) != CAIRO_FORMAT_ARGB32)
		return false;

	cairo_surface_flush(surface);
	data   = cairo_image_surface_get_data(surface);
	width  = cairo_image_surface_get_width(surface);
	height = cairo_image_surface_get_height(surface);
	stride = cairo_image_surface_get_stride(surface);
	if (!data)
		return false;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			if (data[y * stride + x * 4 + 3] != 255)
				return false;
		}
	}

	return true;
}

/* The broad part of the glass frame lighting: the full-height gradient with
   the contour-keyed edge component removed (see FRAME_TOP_EDGE_*) plus the
   diagonal sheen. Baked into glass_clean so the lens patch can re-derive the
   edge lighting along the deformed contour without double-counting. */
void
barny_draw_broad_frame(cairo_t *cr, double w, double h)
{
	cairo_pattern_t *p;

	p = cairo_pattern_create_linear(0, 0, 0, h);
	cairo_pattern_add_color_stop_rgba(p, 0.00, 1, 1, 1, 0.05);
	cairo_pattern_add_color_stop_rgba(p, 0.14, 1, 1, 1, 0.05);
	cairo_pattern_add_color_stop_rgba(p, 0.55, 1, 1, 1, 0.022);
	cairo_pattern_add_color_stop_rgba(p, 1.00, 1, 1, 1, 0.035);
	cairo_set_source(cr, p);
	cairo_paint(cr);
	cairo_pattern_destroy(p);

	p = cairo_pattern_create_linear(0, 0, w * 0.55, h);
	cairo_pattern_add_color_stop_rgba(p, 0.0, 1, 1, 1, 0.10);
	cairo_pattern_add_color_stop_rgba(p, 0.4, 1, 1, 1, 0.0);
	cairo_set_source(cr, p);
	cairo_paint(cr);
	cairo_pattern_destroy(p);
}

static void
draw_spectral_rim(cairo_t *cr, double w, double h, double r, double strength)
{
	cairo_pattern_t *p;
	int              i;

	if (strength <= 0.001)
		return;

	prism_stops_init();
	p = cairo_pattern_create_linear(0, 0, w, 0);
	for (i = 0; i <= PRISM_STOPS; i++) {
		const double *c = prism_stop[i % PRISM_HUES];

		cairo_pattern_add_color_stop_rgba(p, (double)i / PRISM_STOPS,
		                                  c[0], c[1], c[2], strength);
	}

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
	barny_rounded_rect_path(cr, PRISM_LINE_W / 2, PRISM_LINE_W / 2,
	                        w - PRISM_LINE_W, h - PRISM_LINE_W,
	                        r - PRISM_LINE_W / 4);
	cairo_set_source(cr, p);
	cairo_set_line_width(cr, PRISM_LINE_W);
	cairo_stroke(cr);
	cairo_restore(cr);

	cairo_pattern_destroy(p);
}

/* The clipped shadow is kept as its own cache: it is what shows through
   wherever the lens pinches the bar silhouette away. */
static void
build_shadow_cache(barny_output_t *output)
{
	barny_state_t   *state = output->state;
	int              sw    = output->surf_width;
	int              sh    = output->surf_height;
	int              cy    = output->pad_top;
	int              ch    = output->height;
	cairo_surface_t *shadow;
	cairo_t         *sc;

	if (output->shadow_cache) {
		cairo_surface_destroy(output->shadow_cache);
		output->shadow_cache = NULL;
	}
	shadow = create_bar_shadow(output->pad_left, cy, output->width, ch,
	                           state->config.border_radius, sw, sh);
	if (!shadow)
		return;

	output->shadow_cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                                  sw, sh);
	sc                   = cairo_create(output->shadow_cache);
	if (state->config.position_top)
		cairo_rectangle(sc, 0, cy, sw, sh - cy);
	else
		cairo_rectangle(sc, 0, 0, sw, cy + ch);
	cairo_clip(sc);
	cairo_set_source_surface(sc, shadow, 0, 0);
	cairo_paint(sc);
	cairo_destroy(sc);
	cairo_surface_destroy(shadow);
}

static cairo_surface_t *
bar_lens_map(barny_output_t *output, int cw, int ch)
{
	cairo_surface_t *map = output->lens_map;

	if (map && cairo_image_surface_get_width(map) == cw
	    && cairo_image_surface_get_height(map) == ch)
		return map;

	if (map)
		cairo_surface_destroy(map);
	output->lens_map = barny_create_edge_lens_map(
	        cw, ch, output->state->config.border_radius, BAR_LENS_EDGE,
	        BAR_LENS_DISP);

	return output->lens_map;
}

static cairo_surface_t *
build_lensed_strip(barny_output_t *output)
{
	barny_state_t   *state = output->state;
	int              cw    = output->width;
	int              ch    = output->height;
	cairo_surface_t *src;
	cairo_surface_t *map;
	cairo_surface_t *lensed;
	cairo_t         *sc;

	src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
	sc  = cairo_create(src);
	barny_paint_glass_bg(sc, state->blurred_wallpaper, cw, ch, 0, 0, ch,
	                     state->config.position_top);
	cairo_destroy(sc);

	map    = bar_lens_map(output, cw, ch);
	lensed = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
	if (map) {
		barny_apply_displacement(src, lensed, map, BAR_LENS_DISP,
		                         BAR_LENS_CHROMA);
	} else {
		sc = cairo_create(lensed);
		cairo_set_source_surface(sc, src, 0, 0);
		cairo_paint(sc);
		cairo_destroy(sc);
	}
	cairo_surface_destroy(src);

	return lensed;
}

static void
paint_bar_glass(cairo_t *cr, cairo_surface_t *lensed, int cw, int ch,
                int radius)
{
	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, cw, ch, radius);
	cairo_clip(cr);
	cairo_set_source_surface(cr, lensed, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	barny_draw_glass_frame(cr, cw, ch, radius);
}

/* Clean strip for the lens patch: bar interior + broad lighting only
   (no contour-keyed edge light, no spectral rim, no rounded clip);
   the overrun rows pad-extend the edge rows for the droplet caps. */
static void
build_glass_clean(barny_output_t *output, cairo_surface_t *lensed)
{
	int              cw   = output->width;
	int              ch   = output->height;
	int              over = output->state->config.position_top
	                                ? output->pad_top
	                                : output->pad_bottom;
	cairo_surface_t *strip;
	cairo_t         *sc;

	if (output->glass_clean) {
		cairo_surface_destroy(output->glass_clean);
		output->glass_clean = NULL;
	}
	output->glass_clean_opaque = false;
	strip = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
	sc    = cairo_create(strip);
	cairo_set_source_surface(sc, lensed, 0, 0);
	cairo_paint(sc);
	barny_draw_broad_frame(sc, cw, ch);
	cairo_destroy(sc);

	output->glass_clean = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                                 cw, ch + 2 * over);
	sc                  = cairo_create(output->glass_clean);
	cairo_set_source_surface(sc, strip, 0, over);
	cairo_pattern_set_extend(cairo_get_source(sc), CAIRO_EXTEND_PAD);
	cairo_paint(sc);
	cairo_destroy(sc);
	output->glass_clean_opaque = surface_is_opaque(output->glass_clean);
	cairo_surface_destroy(strip);
}

static cairo_surface_t *
build_glass_bg(barny_output_t *output)
{
	barny_state_t   *state  = output->state;
	int              radius = state->config.border_radius;
	int              cw     = output->width;
	int              ch     = output->height;
	cairo_surface_t *cache;
	cairo_surface_t *lensed;
	cairo_t         *cr;

	cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                   output->surf_width,
	                                   output->surf_height);
	cr    = cairo_create(cache);

	build_shadow_cache(output);
	if (output->shadow_cache) {
		cairo_set_source_surface(cr, output->shadow_cache, 0, 0);
		cairo_paint(cr);
	}

	lensed = build_lensed_strip(output);

	cairo_translate(cr, output->pad_left, output->pad_top);
	paint_bar_glass(cr, lensed, cw, ch, radius);
	draw_spectral_rim(cr, cw, ch, radius, state->config.glass_prism);
	cairo_destroy(cr);

	build_glass_clean(output, lensed);
	cairo_surface_destroy(lensed);

	return cache;
}

void
barny_render_liquid_glass(barny_output_t *output, cairo_t *cr)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	if (!output->bg_cache) {
		output->bg_cache = build_glass_bg(output);
	}

	if (output->bg_cache) {
		cairo_set_source_surface(cr, output->bg_cache, 0, 0);
		cairo_paint(cr);
	}

	render_dynamic(output, cr);
}

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

static cairo_surface_t *
load_jpeg(const char *path)
{
	FILE                         *f;
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr_ext     jerr;
	int                           width;
	int                           height;
	int                           row_stride;
	cairo_surface_t              *surface;
	uint8_t                      *data;
	int                           cairo_stride;
	JSAMPARRAY                    buffer;
	int                           x;
	int                           y;
	uint8_t                      *dst_row;
	uint8_t                      *src_row;

	f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}

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

	cinfo.out_color_space = JCS_RGB;
	jpeg_start_decompress(&cinfo);

	width      = cinfo.output_width;
	height     = cinfo.output_height;
	row_stride = cinfo.output_width * cinfo.output_components;

	surface    = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		jpeg_finish_decompress(&cinfo);
		jpeg_destroy_decompress(&cinfo);
		fclose(f);
		return NULL;
	}

	cairo_surface_flush(surface);
	data         = cairo_image_surface_get_data(surface);
	cairo_stride = cairo_image_surface_get_stride(surface);

	buffer       = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE,
	                                          row_stride, 1);

	while (cinfo.output_scanline < cinfo.output_height) {
		y = cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, buffer, 1);

		dst_row = data + y * cairo_stride;
		src_row = buffer[0];

		for (x = 0; x < width; x++) {
			dst_row[x * 4 + 0] = src_row[x * 3 + 2];
			dst_row[x * 4 + 1] = src_row[x * 3 + 1];
			dst_row[x * 4 + 2] = src_row[x * 3 + 0];
			dst_row[x * 4 + 3] = 255;
		}
	}

	cairo_surface_mark_dirty(surface);

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(f);

	return surface;
}

static int
has_extension(const char *path, const char *ext)
{
	size_t      path_len = strlen(path);
	size_t      ext_len  = strlen(ext);
	const char *path_ext;
	size_t      i;

	if (path_len < ext_len)
		return 0;

	path_ext = path + path_len - ext_len;
	for (i = 0; i < ext_len; i++) {
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

	if (has_extension(path, ".jpg") || has_extension(path, ".jpeg")) {
		surface = load_jpeg(path);
		if (surface) {
			printf("barny: loaded JPEG wallpaper: %s\n", path);
			return surface;
		}
	}

	surface = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
		printf("barny: loaded PNG wallpaper: %s\n", path);
		return surface;
	}
	cairo_surface_destroy(surface);

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
