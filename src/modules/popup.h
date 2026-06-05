#ifndef BARNY_POPUP_H
#define BARNY_POPUP_H

#include "barny.h"

#define BARNY_POPUP_PAD_X 14
#define BARNY_POPUP_PAD_Y 10

typedef struct barny_popup barny_popup_t;

typedef struct {
	int   (*content_height)(void *ud);
	int   (*content_width)(void *ud);
	void  (*render)(void *ud, cairo_t *cr, int w, int h);
	void *userdata;
} barny_popup_callbacks_t;

barny_popup_t *
barny_popup_create(barny_state_t *state, barny_module_t *owner,
                   const barny_popup_callbacks_t *cb, int gap_px);

void
barny_popup_destroy(barny_popup_t *popup);

void
barny_popup_redraw(barny_popup_t *popup);

bool
barny_popup_visible(const barny_popup_t *popup);

int
barny_popup_measure_text(PangoFontDescription *font_desc, const char *text);

PangoFontDescription *
barny_popup_font_from(const char *font, const char *fallback);

void
barny_popup_draw_row(cairo_t *cr, PangoLayout *layout, int row_y, int line_h,
                     int width, const char *label, const char *value,
                     double lr, double lg, double lb, double vr, double vg,
                     double vb, double val_alpha);

#endif
