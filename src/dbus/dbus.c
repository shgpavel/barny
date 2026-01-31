#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "barny.h"

int
barny_dbus_init(barny_state_t *state)
{
	int r;

	r = sd_bus_open_user(&state->dbus);
	if (r < 0) {
		fprintf(stderr, "barny: failed to connect to session bus: %s\n",
		        strerror(-r));
		state->dbus = NULL;
		state->dbus_fd = -1;
		return -1;
	}

	state->dbus_fd = sd_bus_get_fd(state->dbus);
	if (state->dbus_fd < 0) {
		fprintf(stderr, "barny: failed to get D-Bus fd\n");
		sd_bus_unref(state->dbus);
		state->dbus = NULL;
		state->dbus_fd = -1;
		return -1;
	}

	printf("barny: connected to D-Bus session bus (fd=%d)\n", state->dbus_fd);

	/* Initialize SNI watcher and host */
	if (barny_sni_watcher_init(state) < 0) {
		fprintf(stderr, "barny: failed to initialize SNI watcher\n");
	}

	if (barny_sni_host_init(state) < 0) {
		fprintf(stderr, "barny: failed to initialize SNI host\n");
	}

	return 0;
}

void
barny_dbus_cleanup(barny_state_t *state)
{
	barny_sni_host_cleanup(state);
	barny_sni_watcher_cleanup(state);

	if (state->dbus) {
		sd_bus_unref(state->dbus);
		state->dbus = NULL;
	}
	state->dbus_fd = -1;
}

int
barny_dbus_dispatch(barny_state_t *state)
{
	int r;

	if (!state->dbus) {
		return 0;
	}

	/* Process all pending messages */
	for (;;) {
		r = sd_bus_process(state->dbus, NULL);
		if (r < 0) {
			fprintf(stderr, "barny: D-Bus process error: %s\n",
			        strerror(-r));
			return -1;
		}
		if (r == 0) {
			break;  /* No more messages to process */
		}
	}

	return 0;
}

int
barny_dbus_get_fd(barny_state_t *state)
{
	return state->dbus_fd;
}
