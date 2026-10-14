#include "barny.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

static void
scaler_preferred(void *data, struct wp_fractional_scale_v1 *fractional,
                 uint32_t scale)
{
	barny_scaler_t *sc = data;
	(void)fractional;

	if (scale == 0 || scale == sc->preferred)
		return;

	sc->preferred = scale;
	if (sc->changed)
		sc->changed(sc->data);
}

static const struct wp_fractional_scale_v1_listener scaler_listener = {
	.preferred_scale = scaler_preferred,
};

void
barny_scaler_init(barny_scaler_t *sc, barny_state_t *state,
                  struct wl_surface *surface, void (*changed)(void *),
                  void *data)
{
	sc->viewport   = NULL;
	sc->fractional = NULL;
	sc->preferred  = 0;
	sc->changed    = changed;
	sc->data       = data;

	if (!state->viewporter || !state->fractional_scale || !surface)
		return;

	sc->viewport   = wp_viewporter_get_viewport(state->viewporter, surface);
	sc->fractional = wp_fractional_scale_manager_v1_get_fractional_scale(
	        state->fractional_scale, surface);
	wp_fractional_scale_v1_add_listener(sc->fractional, &scaler_listener,
	                                    sc);
}

void
barny_scaler_fini(barny_scaler_t *sc)
{
	if (sc->fractional) {
		wp_fractional_scale_v1_destroy(sc->fractional);
		sc->fractional = NULL;
	}
	if (sc->viewport) {
		wp_viewport_destroy(sc->viewport);
		sc->viewport = NULL;
	}
	sc->preferred = 0;
}

uint32_t
barny_scaler_scale(const barny_scaler_t *sc, uint32_t guess120,
                   int32_t output_scale)
{
	if (sc->viewport && sc->preferred)
		return sc->preferred;
	if (sc->viewport && guess120)
		return guess120;

	return (uint32_t)(output_scale > 1 ? output_scale : 1)
	       * BARNY_SCALE_UNIT;
}

void
barny_scaler_apply(const barny_scaler_t *sc, struct wl_surface *surface,
                   uint32_t scale120, int w, int h)
{
	if (sc->viewport) {
		wp_viewport_set_destination(sc->viewport, w, h);
		return;
	}

	wl_surface_set_buffer_scale(surface,
	                            (int32_t)(scale120 / BARNY_SCALE_UNIT));
}
