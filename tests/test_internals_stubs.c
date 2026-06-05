#include "barny.h"
#include <stdlib.h>
#include <string.h>

void
barny_render_frame(barny_output_t *output)
{
	(void)output;
}

int
barny_sway_ipc_send(barny_state_t *state, uint32_t type, const char *payload)
{
	(void)state;
	(void)type;
	(void)payload;
	return 0;
}

char *
barny_sway_ipc_recv(barny_state_t *state, uint32_t *type)
{
	(void)state;
	if (type)
		*type = 0;
	return strdup("[]");
}

char *
barny_sway_ipc_recv_sync(barny_state_t *state, uint32_t *type, int timeout_ms)
{
	(void)timeout_ms;
	return barny_sway_ipc_recv(state, type);
}

int
barny_sway_ipc_subscribe(barny_state_t *state, const char *events)
{
	(void)state;
	(void)events;
	return 0;
}

int
barny_sway_ipc_init(barny_state_t *state)
{
	(void)state;
	return -1;
}

void
barny_sway_ipc_cleanup(barny_state_t *state)
{
	(void)state;
}

barny_output_t *
barny_output_create(void)
{
	return NULL;
}

void
barny_output_destroy(barny_output_t *output)
{
	(void)output;
}

#include "../src/modules/popup.h"

barny_popup_t *
barny_popup_create(barny_state_t *state, barny_module_t *owner,
                   const barny_popup_callbacks_t *cb, int gap_px)
{
	(void)state;
	(void)owner;
	(void)cb;
	(void)gap_px;
	return NULL;
}

void
barny_popup_destroy(barny_popup_t *popup)
{
	(void)popup;
}

void
barny_popup_redraw(barny_popup_t *popup)
{
	(void)popup;
}

bool
barny_popup_visible(const barny_popup_t *popup)
{
	(void)popup;
	return false;
}

PangoFontDescription *
barny_popup_font_from(const char *font, const char *fallback)
{
	(void)font;
	(void)fallback;
	return NULL;
}

void
barny_popup_draw_row(cairo_t *cr, PangoLayout *layout, int row_y, int line_h,
                     int width, const char *label, const char *value,
                     double lr, double lg, double lb, double vr, double vg,
                     double vb, double val_alpha)
{
	(void)cr;
	(void)layout;
	(void)row_y;
	(void)line_h;
	(void)width;
	(void)label;
	(void)value;
	(void)lr;
	(void)lg;
	(void)lb;
	(void)vr;
	(void)vg;
	(void)vb;
	(void)val_alpha;
}

int
barny_module_render_text(cairo_t *cr, PangoFontDescription *font,
                         const char *text, int x, int y, int h,
                         const barny_config_t *cfg, double fb_r, double fb_g,
                         double fb_b, double alpha)
{
	(void)cr;
	(void)font;
	(void)text;
	(void)x;
	(void)y;
	(void)h;
	(void)cfg;
	(void)fb_r;
	(void)fb_g;
	(void)fb_b;
	(void)alpha;
	return 0;
}
