/*
 * Stubs for barny_test_internals binary.
 * Since we #include source files directly, we need stubs for external
 * functions that those files depend on but aren't being tested.
 */

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

/* Output stubs (may be needed by some modules) */
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

/* Popup helper stubs — real impl in src/modules/popup.c pulls in
 * wayland protocol symbols not linked into this test binary. */
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
