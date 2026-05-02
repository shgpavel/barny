#ifndef BARNY_POPUP_H
#define BARNY_POPUP_H

#include "barny.h"

/* Padding from popup edge where module-supplied content begins. */
#define BARNY_POPUP_PAD_X 14
#define BARNY_POPUP_PAD_Y 10

typedef struct barny_popup barny_popup_t;

typedef struct {
	int  (*content_height)(void *ud);    /* in px */
	int  (*content_width)(void *ud);     /* in px, or 0 to use default */
	void (*render)(void *ud, cairo_t *cr, int w, int h);
	void *userdata;
} barny_popup_callbacks_t;

/* Allocate and configure a hover popup anchored next to owner module.
 * gap_px is additional spacing between the bar and the popup.
 * Returns NULL on failure. */
barny_popup_t *
barny_popup_create(barny_state_t *state, barny_module_t *owner,
                   const barny_popup_callbacks_t *cb, int gap_px);

/* Tear down all wayland/cairo/shm resources. Safe with NULL. */
void
barny_popup_destroy(barny_popup_t *popup);

/* Trigger redraw of current popup content (e.g. after data update). */
void
barny_popup_redraw(barny_popup_t *popup);

/* Returns true if the popup is currently displayed (configured & buffer ready). */
bool
barny_popup_visible(const barny_popup_t *popup);

/* Measure text width in pixels using the given Pango font description.
 * Used by content_width callbacks to size the popup to fit its rows. */
int
barny_popup_measure_text(PangoFontDescription *font_desc, const char *text);

#endif /* BARNY_POPUP_H */
