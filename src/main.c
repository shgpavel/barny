#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#include "barny.h"

static barny_state_t state = { 0 };

static void
signal_handler(int sig)
{
	(void)sig;
	state.running = false;
}

static void
setup_signals(void)
{
	struct sigaction sa = { .sa_handler = signal_handler, .sa_flags = 0 };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

static int
setup_epoll(barny_state_t *s)
{
	s->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (s->epoll_fd < 0) {
		fprintf(stderr, "barny: failed to create epoll fd\n");
		return -1;
	}

	/* Add Wayland display fd */
	struct epoll_event ev
	        = { .events = EPOLLIN, .data.fd = wl_display_get_fd(s->display) };
	if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
		fprintf(stderr, "barny: failed to add wayland fd to epoll\n");
		return -1;
	}

	/* Add Sway IPC fd if connected */
	if (s->sway_ipc_fd >= 0) {
		ev.data.fd = s->sway_ipc_fd;
		if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
			fprintf(stderr,
			        "barny: failed to add sway ipc fd to epoll\n");
			return -1;
		}
	}

	return 0;
}

/* Find workspace module */
static barny_module_t *
find_workspace_module(barny_state_t *s)
{
	for (int i = 0; i < s->module_count; i++) {
		if (s->modules[i]
		    && strcmp(s->modules[i]->name, "workspace") == 0) {
			return s->modules[i];
		}
	}
	return NULL;
}

static void
run_event_loop(barny_state_t *s)
{
	struct epoll_event events[16];
	int                wayland_fd    = wl_display_get_fd(s->display);
	barny_module_t    *workspace_mod = find_workspace_module(s);

	while (s->running) {
		/* Dispatch any pending Wayland events first */
		while (wl_display_prepare_read(s->display) != 0) {
			wl_display_dispatch_pending(s->display);
		}

		if (wl_display_flush(s->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(s->display);
			fprintf(stderr, "barny: wayland flush failed\n");
			break;
		}

		int nfds = epoll_wait(s->epoll_fd, events, 16, 500);

		if (nfds < 0) {
			wl_display_cancel_read(s->display);
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "barny: epoll_wait failed: %s\n",
			        strerror(errno));
			break;
		}

		/* Handle events */
		bool wayland_readable       = false;
		bool need_workspace_refresh = false;

		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd == wayland_fd) {
				wayland_readable = true;
			} else if (events[i].data.fd == s->sway_ipc_fd) {
				uint32_t type;
				char    *payload = barny_sway_ipc_recv(s, &type);
				if (payload) {
					free(payload);
					need_workspace_refresh = true;
				}
			}
		}

		/* Complete Wayland read BEFORE any other operations */
		if (wayland_readable) {
			if (wl_display_read_events(s->display) < 0) {
				fprintf(stderr, "barny: wayland read failed\n");
				break;
			}
		} else {
			wl_display_cancel_read(s->display);
		}

		/* Dispatch events that were just read */
		wl_display_dispatch_pending(s->display);

		/* Refresh workspace data when IPC events arrive */
		if (need_workspace_refresh && workspace_mod) {
			barny_workspace_refresh(workspace_mod);
		}

		/* Only update modules on timeout or IPC events - not on every Wayland event */
		if (nfds == 0 || need_workspace_refresh) {
			barny_modules_update(s);
		}
	}
}

int
main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	printf("barny %s - liquid glass status bar\n", BARNY_VERSION);

	/* Load configuration */
	barny_config_defaults(&state.config);
	barny_config_load(&state.config, "/etc/barny/barny.conf");

	/* Try user config */
	char        config_path[512];
	const char *home = getenv("HOME");
	if (home) {
		snprintf(config_path, sizeof(config_path),
		         "%s/.config/barny/barny.conf", home);
		barny_config_load(&state.config, config_path);
	}

	/* Validate font configuration */
	barny_config_validate_font(&state.config);

	/* Initialize Wayland connection */
	if (barny_wayland_init(&state) < 0) {
		fprintf(stderr, "barny: failed to initialize wayland\n");
		return 1;
	}

	/* Load wallpaper for liquid glass effect */
	if (state.config.wallpaper_path) {
		state.wallpaper
		        = barny_load_wallpaper(state.config.wallpaper_path);
		if (state.wallpaper) {
			int w = cairo_image_surface_get_width(state.wallpaper);
			int h = cairo_image_surface_get_height(state.wallpaper);

			/* Step 1: Create blurred copy */
			state.blurred_wallpaper = cairo_image_surface_create(
			        CAIRO_FORMAT_ARGB32, w, h);
			cairo_t *cr = cairo_create(state.blurred_wallpaper);
			cairo_set_source_surface(cr, state.wallpaper, 0, 0);
			cairo_paint(cr);
			cairo_destroy(cr);
			barny_blur_surface(state.blurred_wallpaper,
			                   (int)state.config.blur_radius);
			barny_apply_brightness(state.blurred_wallpaper,
			                       state.config.brightness);

			/* Step 2: Create displacement map if refraction is enabled */
			if (state.config.refraction_mode != BARNY_REFRACT_NONE) {
				printf("barny: creating liquid glass displacement map...\n");
				state.displacement_map
				        = barny_create_displacement_map(
				                w, h, state.config.refraction_mode,
				                state.config.border_radius,
				                state.config.edge_refraction,
				                state.config.noise_scale,
				                state.config.noise_octaves);

				/* Step 3: Apply displacement to create final wallpaper */
				if (state.displacement_map) {
					state.displaced_wallpaper
					        = cairo_image_surface_create(
					                CAIRO_FORMAT_ARGB32, w, h);
					barny_apply_displacement(
					        state.blurred_wallpaper,
					        state.displaced_wallpaper,
					        state.displacement_map,
					        state.config.displacement_scale,
					        state.config.chromatic_aberration);
					printf("barny: liquid glass effect applied (mode=%s, scale=%.1f, chromatic=%.1f)\n",
					       state.config.refraction_mode
					                       == BARNY_REFRACT_LENS ?
					               "lens" :
					               "liquid",
					       state.config.displacement_scale,
					       state.config.chromatic_aberration);
				}
			}
		}
	}

	/* Initialize Sway IPC */
	state.sway_ipc_fd = -1;
	barny_sway_ipc_init(&state);

	/* Initialize modules */
	barny_module_register(&state, barny_module_clock_create());
	barny_module_register(&state, barny_module_workspace_create());
	barny_module_register(&state, barny_module_weather_create());
	barny_module_register(&state, barny_module_crypto_create());
	barny_module_register(&state, barny_module_sysinfo_create());
	barny_modules_init(&state);

	/* Setup signal handlers */
	setup_signals();
	state.running = true;

	/* Setup epoll */
	if (setup_epoll(&state) < 0) {
		barny_wayland_cleanup(&state);
		return 1;
	}

	/* Run event loop */
	run_event_loop(&state);

	/* Cleanup */
	barny_modules_destroy(&state);
	barny_sway_ipc_cleanup(&state);
	barny_wayland_cleanup(&state);

	if (state.wallpaper) {
		cairo_surface_destroy(state.wallpaper);
	}
	if (state.blurred_wallpaper) {
		cairo_surface_destroy(state.blurred_wallpaper);
	}
	if (state.displaced_wallpaper) {
		cairo_surface_destroy(state.displaced_wallpaper);
	}
	if (state.displacement_map) {
		cairo_surface_destroy(state.displacement_map);
	}
	if (state.epoll_fd >= 0) {
		close(state.epoll_fd);
	}

	printf("barny: shutdown complete\n");
	return 0;
}
