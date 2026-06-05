#include <cairo/cairo.h>

#include "barny.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void
barny_rounded_rect_path(cairo_t *cr, double x, double y, double w, double h,
                        double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
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
	cairo_pattern_t *highlight;
	cairo_pattern_t *shadow;
	cairo_pattern_t *top_refract;
	cairo_pattern_t *left_refract;

	barny_rounded_rect_path(cr, 0.5, 0.5, w - 1, h - 1, r);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);

	barny_rounded_rect_path(cr, 1.5, 1.5, w - 3, h - 3, r - 1);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	cairo_set_line_width(cr, 2);
	cairo_stroke(cr);

	cairo_save(cr);
	barny_rounded_rect_path(cr, 0, 0, w, h, r);
	cairo_clip(cr);

	highlight = cairo_pattern_create_linear(0, 0, w * 0.7, h * 0.7);
	cairo_pattern_add_color_stop_rgba(highlight, 0.0, 1, 1, 1, 0.15);
	cairo_pattern_add_color_stop_rgba(highlight, 0.3, 1, 1, 1, 0.04);
	cairo_pattern_add_color_stop_rgba(highlight, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, highlight);
	cairo_paint(cr);
	cairo_pattern_destroy(highlight);

	shadow = cairo_pattern_create_linear(w * 0.3, h * 0.3, w, h);
	cairo_pattern_add_color_stop_rgba(shadow, 0.0, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(shadow, 0.7, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(shadow, 1.0, 0, 0, 0, 0.15);
	cairo_set_source(cr, shadow);
	cairo_paint(cr);
	cairo_pattern_destroy(shadow);

	top_refract = cairo_pattern_create_linear(0, 0, 0, 8);
	cairo_pattern_add_color_stop_rgba(top_refract, 0.0, 1, 1, 1, 0.08);
	cairo_pattern_add_color_stop_rgba(top_refract, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, top_refract);
	cairo_rectangle(cr, 0, 0, w, 8);
	cairo_fill(cr);
	cairo_pattern_destroy(top_refract);

	left_refract = cairo_pattern_create_linear(0, 0, 8, 0);
	cairo_pattern_add_color_stop_rgba(left_refract, 0.0, 1, 1, 1, 0.06);
	cairo_pattern_add_color_stop_rgba(left_refract, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, left_refract);
	cairo_rectangle(cr, 0, 0, 8, h);
	cairo_fill(cr);
	cairo_pattern_destroy(left_refract);

	cairo_restore(cr);
}
