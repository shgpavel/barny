#include "barny.h"
#include "../src/modules/popup.h"

#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "../src/render/liquid_glass.c"

#define BAR_W 1920
#define BAR_H 47

static double
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (double)ts.tv_sec * 1.0e9 + (double)ts.tv_nsec;
}

static void
report(const char *name, int iters, double total_ns)
{
	double      per;
	const char *unit;
	double      scaled;

	per    = total_ns / (double)iters;
	unit   = "ns";
	scaled = per;
	if (scaled >= 1e6) {
		scaled /= 1e6;
		unit    = "ms";
	} else if (scaled >= 1e3) {
		scaled /= 1e3;
		unit    = "us";
	}
	printf("  %-32s %8d iters  %9.2f %s/op  (total %.2f ms)\n",
	       name, iters, scaled, unit, total_ns / 1e6);
}

static cairo_t *
make_cairo(cairo_surface_t **out_surf)
{
	cairo_surface_t *surf;

	surf      = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BAR_W, BAR_H);
	*out_surf = surf;

	return cairo_create(surf);
}

static void
bench_module(const char *name, barny_module_t *(*create)(void), int update_iters,
             int render_iters)
{
	barny_state_t    state;
	barny_module_t  *mod;
	double           t0;
	double           t1;
	char             label[64];
	cairo_surface_t *surf;
	cairo_t         *cr;
	int              i;

	state = (barny_state_t){ 0 };
	barny_config_defaults(&state.config);

	mod = create();
	if (!mod || !mod->init || mod->init(mod, &state) != 0) {
		printf("  %-32s SKIP (init failed)\n", name);
		if (mod && mod->destroy)
			mod->destroy(mod);
		free(mod);
		return;
	}

	if (mod->update) {
		t0 = now_ns();
		for (i = 0; i < update_iters; i++)
			mod->update(mod);
		t1 = now_ns();
		snprintf(label, sizeof(label), "%s update", name);
		report(label, update_iters, t1 - t0);
	}

	if (mod->render) {
		cr = make_cairo(&surf);
		t0 = now_ns();
		for (i = 0; i < render_iters; i++)
			mod->render(mod, cr, 0, 0, mod->width > 0 ? mod->width : 100,
			            BAR_H);
		t1 = now_ns();
		snprintf(label, sizeof(label), "%s render", name);
		report(label, render_iters, t1 - t0);
		cairo_destroy(cr);
		cairo_surface_destroy(surf);
	}

	if (mod->destroy)
		mod->destroy(mod);
	barny_config_cleanup(&state.config);
	free(mod);
}

static void
bench_config_parse(int iters)
{
	const char *path;
	FILE       *f;
	double      t0;
	double      t1;
	int         i;

	path = "/tmp/barny_perf_config.conf";
	f    = fopen(path, "w");
	if (!f)
		return;
	fputs("position = bottom\n"
	      "height = 47\n"
	      "margin_top = 0\n"
	      "margin_bottom = 8\n"
	      "margin_left = 8\n"
	      "margin_right = 8\n"
	      "font = \"Iosevka 11\"\n"
	      "text_color = \"white\"\n"
	      "border_radius = 22\n"
	      "blur_radius = 5\n"
	      "module_spacing = 16\n"
	      "modules_left = workspace, gap:9, sysinfo, gap:39, clock\n"
	      "modules_right = weather, disk, ram, network, crypto, tray\n"
	      "crypto_pairs = BTC-USDT-SWAP, ETH-USDT-SWAP, SOL-USDT-SWAP\n"
	      "sysinfo_p_cores = 8\n"
	      "sysinfo_e_cores = 16\n"
	      "ram_mode = used\n"
	      "ram_decimals = 1\n",
	      f);
	fclose(f);

	t0 = now_ns();
	for (i = 0; i < iters; i++) {
		barny_config_t cfg;
		barny_config_defaults(&cfg);
		barny_config_load(&cfg, path);
		barny_config_cleanup(&cfg);
	}
	t1 = now_ns();
	report("config load+cleanup", iters, t1 - t0);
	unlink(path);
}

