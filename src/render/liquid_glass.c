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
#define BULGE_RIM_CHROMA 0.28  /* vertical colour dispersion of the rim */

#define LENS_SPRING_K    420.0 /* position spring stiffness, s^-2 */
#define LENS_SPRING_ZETA 0.82  /* underdamped: slight droplet overshoot */
#define LENS_POP_K       900.0 /* enter/leave pop spring, critically damped */
#define LENS_DT_MAX      0.05  /* integration step cap, s */
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
#define PRISM_CYCLES 2.5

/* Key light for the droplet specular: from above, slightly left, matching
   the diagonal sheen of the glass frame. */
#define SPEC_LX      (-0.30)
#define SPEC_LY      (-0.954)
#define SPEC_W       7.0   /* px band the specular hugs inside the rim */
#define SPEC_P       10.0  /* highlight tightness */
#define SPEC_GAIN    2.4   /* glass_gleam multiplier */
#define SPEC_COUNTER 0.22  /* strength of the cool counter-arc */

static void
hsv2rgb(double hh, double s, double v, double *r, double *g, double *b);

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
barny_apply_brightness(cairo_surface_t *surface, double factor)
{
	int      width;
	int      height;
	int      stride;
	uint8_t *data;
	uint8_t *row;
	int      x;
	int      y;
	int      b;
	int      g;
	int      r;

	cairo_surface_flush(surface);

	width  = cairo_image_surface_get_width(surface);
	height = cairo_image_surface_get_height(surface);
	stride = cairo_image_surface_get_stride(surface);
	data   = cairo_image_surface_get_data(surface);

	for (y = 0; y < height; y++) {
		row = data + y * stride;
		for (x = 0; x < width; x++) {
			b              = row[x * 4 + 0];
			g              = row[x * 4 + 1];
			r              = row[x * 4 + 2];

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
barny_sample_bilinear(uint8_t *data, int stride, int width, int height, double x,
                double y, uint8_t *out)
{
	int      x0;
	int      y0;
	int      x1;
	int      y1;
	double   fx;
	double   fy;
	uint8_t *p00;
	uint8_t *p10;
	uint8_t *p01;
	uint8_t *p11;
	int      i;
	double   v00;
	double   v10;
	double   v01;
	double   v11;
	double   v0;
	double   v1;
	double   v;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= width - 1)
		x = width - 1.001;
	if (y >= height - 1)
		y = height - 1.001;

	x0  = (int)x;
	y0  = (int)y;
	x1  = x0 + 1;
	y1  = y0 + 1;

	fx  = x - x0;
	fy  = y - y0;

	p00 = data + y0 * stride + x0 * 4;
	p10 = data + y0 * stride + x1 * 4;
	p01 = data + y1 * stride + x0 * 4;
	p11 = data + y1 * stride + x1 * 4;

	for (i = 0; i < 4; i++) {
		v00    = p00[i];
		v10    = p10[i];
		v01    = p01[i];
		v11    = p11[i];

		v0     = v00 + (v10 - v00) * fx;
		v1     = v01 + (v11 - v01) * fx;
		v      = v0 + (v1 - v0) * fy;

		out[i] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
	}
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

double
barny_sd_round_rect(double px, double py, double hw, double hh, double r)
{
	double qx      = fabs(px) - (hw - r);
	double qy      = fabs(py) - (hh - r);
	double ax      = qx > 0 ? qx : 0;
	double ay      = qy > 0 ? qy : 0;
	double outside = sqrt(ax * ax + ay * ay);
	double mq      = qx > qy ? qx : qy;
	double inside  = mq < 0 ? mq : 0;

	return outside + inside - r;
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

/* Polynomial smooth-minimum: blends two signed distances so the union of two
   shapes grows a concave "surface tension" fillet instead of a hard corner. */
double
barny_smin(double a, double b, double k)
{
	double h;

	if (k <= 0.0)
		return a < b ? a : b;

	h = 0.5 + 0.5 * (b - a) / k;
	if (h < 0.0)
		h = 0.0;
	if (h > 1.0)
		h = 1.0;

	return (b * (1.0 - h) + a * h) - k * h * (1.0 - h);
}

/* Render the merged bar+droplet blob into a fully self-contained premultiplied
   ARGB patch. Coverage comes from the deformed distance field: the bar slab is
   pinched toward the droplet (surface tension) and smooth-unioned with the
   pill, so the bar silhouette really recedes around the lens. The interior
   refracts the clean bar strip (glass_clean); the frame highlight, bottom
   shadow band and spectral rim are re-derived along the deformed contour; the
   drop shadow (shadow_cache) shows through wherever the silhouette vacates.
   In authoritative mode the patch replaces the cached bar pixels wholesale
   (painted with SOURCE); otherwise it is a plain overlay for the
   squircle-corner zones the analytic field cannot reproduce. */
static cairo_surface_t *
build_lens_patch(barny_output_t *output, int sox, int soy, int pw, int ph,
                 double cxp, double cyp, double hw, double hh, double br,
                 double skew, double bar_top, double bar_bot, double disp,
                 double chroma, double strength, double pinch_amp,
                 bool authoritative)
{
	barny_state_t   *state   = output->state;
	double           prism   = state->config.glass_prism;
	double           gleam   = state->config.glass_gleam;
	double           spec_g  = gleam * SPEC_GAIN * strength;
	double           cx      = output->pad_left;
	double           cw      = output->width;
	double           chh     = bar_bot - bar_top;
	double           edge_h  = BARNY_FRAME_EDGE_TOP_STOP * chh;
	double           shad_h  = chh * 0.28 > 10.0 ? 10.0 : chh * 0.28;
	double           pinch_r = pw / 2.0 - 6.0;
	int              gw      = pw + 2;
	int              gh      = ph + 2;
	cairo_surface_t *dst;
	uint8_t         *gdata;
	uint8_t         *hdata;
	uint8_t         *bdata;
	uint8_t         *ddata;
	int              gstride;
	int              hstride;
	int              bstride;
	int              dstride;
	int              gsw;
	int              gsh;
	int              bsw;
	int              bsh;
	float           *df;
	float           *ddf;
	double          *cols;
	double          *pinch;
	double          *prm;
	double           stop_r[PRISM_STOPS + 1];
	double           stop_g[PRISM_STOPS + 1];
	double           stop_b[PRISM_STOPS + 1];
	int              x;
	int              y;
	int              i;

	if (!output->glass_clean || !output->shadow_cache || !output->bg_cache)
		return NULL;

	dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(dst);
		return NULL;
	}

	df   = malloc(sizeof(float) * (size_t)gw * gh * 2);
	cols = malloc(sizeof(double) * ((size_t)gw + (size_t)pw * 3));
	if (!df || !cols) {
		free(df);
		free(cols);
		cairo_surface_destroy(dst);
		return NULL;
	}
	ddf   = df + (size_t)gw * gh;
	pinch = cols;
	prm   = cols + gw;

	cairo_surface_flush(output->glass_clean);
	cairo_surface_flush(output->shadow_cache);
	cairo_surface_flush(output->bg_cache);
	gdata   = cairo_image_surface_get_data(output->glass_clean);
	gstride = cairo_image_surface_get_stride(output->glass_clean);
	gsw     = cairo_image_surface_get_width(output->glass_clean);
	gsh     = cairo_image_surface_get_height(output->glass_clean);
	hdata   = cairo_image_surface_get_data(output->shadow_cache);
	hstride = cairo_image_surface_get_stride(output->shadow_cache);
	bdata   = cairo_image_surface_get_data(output->bg_cache);
	bstride = cairo_image_surface_get_stride(output->bg_cache);
	bsw     = cairo_image_surface_get_width(output->bg_cache);
	bsh     = cairo_image_surface_get_height(output->bg_cache);
	ddata   = cairo_image_surface_get_data(dst);
	dstride = cairo_image_surface_get_stride(dst);

	/* replicate the baked stroke's 40-stop rainbow so the analytic rim
	   matches it exactly at the patch borders */
	for (i = 0; i <= PRISM_STOPS; i++) {
		double hue = fmod((double)i / PRISM_STOPS * PRISM_CYCLES, 1.0);

		hsv2rgb(hue, 0.9, 1.0, &stop_r[i], &stop_g[i], &stop_b[i]);
	}
	for (x = 0; x < pw; x++) {
		double pos = ((double)(x + sox) + 0.5 - cx) / cw;
		double f;
		int    si;

		if (pos < 0.0)
			pos = 0.0;
		if (pos > 1.0)
			pos = 1.0;
		f  = pos * PRISM_STOPS;
		si = (int)f;
		if (si >= PRISM_STOPS)
			si = PRISM_STOPS - 1;
		f             -= si;
		prm[x * 3 + 0] = stop_r[si] + (stop_r[si + 1] - stop_r[si]) * f;
		prm[x * 3 + 1] = stop_g[si] + (stop_g[si + 1] - stop_g[si]) * f;
		prm[x * 3 + 2] = stop_b[si] + (stop_b[si + 1] - stop_b[si]) * f;
	}

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

	for (y = 0; y < ph; y++) {
		uint8_t *drow = ddata + y * dstride;
		int      sy   = y + soy;
		uint8_t *hrow = sy >= 0 && sy < bsh ? hdata + sy * hstride
		                                    : NULL;
		uint8_t *brow = sy >= 0 && sy < bsh ? bdata + sy * bstride
		                                    : NULL;

		for (x = 0; x < pw; x++) {
			int    gi  = (y + 1) * gw + (x + 1);
			double d   = df[gi];
			double dd  = ddf[gi];
			double ly  = y + 0.5;
			int    sx  = x + sox;
			bool   inb = brow && sx >= 0 && sx < bsw;
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
			double t;
			double cov;
			double pa;

			/* untouched by droplet and pinch: bar cache verbatim */
			if (dd > LENS_FAR_MARGIN && pinch[x + 1] < 0.004) {
				if (authoritative && inb) {
					drow[x * 4 + 0] = brow[sx * 4 + 0];
					drow[x * 4 + 1] = brow[sx * 4 + 1];
					drow[x * 4 + 2] = brow[sx * 4 + 2];
					drow[x * 4 + 3] = brow[sx * 4 + 3];
				} else {
					drow[x * 4 + 0] = 0;
					drow[x * 4 + 1] = 0;
					drow[x * 4 + 2] = 0;
					drow[x * 4 + 3] = 0;
				}
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
				double  nnx   = 0.0;
				double  nny   = 0.0;
				double  pedge = 0.0;
				double  dispx = 0.0;
				double  dispy = 0.0;
				double  gx    = (double)sx + 0.5 - cx;
				double  gy    = ly;
				double  fr;
				double  fg;
				double  fb;
				double  fa;
				double  mnx;
				double  mny;
				double  mlen;
				double  depth = -d;
				double  wtop  = 0.0;
				double  wbot  = 0.0;
				double  atop  = 0.0;
				double  abot  = 0.0;
				double  aw;
				double  veil;
				double  ri;
				double  core;
				double  rim;
				double  ty;
				double  facing;
				double  lit;
				double  band;
				double  spec;
				double  cntr;
				uint8_t p1[4];
				uint8_t p2[4];
				uint8_t p3[4];

				/* Plano-convex lens: sample the strip pulled
				   inward along the droplet normal. The interior
				   magnifies; the rim compresses the background
				   into a bright refractive ring. Offset -> 0 at
				   the outline, so the bubble blends seamlessly
				   into the bar. */
				{
					double nx   = ddf[gi + 1] - ddf[gi - 1];
					double ny   = ddf[gi + gw] - ddf[gi - gw];
					double nlen = sqrt(nx * nx + ny * ny);

					if (nlen > 1e-6) {
						nnx = nx / nlen;
						nny = ny / nlen;
					}
				}
				if (dd < 0.0) {
					double ddep = -dd;
					double zone = hh;
					double p    = zone > 0.0 ? ddep / zone
					                         : 1.0;
					double srcoff;

					if (p > 1.0)
						p = 1.0;
					srcoff = zone
					         * (sqrt(2.0 * p - p * p) - p)
					         * disp
					         + BULGE_BIAS * (1.0 - p);
					dispx  = -nnx * srcoff;
					dispy  = -nny * srcoff;
					pedge  = (1.0 - p) * (1.0 - p);
				}
				facing = nnx * SPEC_LX + nny * SPEC_LY;

				if (chroma > 0.01) {
					double cs = chroma * pedge;

					barny_sample_bilinear(gdata, gstride, gsw, gsh,
					                gx + dispx - nnx * cs,
					                gy + dispy - nny * cs,
					                p1);
					barny_sample_bilinear(gdata, gstride, gsw, gsh,
					                gx + dispx, gy + dispy,
					                p2);
					barny_sample_bilinear(gdata, gstride, gsw, gsh,
					                gx + dispx + nnx * cs,
					                gy + dispy + nny * cs,
					                p3);
					fb = p3[3] > 0 ? p3[0] * 255.0 / p3[3]
					               : 0.0;
					fg = p2[3] > 0 ? p2[1] * 255.0 / p2[3]
					               : 0.0;
					fr = p1[3] > 0 ? p1[2] * 255.0 / p1[3]
					               : 0.0;
					fa = p2[3] / 255.0;
				} else {
					barny_sample_bilinear(gdata, gstride, gsw, gsh,
					                gx + dispx, gy + dispy,
					                p2);
					fb = p2[3] > 0 ? p2[0] * 255.0 / p2[3]
					               : 0.0;
					fg = p2[3] > 0 ? p2[1] * 255.0 / p2[3]
					               : 0.0;
					fr = p2[3] > 0 ? p2[2] * 255.0 / p2[3]
					               : 0.0;
					fa = p2[3] / 255.0;
				}

				/* contour normal of the merged field drives
				   the edge relighting: highlight where the
				   contour faces up, shadow band where it faces
				   down */
				mnx  = df[gi + 1] - df[gi - 1];
				mny  = df[gi + gw] - df[gi - gw];
				mlen = sqrt(mnx * mnx + mny * mny);
				if (mlen > 1e-4) {
					mny /= mlen;
					wtop = mny < 0.0 ? -mny : 0.0;
					wbot = mny > 0.0 ? mny : 0.0;
				}
				if (depth < edge_h && edge_h > 0.0)
					atop = BARNY_FRAME_EDGE_TOP_A
					       * (1.0 - depth / edge_h) * wtop;
				if (depth < shad_h && shad_h > 0.0)
					abot = BARNY_FRAME_EDGE_BOT_A
					       * (1.0 - depth / shad_h) * wbot;

				/* frosted brightness lift inside the bubble */
				veil = -dd / BULGE_VEIL_FADE;
				if (veil < 0.0)
					veil = 0.0;
				if (veil > 1.0)
					veil = 1.0;
				veil *= BULGE_VEIL * strength;
				aw    = veil + atop - veil * atop;

				/* crisp bright rim tracing the droplet, lit
				   from the key light instead of a uniform ring */
				lit = 0.45
				      + 0.55 * (facing > 0.15 ? facing : 0.15);
				ri  = 1.0 + dd / BULGE_RIM_W;
				if (ri < 0.0 || dd > 0.0)
					ri = 0.0;
				ri   = ri * ri;
				core = 1.0 - fabs(dd) / 1.4;
				if (core < 0.0)
					core = 0.0;
				rim = (BULGE_RIM_STR * ri
				       + BULGE_RIM_CORE * core * lit)
				      * strength;
				if (rim > 1.0)
					rim = 1.0;

				/* specular: an arc hugging the rim where the
				   droplet surface faces the light, plus a faint
				   cool counter-arc opposite it */
				spec = 0.0;
				cntr = 0.0;
				if (dd < 0.0 && -dd < SPEC_W && spec_g > 0.001) {
					band = 1.0 + dd / SPEC_W;
					band = band * band * (3.0 - 2.0 * band);
					if (facing > 0.0)
						spec = spec_g * band
						       * pow(facing, SPEC_P);
					else
						cntr = SPEC_COUNTER * spec_g
						       * band
						       * pow(-facing, SPEC_P);
				}
				ty = (ly - cyp) / hh;
				if (ty < -1.0)
					ty = -1.0;
				if (ty > 1.0)
					ty = 1.0;

				fr  = fr * (1.0 - aw) + 255.0 * aw;
				fg  = fg * (1.0 - aw) + 255.0 * aw;
				fb  = fb * (1.0 - aw) + 255.0 * aw;
				fr *= 1.0 - abot;
				fg *= 1.0 - abot;
				fb *= 1.0 - abot;
				fr += (rim * (1.0 - BULGE_RIM_CHROMA * ty)
				       + spec + cntr * 0.85)
				      * 255.0;
				fg += (rim + spec + cntr * 0.92) * 255.0;
				fb += (rim * (1.0 + BULGE_RIM_CHROMA * ty)
				       + spec + cntr)
				      * 255.0;
				if (fr > 255.0)
					fr = 255.0;
				if (fg > 255.0)
					fg = 255.0;
				if (fb > 255.0)
					fb = 255.0;

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
			t   = -d;
			cov = t < 1.6 - t ? t : 1.6 - t;
			cov += 0.5;
			if (cov < 0.0)
				cov = 0.0;
			if (cov > 1.0)
				cov = 1.0;
			pa = prism > 0.001 ? prism * cov : 0.0;
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

			drow[x * 4 + 0] = (uint8_t)outb;
			drow[x * 4 + 1] = (uint8_t)outg;
			drow[x * 4 + 2] = (uint8_t)outr;
			drow[x * 4 + 3] = (uint8_t)outa;
		}
	}

	free(df);
	free(cols);

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
	double         damp;
	double         pop_damp;
	double         bmin;
	double         bmax;

	if (!state->config.dynamic_glass || output != state->dyn_output
	    || !state->lens_animating)
		return false;

	now                 = barny_now_ms();
	dt                  = (double)(now - state->lens_prev_ms) / 1000.0;
	state->lens_prev_ms = now;
	if (dt < 0.0)
		dt = 0.0;
	if (dt > LENS_DT_MAX)
		dt = LENS_DT_MAX;

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

	damp     = 2.0 * LENS_SPRING_ZETA * sqrt(LENS_SPRING_K);
	pop_damp = 2.0 * sqrt(LENS_POP_K);

	state->lens_vx += (LENS_SPRING_K * (tx - state->lens_x)
	                   - damp * state->lens_vx)
	                  * dt;
	state->lens_x  += state->lens_vx * dt;

	state->lens_sv += (LENS_POP_K
	                           * (state->lens_target_scale
	                              - state->lens_scale)
	                   - pop_damp * state->lens_sv)
	                  * dt;
	state->lens_scale += state->lens_sv * dt;
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

static void
render_dynamic(barny_output_t *output, cairo_t *cr)
{
	barny_state_t   *state  = output->state;
	int              radius = state->config.border_radius;
	double           cx     = output->pad_left;
	double           cy     = output->pad_top;
	double           cw     = output->width;
	double           ch     = output->height;
	double           px     = state->lens_x;
	double           s      = state->lens_scale;
	double           bulge  = state->config.glass_bulge;

	if (!state->config.dynamic_glass)
		return;
	if (output != state->dyn_output || !output->bg_cache)
		return;
	if (s < 0.005)
		return;

	if (bulge > 0.1) {
		double           over = state->config.position_top
		                                ? output->pad_top
		                                : output->pad_bottom;
		double           bw   = BUBBLE_W;
		double           bh   = ch + 2 * over;
		double           stretch = 1.0
		                           + fmin(LENS_STRETCH_GAIN
		                                          * fabs(state->lens_vx),
		                                  LENS_STRETCH_MAX);
		double           hw   = bw / 2.0 * stretch * s;
		double           hh   = bh / 2.0
		                        * (1.0 - 0.55 * (stretch - 1.0)) * s;
		double           br   = hh;
		double           bcx  = px;
		int              pw   = (int)(bw * (1.0 + LENS_STRETCH_MAX))
		                        + 2 * BULGE_NECK;
		int              ph   = (int)bh;
		double           straight_l = cx + radius;
		double           straight_r = cx + cw - radius;
		double           margin;
		double           corner_ramp;
		double           pinch_amp;
		double           skew;
		bool             authoritative;
		int              x0;
		int              y0;
		cairo_surface_t *patch;

		if (br > hw)
			br = hw;
		if (bcx < cx + bw / 2.0)
			bcx = cx + bw / 2.0;
		if (bcx > cx + cw - bw / 2.0)
			bcx = cx + cw - bw / 2.0;

		x0 = (int)(bcx - pw / 2.0);
		y0 = (int)(cy - over);

		/* The analytic field cannot reproduce the squircle corners, so
		   the SOURCE rewrite is only allowed on the straight stretch;
		   near the corners the pinch dies off and the patch degrades
		   to a plain overlay. */
		authoritative = x0 >= straight_l && x0 + pw <= straight_r;
		margin        = fmin(x0 - straight_l,
		                     straight_r - (x0 + pw));
		if (margin < 0.0)
			margin = 0.0;
		corner_ramp = margin >= LENS_PINCH_FADE
		                      ? 1.0
		                      : margin / LENS_PINCH_FADE;
		corner_ramp = corner_ramp * corner_ramp
		              * (3.0 - 2.0 * corner_ramp);
		pinch_amp   = LENS_PINCH_MAX * (bulge / 15.0) * s * corner_ramp;
		if (pinch_amp > ch * 0.2)
			pinch_amp = ch * 0.2;

		skew = LENS_SKEW_GAIN * state->lens_vx;
		if (skew > LENS_SKEW_MAX)
			skew = LENS_SKEW_MAX;
		if (skew < -LENS_SKEW_MAX)
			skew = -LENS_SKEW_MAX;

		patch = build_lens_patch(output, x0, y0, pw, ph,
		                         bcx - x0, ph / 2.0, hw, hh, br, skew,
		                         over, over + ch,
		                         BULGE_REFRACT * s, BULGE_CHROMA * s,
		                         s, pinch_amp, authoritative);

		if (patch) {
			cairo_save(cr);
			if (authoritative) {
				cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
				cairo_rectangle(cr, x0, y0, pw, ph);
				cairo_clip(cr);
			} else {
				barny_rounded_rect_path(cr, cx, cy - over, cw,
				                        ch + 2 * over, radius);
				cairo_clip(cr);
			}
			cairo_set_source_surface(cr, patch, x0, y0);
			cairo_paint(cr);
			cairo_restore(cr);
			cairo_surface_destroy(patch);
		}
	}
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

static void
draw_spectral_rim(cairo_t *cr, double w, double h, double r, double strength)
{
	cairo_pattern_t *p;
	int              i;
	int              n      = 40;
	double           cycles = 2.5;

	if (strength <= 0.001)
		return;

	p = cairo_pattern_create_linear(0, 0, w, 0);
	for (i = 0; i <= n; i++) {
		double pos = (double)i / n;
		double hue = fmod(pos * cycles, 1.0);
		double rr;
		double gg;
		double bb;

		hsv2rgb(hue, 0.9, 1.0, &rr, &gg, &bb);
		cairo_pattern_add_color_stop_rgba(p, pos, rr, gg, bb, strength);
	}

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_ADD);
	barny_rounded_rect_path(cr, 0.8, 0.8, w - 1.6, h - 1.6, r - 0.4);
	cairo_set_source(cr, p);
	cairo_set_line_width(cr, 1.6);
	cairo_stroke(cr);
	cairo_restore(cr);

	cairo_pattern_destroy(p);
}

static cairo_surface_t *
build_glass_bg(barny_output_t *output)
{
	barny_state_t   *state  = output->state;
	int              radius = state->config.border_radius;
	int              sw     = output->surf_width;
	int              sh     = output->surf_height;
	int              cx     = output->pad_left;
	int              cy     = output->pad_top;
	int              cw     = output->width;
	int              ch     = output->height;
	int              over   = state->config.position_top
	                                  ? output->pad_top
	                                  : output->pad_bottom;
	cairo_surface_t *cache;
	cairo_t         *cr;
	cairo_surface_t *bg;
	cairo_surface_t *src;
	cairo_surface_t *lensed;
	cairo_surface_t *shadow;
	cairo_surface_t *strip_src;
	cairo_t         *sc;

	cache = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
	cr    = cairo_create(cache);
	bg    = state->blurred_wallpaper;

	/* The clipped shadow is kept as its own cache: it is what shows
	   through wherever the lens pinches the bar silhouette away. */
	if (output->shadow_cache) {
		cairo_surface_destroy(output->shadow_cache);
		output->shadow_cache = NULL;
	}
	shadow = create_bar_shadow(cx, cy, cw, ch, radius, sw, sh);
	if (shadow) {
		output->shadow_cache = cairo_image_surface_create(
		        CAIRO_FORMAT_ARGB32, sw, sh);
		sc = cairo_create(output->shadow_cache);
		if (state->config.position_top)
			cairo_rectangle(sc, 0, cy, sw, sh - cy);
		else
			cairo_rectangle(sc, 0, 0, sw, cy + ch);
		cairo_clip(sc);
		cairo_set_source_surface(sc, shadow, 0, 0);
		cairo_paint(sc);
		cairo_destroy(sc);
		cairo_surface_destroy(shadow);

		cairo_set_source_surface(cr, output->shadow_cache, 0, 0);
		cairo_paint(cr);
	}

	src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
	sc  = cairo_create(src);
	barny_paint_glass_bg(sc, bg, cw, ch, 0, 0, ch,
	                     state->config.position_top);
	cairo_destroy(sc);

	if (!output->lens_map)
		output->lens_map = barny_create_edge_lens_map(
		        cw, ch, radius, BAR_LENS_EDGE, BAR_LENS_DISP);

	lensed = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
	if (output->lens_map) {
		barny_apply_displacement(src, lensed, output->lens_map,
		                         BAR_LENS_DISP, BAR_LENS_CHROMA);
	} else {
		sc = cairo_create(lensed);
		cairo_set_source_surface(sc, src, 0, 0);
		cairo_paint(sc);
		cairo_destroy(sc);
	}

	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, cw, ch, radius);
	cairo_clip(cr);
	cairo_set_source_surface(cr, lensed, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	barny_draw_glass_frame(cr, cw, ch, radius);
	draw_spectral_rim(cr, cw, ch, radius, state->config.glass_prism);
	cairo_restore(cr);

	/* Clean strip for the lens patch: bar interior + broad lighting only
	   (no contour-keyed edge light, no spectral rim, no rounded clip);
	   the overrun rows pad-extend the edge rows for the droplet caps. */
	if (output->glass_clean) {
		cairo_surface_destroy(output->glass_clean);
		output->glass_clean = NULL;
	}
	strip_src = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cw, ch);
	sc        = cairo_create(strip_src);
	cairo_set_source_surface(sc, lensed, 0, 0);
	cairo_paint(sc);
	barny_draw_broad_frame(sc, cw, ch);
	cairo_destroy(sc);

	output->glass_clean = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                                 cw, ch + 2 * over);
	sc                  = cairo_create(output->glass_clean);
	cairo_set_source_surface(sc, strip_src, 0, over);
	cairo_pattern_set_extend(cairo_get_source(sc), CAIRO_EXTEND_PAD);
	cairo_paint(sc);
	cairo_destroy(sc);
	cairo_surface_destroy(strip_src);

	cairo_surface_destroy(src);
	cairo_surface_destroy(lensed);
	cairo_destroy(cr);

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
	const char *path_ext = path + path_len - ext_len;
	size_t      i;

	if (path_len < ext_len)
		return 0;

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
