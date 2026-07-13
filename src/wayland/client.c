#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "barny.h"
#include "util.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static void
registry_global(void *data, struct wl_registry *registry, uint32_t name,
                const char *interface, uint32_t version);
static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name);

static const struct wl_registry_listener registry_listener = {
	.global        = registry_global,
	.global_remove = registry_global_remove,
};

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
		out->width       = width;
		out->height      = out->state->config.height;
		out->mode_height = height;
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

static void
pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
              struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
	barny_state_t  *state = data;
	barny_output_t *out;

	(void)pointer;
	(void)serial;

	state->pointer_x       = wl_fixed_to_double(sx);
	state->pointer_y       = wl_fixed_to_double(sy);
	state->pointer_surface = surface;

	for (out = state->outputs; out; out = out->next) {
		if (out->surface == surface) {
			state->pointer_output = out;
			break;
		}
	}

	if (state->pointer_output) {
		state->dyn_output = state->pointer_output;
		state->dyn_dirty  = true;

		if (state->lens_scale < 0.01) {
			state->lens_x       = state->pointer_x;
			state->lens_vx      = 0.0;
			state->lens_prev_ms = barny_now_ms();
		}
		state->lens_target_scale = 1.0;
		state->lens_animating    = true;
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

	if (state->hover_module && state->hover_module->on_hover) {
		state->hover_module->on_hover(state->hover_module, false, 0,
		                              0);
	}
	state->hover_module    = NULL;

	state->pointer_output  = NULL;
	state->pointer_surface = NULL;

	state->lens_target_scale = 0.0;
	state->lens_animating    = true;

	state->dyn_dirty       = true;
}

static void
pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
               wl_fixed_t sx, wl_fixed_t sy)
{
	barny_state_t  *state = data;
	barny_module_t *hovered;
	barny_module_t *mod;
	int             mx;
	int             i;

	(void)pointer;
	(void)time;

	state->pointer_x = wl_fixed_to_double(sx);
	state->pointer_y = wl_fixed_to_double(sy);

	if (barny_menu_owns_surface(state, state->pointer_surface)) {
		barny_menu_pointer_motion(state, state->pointer_x,
		                          state->pointer_y);
		return;
	}

	if (state->pointer_output) {
		state->dyn_output      = state->pointer_output;
		state->dyn_dirty       = true;
		state->lens_animating  = true;
	}

	mx               = (int)state->pointer_x;
	hovered          = NULL;

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod && mod->on_hover && mod->width > 0 && mod->render_x >= 0 && mx >= mod->render_x && mx < mod->render_x + mod->width) {
			hovered = mod;
			break;
		}
	}

	if (hovered != state->hover_module) {
		if (state->hover_module && state->hover_module->on_hover) {
			state->hover_module->on_hover(state->hover_module,
			                              false, mx,
			                              (int)state->pointer_y);
		}
		if (hovered) {
			hovered->on_hover(hovered, true, mx,
			                  (int)state->pointer_y);
		}
		state->hover_module = hovered;
	}
}

static void
pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
               uint32_t time, uint32_t button, uint32_t button_state)
{
	barny_state_t  *state = data;
	barny_module_t *mod;
	int             x;
	int             y;
	int             i;

	(void)pointer;
	(void)serial;
	(void)time;

	if (barny_menu_is_open(state)) {
		if (barny_menu_owns_surface(state, state->pointer_surface)) {
			barny_menu_pointer_button(state, button, button_state);
		} else if (button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
			barny_menu_close(state);
		}
		return;
	}

	if (button_state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}

	if (!state->pointer_output) {
		return;
	}

	x = (int)state->pointer_x;
	y = (int)state->pointer_y;

	for (i = 0; i < state->module_count; i++) {
		mod = state->modules[i];
		if (mod && mod->on_click) {
			mod->on_click(mod, button, x, y);
		}
	}
}

static void
pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
             wl_fixed_t value)
{
	barny_state_t *state = data;
	double         px;

	(void)pointer;
	(void)time;

	if (axis != 1 || !state->pointer_output || state->axis_source != WL_POINTER_AXIS_SOURCE_FINGER) {
		return;
	}

	px                            = wl_fixed_to_double(value);
	state->touchpad_scroll_accum += px;

	if (state->touchpad_scroll_accum > 30.0) {
		barny_sway_ipc_send(state, 0, "workspace next");
		state->touchpad_scroll_accum = 0.0;
	} else if (state->touchpad_scroll_accum < -30.0) {
		barny_sway_ipc_send(state, 0, "workspace prev");
		state->touchpad_scroll_accum = 0.0;
	}
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
	barny_state_t *state = data;
	(void)pointer;
	state->axis_source = axis_source;
}