static void
bench_text_measure(int iters)
{
	PangoFontDescription *fd;
	cairo_surface_t      *surf;
	cairo_t              *cr;
	double                t0;
	double                t1;
	int                   i;

	fd   = pango_font_description_from_string("Sans 11");
	surf = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
	cr   = cairo_create(surf);

	t0   = now_ns();
	for (i = 0; i < iters; i++) {
		PangoLayout *layout = pango_cairo_create_layout(cr);
		int          w;
		int          h;

		pango_layout_set_font_description(layout, fd);
		pango_layout_set_text(layout, "BTC-USDT-SWAP $109234.56", -1);
		pango_layout_get_pixel_size(layout, &w, &h);
		g_object_unref(layout);
	}
	t1 = now_ns();
	report("pango measure", iters, t1 - t0);

	cairo_destroy(cr);
	cairo_surface_destroy(surf);
	pango_font_description_free(fd);
}

static void
bench_lens_render(int iters)
{
	barny_state_t    state = { 0 };
	barny_output_t   output = { 0 };
	cairo_surface_t *wallpaper;
	cairo_surface_t *surface;
	cairo_t         *cr;
	cairo_t         *wc;
	double           t0;
	double           t1;
	int              i;
	int              x;
	int              y;
	int              w;
	int              h;

	barny_config_defaults(&state.config);
	state.config.dynamic_glass = true;
	state.dyn_output           = &output;
	state.lens_scale           = 1.0;
	state.lens_x               = BAR_W / 2.0;

	output.state       = &state;
	output.width       = BAR_W;
	output.height      = BAR_H;
	output.pad_left    = 8;
	output.pad_right   = 8;
	output.pad_top     = 4;
	output.pad_bottom  = 8;
	output.surf_width  = output.pad_left + output.width + output.pad_right;
	output.surf_height = output.pad_top + output.height + output.pad_bottom;

	wallpaper = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BAR_W, BAR_H);
	wc        = cairo_create(wallpaper);
	cairo_set_source_rgb(wc, 0.12, 0.16, 0.24);
	cairo_paint(wc);
	cairo_destroy(wc);
	state.blurred_wallpaper = wallpaper;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                     output.surf_width,
	                                     output.surf_height);
	cr      = cairo_create(surface);

	/* Build the static bar caches before timing animation frames. */
	barny_render_liquid_glass(&output, cr);

	t0 = now_ns();
	for (i = 0; i < iters; i++) {
		int phase = i % 120;

		state.lens_x  = 200.0 + phase * 12.0 + (i % 7) * 0.137;
		state.lens_vx = (phase - 60) * 20.0;
		barny_lens_rect(&output, &x, &y, &w, &h);
		cairo_save(cr);
		cairo_rectangle(cr, x, y, w, h);
		cairo_clip(cr);
		barny_render_liquid_glass(&output, cr);
		cairo_restore(cr);
	}
	t1 = now_ns();
	report("animated lens frame", iters, t1 - t0);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	barny_output_free_glass_caches(&output, false);
	cairo_surface_destroy(wallpaper);
	barny_config_cleanup(&state.config);
}

