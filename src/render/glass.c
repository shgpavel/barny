#include <cairo/cairo.h>
#include <math.h>

#include "barny.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SQUIRCLE_N     4.0
#define SQUIRCLE_STEPS 14

static void
squircle_corner(cairo_t *cr, double cx, double cy, double r, double a0,
                double a1)
{
	int    i;
	double t;
	double ang;
	double ca;
	double sa;
	double u;
	double v;

	for (i = 0; i <= SQUIRCLE_STEPS; i++) {
		t   = (double)i / SQUIRCLE_STEPS;
		ang = a0 + (a1 - a0) * t;
		ca  = cos(ang);
		sa  = sin(ang);
		u   = copysign(pow(fabs(ca), 2.0 / SQUIRCLE_N), ca);
		v   = copysign(pow(fabs(sa), 2.0 / SQUIRCLE_N), sa);
		cairo_line_to(cr, cx + r * u, cy + r * v);
	}
}

void
barny_rounded_rect_path(cairo_t *cr, double x, double y, double w, double h,
                        double r)
{
	if (r < 0.5) {
		cairo_rectangle(cr, x, y, w, h);
		return;
	}
	if (r > w / 2)
		r = w / 2;
	if (r > h / 2)
		r = h / 2;

	cairo_new_sub_path(cr);
	cairo_move_to(cr, x + r, y);
	cairo_line_to(cr, x + w - r, y);
	squircle_corner(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_line_to(cr, x + w, y + h - r);
	squircle_corner(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_line_to(cr, x + r, y + h);
	squircle_corner(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_line_to(cr, x, y + r);
	squircle_corner(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

void
barny_paint_glass_bg(cairo_t *cr, cairo_surface_t *bg, int out_w, int out_h,
                     int screen_x, int screen_y, int target_h, bool position_top)
{
	if (bg && out_w > 0 && out_h > 0) {
		int    wp_w;
		int    wp_h;
		double scale_x;
		double scale_y;
		double scale;
		int    src_y_off;

		wp_w      = cairo_image_surface_get_width(bg);
		wp_h      = cairo_image_surface_get_height(bg);
		scale_x   = (double)wp_w / out_w;
		scale_y   = (double)wp_h / out_h;
		scale     = scale_x < scale_y ? scale_x : scale_y;
		src_y_off = position_top ? 0 : (wp_h - (int)(out_h * scale));

		cairo_save(cr);
		cairo_scale(cr, 1.0 / scale, 1.0 / scale);
		cairo_set_source_surface(cr, bg, -screen_x * scale,
		                         -((double)screen_y * scale + src_y_off));
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		cairo_pattern_t *grad;

		grad = cairo_pattern_create_linear(0, 0, 0, target_h);
		cairo_pattern_add_color_stop_rgba(grad, 0, 0.15, 0.15, 0.18, 0.85);
		cairo_pattern_add_color_stop_rgba(grad, 1, 0.08, 0.08, 0.10, 0.85);
		cairo_set_source(cr, grad);
		cairo_paint(cr);
		cairo_pattern_destroy(grad);
	}
}

void
barny_draw_glass_frame(cairo_t *cr, double w, double h, double r)
{
	cairo_pattern_t *p;
	double           shadow_h = h * 0.28 > 10 ? 10 : h * 0.28;

	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, w, h, r);
	cairo_clip(cr);

	p = cairo_pattern_create_linear(0, 0, 0, h);
	cairo_pattern_add_color_stop_rgba(p, 0.00, 1, 1, 1, 0.13);
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

	p = cairo_pattern_create_linear(0, h - shadow_h, 0, h);
	cairo_pattern_add_color_stop_rgba(p, 0.0, 0, 0, 0, 0.0);
	cairo_pattern_add_color_stop_rgba(p, 1.0, 0, 0, 0, 0.16);
	cairo_set_source(cr, p);
	cairo_paint(cr);
	cairo_pattern_destroy(p);

	p = cairo_pattern_create_linear(0, 0, 0, 4);
	cairo_pattern_add_color_stop_rgba(p, 0.0, 1, 1, 1, 0.9);
	cairo_pattern_add_color_stop_rgba(p, 1.0, 1, 1, 1, 0.0);
	cairo_set_source(cr, p);
	cairo_rectangle(cr, 0, 0, w, 4);
	cairo_fill(cr);
	cairo_pattern_destroy(p);

	cairo_restore(cr);

	p = cairo_pattern_create_linear(0, 0, 0, h);
	cairo_pattern_add_color_stop_rgba(p, 0.0, 1, 1, 1, 0.85);
	cairo_pattern_add_color_stop_rgba(p, 0.5, 1, 1, 1, 0.18);
	cairo_pattern_add_color_stop_rgba(p, 1.0, 1, 1, 1, 0.12);
	barny_rounded_rect_path(cr, 1, 1, w - 2, h - 2, r - 0.5);
	cairo_set_source(cr, p);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);
	cairo_pattern_destroy(p);
}
