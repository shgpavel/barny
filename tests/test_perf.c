/*
 * Micro-benchmarks for module update + render hot paths and config parse.
 *
 * Goal: catch regressions in CPU cost. Numbers are informational; the
 * test never fails on slow runs (CI variance is real). The harness
 * prints ns/op for each benchmark; eyeball them on PR diffs.
 */
#include "barny.h"

#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

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
	double per = total_ns / (double)iters;
	const char *unit = "ns";
	double scaled = per;
	if (scaled >= 1e6) {
		scaled /= 1e6;
		unit = "ms";
	} else if (scaled >= 1e3) {
		scaled /= 1e3;
		unit = "us";
	}
	printf("  %-32s %8d iters  %9.2f %s/op  (total %.2f ms)\n",
	       name, iters, scaled, unit, total_ns / 1e6);
}

static cairo_t *
make_cairo(cairo_surface_t **out_surf)
{
	cairo_surface_t *surf
	        = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BAR_W, BAR_H);
	*out_surf = surf;
	return cairo_create(surf);
}

static void
bench_module(const char *name, barny_module_t *(*create)(void), int update_iters,
             int render_iters)
{
	barny_state_t state = { 0 };
	barny_config_defaults(&state.config);

	barny_module_t *mod = create();
	if (!mod || !mod->init || mod->init(mod, &state) != 0) {
		printf("  %-32s SKIP (init failed)\n", name);
		if (mod && mod->destroy)
			mod->destroy(mod);
		free(mod);
		return;
	}

	if (mod->update) {
		double t0 = now_ns();
		for (int i = 0; i < update_iters; i++)
			mod->update(mod);
		double t1 = now_ns();
		char    label[64];
		snprintf(label, sizeof(label), "%s update", name);
		report(label, update_iters, t1 - t0);
	}

	if (mod->render) {
		cairo_surface_t *surf;
		cairo_t         *cr = make_cairo(&surf);
		double           t0 = now_ns();
		for (int i = 0; i < render_iters; i++)
			mod->render(mod, cr, 0, 0, mod->width > 0 ? mod->width : 100,
			            BAR_H);
		double t1 = now_ns();
		char    label[64];
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
	const char *path = "/tmp/barny_perf_config.conf";
	FILE       *f    = fopen(path, "w");
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

	double t0 = now_ns();
	for (int i = 0; i < iters; i++) {
		barny_config_t cfg;
		barny_config_defaults(&cfg);
		barny_config_load(&cfg, path);
		barny_config_cleanup(&cfg);
	}
	double t1 = now_ns();
	report("config load+cleanup", iters, t1 - t0);
	unlink(path);
}

static void
bench_text_measure(int iters)
{
	PangoFontDescription *fd
	        = pango_font_description_from_string("Sans 11");
	cairo_surface_t *surf
	        = cairo_image_surface_create(CAIRO_FORMAT_A8, 1, 1);
	cairo_t *cr = cairo_create(surf);

	double t0 = now_ns();
	for (int i = 0; i < iters; i++) {
		PangoLayout *layout = pango_cairo_create_layout(cr);
		pango_layout_set_font_description(layout, fd);
		pango_layout_set_text(layout, "BTC-USDT-SWAP $109234.56", -1);
		int w, h;
		pango_layout_get_pixel_size(layout, &w, &h);
		g_object_unref(layout);
	}
	double t1 = now_ns();
	report("pango measure", iters, t1 - t0);

	cairo_destroy(cr);
	cairo_surface_destroy(surf);
	pango_font_description_free(fd);
}

int
main(void)
{
	printf("=== barny performance benchmarks ===\n");
	printf("(informational, no pass/fail thresholds)\n\n");

	bench_config_parse(2000);
	bench_text_measure(5000);

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
