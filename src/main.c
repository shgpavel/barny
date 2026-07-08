#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

#include "barny.h"
#include "util.h"

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
	struct epoll_event ev;

	s->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (s->epoll_fd < 0) {
		fprintf(stderr, "barny: failed to create epoll fd\n");
		return -1;
	}

	ev = (struct epoll_event){ .events = EPOLLIN, .data.fd = wl_display_get_fd(s->display) };
	if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
		fprintf(stderr, "barny: failed to add wayland fd to epoll\n");
		return -1;
	}

	if (s->sway_ipc_fd >= 0) {
		ev.data.fd = s->sway_ipc_fd;
		if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
			fprintf(stderr,
			        "barny: failed to add sway ipc fd to epoll\n");
			return -1;
		}
	}

	if (s->dbus_fd >= 0) {
		ev.data.fd = s->dbus_fd;
		if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) < 0) {
			fprintf(stderr,
			        "barny: failed to add D-Bus fd to epoll\n");
			return -1;
		}
	}

	return 0;
}

static void
run_event_loop(barny_state_t *s)
{
	struct epoll_event events[16];
	int                wayland_fd;
	barny_module_t    *workspace_mod;
	barny_module_t    *windowtitle_mod;
	uint64_t           last_update;
	uint64_t           now;
	int                nfds;
	bool               wayland_readable;
	bool               need_workspace_refresh;
	bool               dbus_readable;
	int                i;
	uint32_t           type;
	char              *payload;
	barny_output_t    *out;

	wayland_fd      = wl_display_get_fd(s->display);
	workspace_mod   = barny_module_find(s, "workspace");
	windowtitle_mod = barny_module_find(s, "windowtitle");
	last_update     = 0;

	while (s->running) {
		now = barny_now_ms();
		if (now - last_update >= 500) {
			barny_modules_update(s);
			last_update = now;
		}

		while (wl_display_prepare_read(s->display) != 0) {
			wl_display_dispatch_pending(s->display);
		}

		if (wl_display_flush(s->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(s->display);
			fprintf(stderr, "barny: wayland flush failed\n");
			break;
		}

		nfds = epoll_wait(s->epoll_fd, events, 16, 200);

		if (nfds < 0) {
			wl_display_cancel_read(s->display);
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "barny: epoll_wait failed: %s\n",
			        strerror(errno));
			break;
		}

		wayland_readable       = false;
		need_workspace_refresh = false;
		dbus_readable          = false;

		for (i = 0; i < nfds; i++) {
			if (events[i].data.fd == wayland_fd) {
				wayland_readable = true;
			} else if (events[i].data.fd == s->sway_ipc_fd) {
				for (;;) {
					payload = barny_sway_ipc_recv(s, &type);
					if (!payload)
						break;
					free(payload);
					need_workspace_refresh = true;
				}
			} else if (events[i].data.fd == s->dbus_fd) {
				dbus_readable = true;
			}
		}

		if (wayland_readable) {
			if (wl_display_read_events(s->display) < 0) {
				fprintf(stderr, "barny: wayland read failed\n");
				break;
			}
		} else {
			wl_display_cancel_read(s->display);
		}

		wl_display_dispatch_pending(s->display);

		if (dbus_readable) {
			barny_dbus_dispatch(s);
		}

		if (need_workspace_refresh && workspace_mod) {
			barny_workspace_refresh(workspace_mod);
		}
		if (need_workspace_refresh && windowtitle_mod) {
			barny_windowtitle_refresh(windowtitle_mod);
		}

		if (nfds > 0 && barny_modules_any_dirty(s)) {
			for (out = s->outputs; out; out = out->next) {
				if (out->configured)
					barny_render_frame(out);
			}
			s->dyn_dirty = false;
		} else if (s->dyn_dirty) {
			if (s->dyn_output && s->dyn_output->configured)
				barny_render_frame(s->dyn_output);
			s->dyn_dirty = false;
		}
	}
}

