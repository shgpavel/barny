#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>

#include "barny.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

/* Layer surface listener */
static void
layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
                        uint32_t serial, uint32_t width, uint32_t height)
{
	barny_output_t *output = data;

	output->width          = width;
	output->height         = height;

	zwlr_layer_surface_v1_ack_configure(surface, serial);

	if (barny_output_create_buffer(output) < 0) {
		fprintf(stderr, "barny: failed to create buffer\n");
		return;
	}

	output->configured = true;
	barny_render_frame(output);
}

static void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface)
{
	barny_output_t *output = data;
	(void)surface;

	barny_output_destroy_surface(output);
	output->state->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed    = layer_surface_closed,
};

/* Frame callback */
static void
frame_done(void *data, struct wl_callback *callback, uint32_t callback_time)
{
	barny_output_t *output = data;
	(void)callback_time;

	wl_callback_destroy(callback);
	output->frame_pending = false;

	/* Check if any module needs redraw */
	bool needs_redraw     = false;
	for (int i = 0; i < output->state->module_count; i++) {
		if (output->state->modules[i]
		    && output->state->modules[i]->dirty) {
			needs_redraw = true;
			break;
		}
	}

	if (needs_redraw) {
		barny_render_frame(output);
	}
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

/* Create shared memory file */
static int
create_shm_file(size_t size)
{
	char name[] = "/barny-XXXXXX";
	int  fd     = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		return -1;
	}
	shm_unlink(name);

	if (ftruncate(fd, size) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

int
barny_output_create_surface(barny_output_t *output)
{
	barny_state_t *state = output->state;

	/* Create wl_surface */
	output->surface      = wl_compositor_create_surface(state->compositor);
	if (!output->surface) {
		fprintf(stderr, "barny: failed to create surface\n");
		return -1;
	}

	/* Create layer surface */
	uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
	output->layer_surface
	        = zwlr_layer_shell_v1_get_layer_surface(state->layer_shell,
	                                                output->surface,
	                                                output->wl_output,
	                                                layer,
	                                                "barny");

	if (!output->layer_surface) {
		fprintf(stderr, "barny: failed to create layer surface\n");
		wl_surface_destroy(output->surface);
		output->surface = NULL;
		return -1;
	}

	zwlr_layer_surface_v1_add_listener(output->layer_surface,
	                                   &layer_surface_listener, output);

	/* Configure anchors based on position */
	uint32_t anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
	                  | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
	if (state->config.position_top) {
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
	} else {
		anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
	}
	zwlr_layer_surface_v1_set_anchor(output->layer_surface, anchor);

	/* Set size (0 width = use output width) */
	zwlr_layer_surface_v1_set_size(output->layer_surface, 0,
	                               state->config.height);

	/* Set exclusive zone to reserve space */
	zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface,
	                                         state->config.height);

	/* Set margins */
	zwlr_layer_surface_v1_set_margin(output->layer_surface,
	                                 state->config.margin_top,
	                                 state->config.margin_right,
	                                 state->config.margin_bottom,
	                                 state->config.margin_left);

	/* Commit to trigger configure */
	wl_surface_commit(output->surface);

	return 0;
}

void
barny_output_destroy_surface(barny_output_t *output)
{
	if (output->cr) {
		cairo_destroy(output->cr);
		output->cr = NULL;
	}
	if (output->cairo_surface) {
		cairo_surface_destroy(output->cairo_surface);
		output->cairo_surface = NULL;
	}
	if (output->buffer) {
		wl_buffer_destroy(output->buffer);
		output->buffer = NULL;
	}
	if (output->shm_data) {
		munmap(output->shm_data, output->shm_size);
		output->shm_data = NULL;
	}
	if (output->layer_surface) {
		zwlr_layer_surface_v1_destroy(output->layer_surface);
		output->layer_surface = NULL;
	}
	if (output->surface) {
		wl_surface_destroy(output->surface);
		output->surface = NULL;
	}
	output->configured = false;
}

int
barny_output_create_buffer(barny_output_t *output)
{
	barny_state_t *state  = output->state;
	int            width  = output->width * output->scale;
	int            height = output->height * output->scale;
	int            stride = width * 4;
	int            size   = stride * height;

	/* Clean up old buffer */
	if (output->cr) {
		cairo_destroy(output->cr);
	}
	if (output->cairo_surface) {
		cairo_surface_destroy(output->cairo_surface);
	}
	if (output->buffer) {
		wl_buffer_destroy(output->buffer);
	}
	if (output->shm_data) {
		munmap(output->shm_data, output->shm_size);
	}

	/* Create shared memory buffer */
	int fd = create_shm_file(size);
	if (fd < 0) {
		fprintf(stderr, "barny: failed to create shm file\n");
		return -1;
	}

	output->shm_data
	        = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (output->shm_data == MAP_FAILED) {
		fprintf(stderr, "barny: mmap failed\n");
		close(fd);
		return -1;
	}
	output->shm_size         = size;

	struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
	output->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
	                                           WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	/* Create Cairo surface */
	output->cairo_surface = cairo_image_surface_create_for_data(
	        output->shm_data, CAIRO_FORMAT_ARGB32, width, height, stride);
	output->cr = cairo_create(output->cairo_surface);

	/* Apply scale */
	if (output->scale > 1) {
		cairo_scale(output->cr, output->scale, output->scale);
		wl_surface_set_buffer_scale(output->surface, output->scale);
	}

	return 0;
}

void
barny_output_request_frame(barny_output_t *output)
{
	if (output->frame_pending) {
		return;
	}

	struct wl_callback *cb = wl_surface_frame(output->surface);
	wl_callback_add_listener(cb, &frame_listener, output);
	output->frame_pending = true;
}
