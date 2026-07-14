#ifndef BARNY_POPUP_H
#define BARNY_POPUP_H

#include "barny.h"

#define BARNY_POPUP_PAD_X 14
#define BARNY_POPUP_PAD_Y 10

#define BARNY_POPUP_RADIUS 12

typedef struct barny_popup barny_popup_t;

typedef struct {
	int   (*content_height)(void *ud);
	int   (*content_width)(void *ud);
	void  (*render)(void *ud, cairo_t *cr, int w, int h);
	void *userdata;
} barny_popup_callbacks_t;

/* A slab of liquid glass hanging off the bar: the body carrying the content,
   plus the neck rows that bridge it to the bar edge. Module popups and the tray
   menu are both drawn through this, so every panel wears the same glass. All
   coordinates are patch-local except glass_x/glass_y, which place the patch in
   the wallpaper frame barny_paint_glass_bg samples in. */
typedef struct {
	int  w; /* patch: body_w x (body_h + neck) */
	int  h;
	int  body_w;
	int  body_h;
	int  body_y;   /* body origin inside the patch; the neck takes the rest */
	int  anchor_x; /* where the droplet is born, patch-local */
	int  glass_x;
	int  glass_y;
	bool position_top;
} barny_glass_panel_t;

/* Where a panel's glass samples the wallpaper. barny_paint_glass_bg works in
   the bar's own frame, not the output's: the wallpaper is mapped onto the bar's
   content rect, so x counts from the bar's left edge, and for a bottom bar the
   wallpaper's bottom edge lines up with the bar's -- a panel flush against the
   bar's exclusive edge is at a negative y there. Getting this wrong samples off
   the end of the wallpaper and the panel comes out empty. */
int
barny_glass_panel_glass_x(const barny_config_t *cfg, int output_x);
int
barny_glass_panel_glass_y(const barny_config_t *cfg, int bar_h, int panel_h);

/* advance the open/close spring; true once the morph has settled */
bool
barny_glass_panel_step(double *morph, double *vel, uint64_t *last_us,
                       bool closing);

/* wallpaper plus broad lighting over the whole patch; the morph refracts it */
cairo_surface_t *
barny_glass_panel_bg(barny_state_t *state, barny_output_t *out,
                     const barny_glass_panel_t *panel);

/* the panel at rest: rounded body, no neck, no droplet (animations off) */
void
barny_glass_panel_compose(cairo_t *cr, barny_state_t *state,
                          barny_output_t *out,
                          const barny_glass_panel_t *panel,
                          cairo_surface_t *content);

/* the panel at morph position m: 0 = droplet squeezing out of the bar edge,
   1 = the open body. Draws at the origin of cr. */
void
barny_glass_panel_morph(cairo_t *cr, const barny_glass_panel_t *panel,
                        cairo_surface_t *bg, cairo_surface_t *content,
                        double m);

/* A bead of liquid riding on a finished panel -- the menu's hover highlight is
   one. It lenses whatever is already composed beneath it, glass and label
   alike, the way the bar's cursor droplet lenses the bar, so a hovered row
   reads as glass swelling over it rather than a rectangle painted on top.
   Coordinates are those of src, which is the panel as already drawn. */
typedef struct {
	double cx;
	double cy;
	double hw; /* half extents, the spring's stretch already in them */
	double hh;
	double radius;
	double alpha;
} barny_glass_bubble_t;

void
barny_glass_bubble_draw(cairo_t *cr, cairo_surface_t *src,
                        const barny_glass_bubble_t *bub);

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