int
main(int argc, char *argv[])
{
	char                  config_path[512];
	const char           *home;
	int                   w;
	int                   h;
	int                   max_needed_height;
	int                   crop_y_offset;
	barny_output_t       *out;
	double                scale_x;
	double                scale_y;
	double                scale;
	int                   needed;
	cairo_surface_t      *cropped;
	cairo_t              *cr;
	barny_module_layout_t layout;

	(void)argc;
	(void)argv;

	setvbuf(stdout, NULL, _IOLBF, 0);

	printf("barny %s - liquid glass status bar\n", BARNY_VERSION);

	barny_config_defaults(&state.config);
	barny_config_load(&state.config, "/etc/barny/barny.conf");

	home = getenv("HOME");
	if (home) {
		snprintf(config_path, sizeof(config_path),
		         "%s/.config/barny/barny.conf", home);
		barny_config_load(&state.config, config_path);
	}

	barny_config_validate_font(&state.config);

	if (barny_wayland_init(&state) < 0) {
		fprintf(stderr, "barny: failed to initialize wayland\n");
		return 1;
	}

	if (state.config.wallpaper_path) {
		state.wallpaper
		        = barny_load_wallpaper(state.config.wallpaper_path);
		if (state.wallpaper) {
			w                 = cairo_image_surface_get_width(state.wallpaper);
			h                 = cairo_image_surface_get_height(state.wallpaper);
			max_needed_height = 0;
			crop_y_offset     = 0;

			if (state.outputs) {
				for (out = state.outputs; out; out = out->next) {
					if (out->width > 0 && out->mode_height > 0) {
						scale_x = (double)w / out->width;
						scale_y = (double)h / out->mode_height;
						scale   = scale_x < scale_y ? scale_x : scale_y;
						needed  = (int)(out->mode_height * scale) + (int)state.config.blur_radius * 2 + (int)state.config.displacement_scale * 2 + 64;
						if (needed > max_needed_height) {
							max_needed_height = needed;
						}
					}
				}

				if (max_needed_height > 0 && max_needed_height < h) {
					if (!state.config.position_top) {
						crop_y_offset = h - max_needed_height;
					}
					h       = max_needed_height;
					cropped = cairo_image_surface_create(
					        CAIRO_FORMAT_ARGB32, w, h);
					cr = cairo_create(cropped);
					cairo_set_source_surface(cr, state.wallpaper, 0, -crop_y_offset);
					cairo_paint(cr);
					cairo_destroy(cr);

					cairo_surface_destroy(state.wallpaper);
					state.wallpaper = cropped;
					printf("barny: cropped wallpaper from %dx%d to %dx%d (y-offset=%d) for startup optimization\n",
					       w, h + crop_y_offset, w, h, crop_y_offset);
				}
			}

			state.blurred_wallpaper = cairo_image_surface_create(
			        CAIRO_FORMAT_ARGB32, w, h);
			cr = cairo_create(state.blurred_wallpaper);
			cairo_set_source_surface(cr, state.wallpaper, 0, 0);
			cairo_paint(cr);
			cairo_destroy(cr);
			barny_blur_surface(state.blurred_wallpaper,
			                   (int)state.config.blur_radius);
			barny_apply_vibrancy(state.blurred_wallpaper, 1.35,
			                     state.config.brightness);

			if (state.config.refraction_mode != BARNY_REFRACT_NONE) {
				printf("barny: creating liquid glass displacement map...\n");
				state.displacement_map
				        = barny_create_displacement_map(
				                w, h, state.config.refraction_mode,
				                state.config.border_radius,
				                state.config.edge_refraction,
				                state.config.noise_scale,
				                state.config.noise_octaves);

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

	state.sway_ipc_fd = -1;
	barny_sway_ipc_init(&state);

	state.dbus_fd = -1;
	if (barny_dbus_init(&state) < 0) {
		fprintf(stderr, "barny: D-Bus init failed, tray disabled\n");
	}

	barny_module_layout_init(&layout);
	barny_module_layout_load_from_config(&state.config, &layout);
	barny_module_layout_apply_to_state(&layout, &state);
	barny_module_layout_destroy(&layout);
	barny_modules_init(&state);

	setup_signals();
	state.running = true;

	if (setup_epoll(&state) < 0) {
		barny_wayland_cleanup(&state);
		return 1;
	}

	run_event_loop(&state);

	barny_modules_destroy(&state);
	barny_dbus_cleanup(&state);
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

	barny_config_cleanup(&state.config);
	printf("barny: shutdown complete\n");
	return 0;
}
