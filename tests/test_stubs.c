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