static void
bench_rim(int iters, int mode)
{
	static const char *const names[] = {
		"ambient rim tick", "droplet frame, flow off",
		"droplet frame, off a tick", "droplet frame, on a tick",
	};
	barny_state_t    state  = { 0 };
	barny_output_t   output = { 0 };
	barny_rect_t     rects[5];
	cairo_surface_t *wallpaper;
	cairo_surface_t *surface;
	cairo_t         *cr;
	cairo_t         *wc;
	double           t0;
	double           t1;
	int              i;
	int              j;
	int              n;

	barny_config_defaults(&state.config);
	state.config.dynamic_glass = true;
	if (mode == 1)
		state.config.glass_prism_flow = 0.0;
	state.dyn_output           = &output;
	state.lens_scale           = mode == 0 ? 0.0 : 1.0;
	state.lens_x               = BAR_W / 2.0;

	output.state       = &state;
	output.scale       = 1;
	output.width       = BAR_W;
	output.height      = BAR_H;
	output.pad_left    = 8;
	output.pad_right   = 8;
	output.pad_top     = 4;
	output.pad_bottom  = 8;
	output.surf_width  = output.pad_left + output.width + output.pad_right;
	output.surf_height = output.pad_top + output.height + output.pad_bottom;

	wallpaper = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BAR_W, BAR_H);
	wc        = cairo_create(wallpaper);
	cairo_set_source_rgb(wc, 0.12, 0.16, 0.24);
	cairo_paint(wc);
	cairo_destroy(wc);
	state.blurred_wallpaper = wallpaper;

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                     output.surf_width,
	                                     output.surf_height);
	cr      = cairo_create(surface);

	barny_render_liquid_glass(&output, cr);

	t0 = now_ns();
	for (i = 0; i < iters; i++) {
		int phase = i % 120;

		n = 0;
		if (mode != 0) {
			state.lens_x  = 200.0 + phase * 12.0 + (i % 7) * 0.137;
			state.lens_vx = (phase - 60) * 20.0;
			barny_lens_rect(&output, &rects[0].x, &rects[0].y,
			                &rects[0].w, &rects[0].h);
			n = 1;
		}
		if (mode == 0 || mode == 3) {
			output.rim_tick = (uint64_t)i + 1;
			n += barny_rim_rects(&output, rects + n);
		}
		cairo_save(cr);
		for (j = 0; j < n; j++)
			cairo_rectangle(cr, rects[j].x, rects[j].y, rects[j].w,
			                rects[j].h);
		cairo_clip(cr);
		barny_render_liquid_glass(&output, cr);
		cairo_restore(cr);
	}
	t1 = now_ns();
	report(names[mode], iters, t1 - t0);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	barny_output_free_glass_caches(&output, false);
	cairo_surface_destroy(wallpaper);
	barny_config_cleanup(&state.config);
}

int
main(int argc, char **argv)
{
	printf("=== barny performance benchmarks ===\n");
	printf("(informational, no pass/fail thresholds)\n\n");
	if (argc > 1 && strcmp(argv[1], "--lens-only") == 0) {
		bench_lens_render(5000);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "--rim-only") == 0) {
		bench_rim(5000, 0);
		bench_rim(3000, 1);
		bench_rim(3000, 2);
		bench_rim(3000, 3);
		return 0;
	}

	bench_config_parse(2000);
	bench_text_measure(5000);
	bench_lens_render(2000);
	bench_rim(2000, 0);
	bench_rim(1000, 2);
	bench_rim(1000, 3);

	{
		PangoFontDescription *fd;
		int                   iters;
		double                t0;
		double                t1;
		int                   i;

		fd    = pango_font_description_from_string("Sans 11");
		iters = 5000;
		t0    = now_ns();
		for (i = 0; i < iters; i++)
			(void)barny_popup_measure_text(fd, "BTC-USDT-SWAP $109234.56");
		t1 = now_ns();
		report("popup_measure_text (cached)", iters, t1 - t0);
		pango_font_description_free(fd);
	}

	printf("\n");
	bench_module("clock", barny_module_clock_create, 1000, 1000);
	bench_module("sysinfo", barny_module_sysinfo_create, 200, 500);
	bench_module("ram", barny_module_ram_create, 500, 1000);
	bench_module("disk", barny_module_disk_create, 200, 1000);
	bench_module("network", barny_module_network_create, 200, 1000);
	bench_module("battery", barny_module_battery_create, 500, 1000);
	bench_module("weather", barny_module_weather_create, 500, 1000);
	bench_module("crypto", barny_module_crypto_create, 500, 1000);
	bench_module("workspace", barny_module_workspace_create, 200, 500);

	printf("\n=== budget guidance ===\n");
	printf("  bar refresh ~1Hz; aim:\n");
	printf("    sum(update) per tick    < 5 ms  (=> <0.5%% of one core)\n");
	printf("    each render             < 1 ms\n");
	printf("    config load             < 5 ms\n");
	return 0;
}