static void
pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                  uint32_t axis)
{
	barny_state_t *state = data;
	(void)pointer;
	(void)time;

	if (axis == 1) {
		state->touchpad_scroll_accum = 0.0;
	}
}

static void
pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                      int32_t discrete)
{
	barny_state_t *state = data;
	(void)pointer;

	if (axis != 0 || !state->pointer_output) {
		return;
	}

	if (discrete < 0) {
		barny_sway_ipc_send(state, 0, "workspace prev");
	} else if (discrete > 0) {
		barny_sway_ipc_send(state, 0, "workspace next");
	}
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

static void
keyboard_keymap(void *data, struct wl_keyboard *kbd, uint32_t format, int32_t fd,
                uint32_t size)
{
	(void)data;
	(void)kbd;
	(void)format;
	(void)size;
	if (fd >= 0)
		close(fd);
}

static void
keyboard_enter(void *data, struct wl_keyboard *kbd, uint32_t serial,
               struct wl_surface *surface, struct wl_array *keys)
{
	(void)data;
	(void)kbd;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void
keyboard_leave(void *data, struct wl_keyboard *kbd, uint32_t serial,
               struct wl_surface *surface)
{
	barny_state_t *state = data;
	(void)kbd;
	(void)serial;

	if (barny_menu_owns_surface(state, surface))
		barny_menu_close(state);
}

static void
keyboard_key(void *data, struct wl_keyboard *kbd, uint32_t serial, uint32_t time,
             uint32_t key, uint32_t key_state)
{
	barny_state_t *state = data;
	(void)kbd;
	(void)serial;
	(void)time;

	if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (key == KEY_ESC && barny_menu_is_open(state))
		barny_menu_key_escape(state);
}

static void
keyboard_modifiers(void *data, struct wl_keyboard *kbd, uint32_t serial,
                   uint32_t mods_depressed, uint32_t mods_latched,
                   uint32_t mods_locked, uint32_t group)
{
	(void)data;
	(void)kbd;
	(void)serial;
	(void)mods_depressed;
	(void)mods_latched;
	(void)mods_locked;
	(void)group;
}

static void
keyboard_repeat_info(void *data, struct wl_keyboard *kbd, int32_t rate,
                     int32_t delay)
{
	(void)data;
	(void)kbd;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap      = keyboard_keymap,
	.enter       = keyboard_enter,
	.leave       = keyboard_leave,
	.key         = keyboard_key,
	.modifiers   = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

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

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !state->keyboard) {
		state->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(state->keyboard, &keyboard_listener,
		                         state);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && state->keyboard) {
		wl_keyboard_destroy(state->keyboard);
		state->keyboard = NULL;
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
	barny_state_t    *state = data;
	struct wl_output *wl_output;
	barny_output_t   *output;

	(void)version;

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
		wl_output = wl_registry_bind(
		        registry, name, &wl_output_interface, 4);

		output                = calloc(1, sizeof(barny_output_t));
		output->wl_output     = wl_output;
		output->state         = state;
		output->scale         = 1;
		output->registry_name = name;

		wl_output_add_listener(wl_output, &output_listener, output);

		output->next   = state->outputs;
		state->outputs = output;
	}
}

static void
registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	barny_state_t   *state = data;
	barny_output_t **prev;
	barny_output_t  *out;

	(void)registry;

	prev = &state->outputs;
	for (out = state->outputs; out; out = out->next) {
		if (out->registry_name == name) {
			if (state->pointer_output == out) {
				state->pointer_output = NULL;
			}
			if (state->dyn_output == out) {
				state->dyn_output = NULL;
			}

			*prev = out->next;

			barny_output_destroy_surface(out);
			if (out->wl_output) {
				wl_output_destroy(out->wl_output);
			}
			free(out->name);
			free(out);

			fprintf(stderr,
			        "barny: output removed (registry name %u)\n",
			        name);
			return;
		}
		prev = &out->next;
	}
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

	wl_display_roundtrip(state->display);

	return 0;
}

void
barny_wayland_cleanup(barny_state_t *state)
{
	barny_output_t *out;
	barny_output_t *next;

	out = state->outputs;
	while (out) {
		next = out->next;
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
	if (state->keyboard) {
		wl_keyboard_destroy(state->keyboard);
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
