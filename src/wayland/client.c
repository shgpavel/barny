#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "barny.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* Forward declarations for listeners */
static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
                const char *interface, uint32_t version);
static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name);

static const struct wl_registry_listener registry_listener = {
	.global        = registry_global,
	.global_remove = registry_global_remove,
};

/* Output listener */
static void
output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
                int32_t phys_w, int32_t phys_h, int32_t subpixel, const char *make,
                const char *model, int32_t transform)
{
	(void)data;
	(void)output;
	(void)x;
	(void)y;
	(void)phys_w;
	(void)phys_h;
	(void)subpixel;
	(void)make;
	(void)model;
	(void)transform;
}

static void
output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width,
            int32_t height, int32_t refresh)
{
	barny_output_t *out = data;
	(void)output;
	(void)refresh;

	if (flags & WL_OUTPUT_MODE_CURRENT) {
		out->width  = width;
		out->height = out->state->config.height;
	}
}

static void
output_done(void *data, struct wl_output *output)
{
	barny_output_t *out = data;
	(void)output;

	if (!out->configured && out->width > 0) {
		barny_output_create_surface(out);
	}
}

static void
output_scale(void *data, struct wl_output *output, int32_t factor)
{
	barny_output_t *out = data;
	(void)output;
	out->scale = factor;
}

static void
output_name(void *data, struct wl_output *output, const char *name)
{
	barny_output_t *out = data;
	(void)output;
	free(out->name);
	out->name = strdup(name);
}

static void
output_description(void *data, struct wl_output *output, const char *description)
{
	(void)data;
	(void)output;
	(void)description;
}

static const struct wl_output_listener output_listener = {
	.geometry    = output_geometry,
	.mode        = output_mode,
	.done        = output_done,
	.scale       = output_scale,
	.name        = output_name,
	.description = output_description,
};

/* Pointer listener */
static void
pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
              struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
	barny_state_t *state = data;
	(void)pointer;
	(void)serial;

	state->pointer_x = wl_fixed_to_double(sx);
	state->pointer_y = wl_fixed_to_double(sy);

	/* Find which output this surface belongs to */
	for (barny_output_t *out = state->outputs; out; out = out->next) {
		if (out->surface == surface) {
			state->pointer_output = out;
			break;
		}
	}
}

static void
pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
              struct wl_surface *surface)
{
	barny_state_t *state = data;
	(void)pointer;
	(void)serial;
	(void)surface;
	state->pointer_output = NULL;
}

static void
pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
               wl_fixed_t sx, wl_fixed_t sy)
{
	barny_state_t *state = data;
	(void)pointer;
	(void)time;
	state->pointer_x = wl_fixed_to_double(sx);
	state->pointer_y = wl_fixed_to_double(sy);
}

static void
pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
               uint32_t time, uint32_t button, uint32_t button_state)
{
	barny_state_t *state = data;
	(void)pointer;
	(void)serial;
	(void)time;

	if (button_state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}

	if (!state->pointer_output) {
		return;
	}

	/* Dispatch click to modules */
	int x = (int)state->pointer_x;
	int y = (int)state->pointer_y;

	for (int i = 0; i < state->module_count; i++) {
		barny_module_t *mod = state->modules[i];
		if (mod && mod->on_click) {
			mod->on_click(mod, button, x, y);
		}
	}
}

static void
pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
             wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

static void
pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data;
	(void)pointer;
}

static void
pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source)
{
	(void)data;
	(void)pointer;
	(void)axis_source;
}

static void
pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                  uint32_t axis)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                      int32_t discrete)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter         = pointer_enter,
	.leave         = pointer_leave,
	.motion        = pointer_motion,
	.button        = pointer_button,
	.axis          = pointer_axis,
	.frame         = pointer_frame,
	.axis_source   = pointer_axis_source,
	.axis_stop     = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

/* Seat listener */
static void
seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
	barny_state_t *state = data;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
		state->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(state->pointer, &pointer_listener, state);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && state->pointer) {
		wl_pointer_destroy(state->pointer);
		state->pointer = NULL;
	}
}

static void
seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name         = seat_name,
};

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
                const char *interface, uint32_t version)
{
	barny_state_t *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
		                                     &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm
		        = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(
		        registry, name, &zwlr_layer_shell_v1_interface, 4);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->seat
		        = wl_registry_bind(registry, name, &wl_seat_interface, 7);
		wl_seat_add_listener(state->seat, &seat_listener, state);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *wl_output = wl_registry_bind(
		        registry, name, &wl_output_interface, 4);

		barny_output_t *output = calloc(1, sizeof(barny_output_t));
		output->wl_output      = wl_output;
		output->state          = state;
		output->scale          = 1;

		wl_output_add_listener(wl_output, &output_listener, output);

		/* Add to list */
		output->next   = state->outputs;
		state->outputs = output;
	}
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	(void)data;
	(void)registry;
	(void)name;
	/* TODO: Handle output removal */
}

int
barny_wayland_init(barny_state_t *state)
{
	state->display = wl_display_connect(NULL);
	if (!state->display) {
		fprintf(stderr, "barny: cannot connect to wayland display\n");
		return -1;
	}

	state->registry = wl_display_get_registry(state->display);
	wl_registry_add_listener(state->registry, &registry_listener, state);

	/* First roundtrip to get globals */
	wl_display_roundtrip(state->display);

	if (!state->compositor) {
		fprintf(stderr, "barny: compositor not available\n");
		return -1;
	}
	if (!state->shm) {
		fprintf(stderr, "barny: wl_shm not available\n");
		return -1;
	}
	if (!state->layer_shell) {
		fprintf(stderr,
		        "barny: layer_shell not available (is this wlroots-based?)\n");
		return -1;
	}

	/* Second roundtrip to get output modes */
	wl_display_roundtrip(state->display);

	return 0;
}

void
barny_wayland_cleanup(barny_state_t *state)
{
	/* Destroy outputs */
	barny_output_t *out = state->outputs;
	while (out) {
		barny_output_t *next = out->next;
		barny_output_destroy_surface(out);
		if (out->wl_output) {
			wl_output_destroy(out->wl_output);
		}
		free(out->name);
		free(out);
		out = next;
	}
	state->outputs = NULL;

	if (state->pointer) {
		wl_pointer_destroy(state->pointer);
	}
	if (state->seat) {
		wl_seat_destroy(state->seat);
	}
	if (state->layer_shell) {
		zwlr_layer_shell_v1_destroy(state->layer_shell);
	}
	if (state->shm) {
		wl_shm_destroy(state->shm);
	}
	if (state->compositor) {
		wl_compositor_destroy(state->compositor);
	}
	if (state->registry) {
		wl_registry_destroy(state->registry);
	}
	if (state->display) {
		wl_display_disconnect(state->display);
	}
}

int
barny_wayland_dispatch(barny_state_t *state)
{
	return wl_display_dispatch(state->display);
}
