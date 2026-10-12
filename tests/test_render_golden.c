#include "barny.h"
#include "../src/modules/popup.h"

#include <cairo.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/render/liquid_glass.c"

#define BAR_W    1200
#define BAR_H    47
#define BAR_R    22
#define WALL_W   1216
#define WALL_H   360
#define PANEL_W  240
#define PANEL_H  150
#define NECK_H   14
#define MENU_W   264
#define MENU_H   168

typedef struct {
	const char *name;
	uint64_t    hash;
} golden_t;

static const golden_t golden[] = {
	{ "lens.bot.centre",                 0x4b550504f89f34f3ULL },
	{ "lens.bot.vx+1500",                0xbac3449dcddc2d2eULL },
	{ "lens.bot.vx-1500",                0xef60170ce0ffd097ULL },
	{ "lens.bot.pop50",                  0x9202410332e50f78ULL },
	{ "lens.bot.prism0",                 0x872bd2858595850eULL },
	{ "lens.bot.alpha.centre",           0x572e0fcd3983a247ULL },
	{ "lens.bot.alpha.vx+1500",          0xcfb67981eafc2043ULL },
	{ "lens.top.centre",                 0xad538a578129a89cULL },
	{ "bar.bot.centre",                  0x669df6d61c3b125cULL },
	{ "bar.bot.bg_cache",                0xd97443a33c9e0014ULL },
	{ "bar.bot.glass_clean",             0x52504de9af5b83ebULL },
	{ "bar.bot.shadow_cache",            0xdeabe27a0903e1beULL },
	{ "bar.bot.rest",                    0xd97443a33c9e0014ULL },
	{ "bar.bot.vx+300",                  0x5db2307a19b90b70ULL },
	{ "bar.bot.vx+1500",                 0xaa205edb6108985dULL },
	{ "bar.bot.vx-1500",                 0x1567aad56576985dULL },
	{ "bar.bot.pop50",                   0x37b8191fb8193221ULL },
	{ "bar.bot.nearleft",                0x19cda884db9612f6ULL },
	{ "bar.bot.left",                    0xe899344be706ff7eULL },
	{ "bar.bot.right",                   0x4ff8211597eac934ULL },
	{ "bar2.bot.centre",                 0x0a822dc088ee837fULL },
	{ "bar2.bot.vx-1500",                0x6f20671351f94920ULL },
	{ "bar2.bot.left",                   0x4f05064afe582a43ULL },
	{ "bar.bot.prism0.centre",           0x6ef5fdf43e151340ULL },
	{ "bar.bot.prism0.bg_cache",         0xfe09c46f6ca2aee8ULL },
	{ "bar.bot.prism0.glass_clean",      0x52504de9af5b83ebULL },
	{ "bar.bot.prism0.shadow_cache",     0xdeabe27a0903e1beULL },
	{ "bar.bot.prism0.right",            0xc60b15fb3e0de45fULL },
	{ "bar.bot.alpha.centre",            0xc161b0cd7c94fb55ULL },
	{ "bar.bot.alpha.bg_cache",          0x5e0edcd4d3e899c3ULL },
	{ "bar.bot.alpha.glass_clean",       0x6cd85556e0f0b995ULL },
	{ "bar.bot.alpha.shadow_cache",      0xdeabe27a0903e1beULL },
	{ "bar.bot.alpha.vx+1500",           0x4cf3649f322f1092ULL },
	{ "bar.bot.alpha.left",              0xe9dc6a62cfc49fc4ULL },
	{ "bar.bot.nowall.centre",           0xe790b4c9d4897d58ULL },
	{ "bar.bot.nowall.bg_cache",         0x75178db64bf6dc88ULL },
	{ "bar.bot.nowall.glass_clean",      0x46488b91b8855b6dULL },
	{ "bar.bot.nowall.shadow_cache",     0xdeabe27a0903e1beULL },
	{ "bar.top.centre",                  0x9e42a9769609f059ULL },
	{ "bar.top.bg_cache",                0xf2834e8362d6528cULL },
	{ "bar.top.glass_clean",             0x86d48ffab9b2d5a7ULL },
	{ "bar.top.shadow_cache",            0x2b7685dca03e576eULL },
	{ "bar.top.vx-1500",                 0x6b8c71c0dd2d3a90ULL },
	{ "bar.top.pop50",                   0xdbaa910ac589e7e7ULL },
	{ "bar.top.left",                    0x94af6e7ea3532e39ULL },
	{ "bar.top.right",                   0x9fa808fae3bf2433ULL },
	{ "bar2.top.centre",                 0x026e03047e7b45d8ULL },
	{ "bar.top.alpha.centre",            0x752f2a31a979c99bULL },
	{ "bar.top.alpha.bg_cache",          0xd62cb6ebe3636680ULL },
	{ "bar.top.alpha.glass_clean",       0x55cd1d2e93770599ULL },
	{ "bar.top.alpha.shadow_cache",      0x2b7685dca03e576eULL },
	{ "morph.bot.opaque.m002",           0x531f606c2fce3420ULL },
	{ "morph.bot.opaque.m005",           0x0334310de12d311aULL },
	{ "morph.bot.opaque.m010",           0x073609066afc90b6ULL },
	{ "morph.bot.opaque.m020",           0x16d5265f34fac966ULL },
	{ "morph.bot.opaque.m035",           0x3b7039aac95cac87ULL },
	{ "morph.bot.opaque.m050",           0x1507e6148f6393baULL },
	{ "morph.bot.opaque.m075",           0xdd6dc09631f88570ULL },
	{ "morph.bot.opaque.m095",           0x9b86501fd1525202ULL },
	{ "morph.bot.opaque.m100",           0xa3cbf6601066161eULL },
	{ "morph.bot.opaque.m101",           0x136c04af04dd21f1ULL },
	{ "morph.bot.alpha.m002",            0x7cb8d48397e77b38ULL },
	{ "morph.bot.alpha.m005",            0x91276af78bc280daULL },
	{ "morph.bot.alpha.m010",            0x54290c5468eee9a8ULL },
	{ "morph.bot.alpha.m020",            0x059508496f4117b5ULL },
	{ "morph.bot.alpha.m035",            0x1389afbd4e8975eaULL },
	{ "morph.bot.alpha.m050",            0xeff6b022b53ba2c0ULL },
	{ "morph.bot.alpha.m075",            0x8c19f889c8a2d56eULL },
	{ "morph.bot.alpha.m095",            0x9b94e754d301ae57ULL },
	{ "morph.bot.alpha.m100",            0xba163230f8ceaf27ULL },
	{ "morph.top.opaque.m002",           0xd3364fb045474181ULL },
	{ "morph.top.opaque.m005",           0x6fb6587f1d1f26afULL },
	{ "morph.top.opaque.m010",           0xdbbb647527efed10ULL },
	{ "morph.top.opaque.m020",           0x71cbc3415367dc1fULL },
	{ "morph.top.opaque.m035",           0xa015049b92a7c1e7ULL },
	{ "morph.top.opaque.m050",           0xa8bd4f055817017dULL },
	{ "morph.top.opaque.m075",           0xb4b4cf1d203a8db5ULL },
	{ "morph.top.opaque.m095",           0x2a07ad62c496a5c6ULL },
	{ "morph.top.opaque.m100",           0x4bb2f27a4d5937f6ULL },
	{ "morph.top.opaque.m101",           0xce1ea0b1144d1cacULL },
	{ "morph.top.alpha.m002",            0x85d49594e425b94cULL },
	{ "morph.top.alpha.m005",            0xcd036b545f4986aeULL },
	{ "morph.top.alpha.m010",            0x67dc72616c8e6c9fULL },
	{ "morph.top.alpha.m020",            0x82d55e1a86f0cfe3ULL },
	{ "morph.top.alpha.m035",            0x0461162963360935ULL },
	{ "morph.top.alpha.m050",            0x009dbe781f680ed2ULL },
	{ "morph.top.alpha.m075",            0xdc8672ab607975e7ULL },
	{ "morph.top.alpha.m095",            0x1b273f4a013cff9cULL },
	{ "morph.top.alpha.m100",            0x514931e17ae06753ULL },
	{ "popup.bot.glass_src",             0xf188e6d2a8e43e49ULL },
	{ "popup.bot.m075",                  0x6b339bd5c01b182eULL },
	{ "popup.bot.m095",                  0x58d8e2b79a9f244dULL },
	{ "popup.bot.m100",                  0x0068e53919abb768ULL },
	{ "popup.bot.compose",               0x7714b855d00c7dafULL },
	{ "popup.top.glass_src",             0x9fd43ecfecc671aeULL },
	{ "popup.top.m075",                  0x620d9ddbf759bef4ULL },
	{ "popup.top.m095",                  0xbe1d56c2b6398b4dULL },
	{ "popup.top.m100",                  0x07e6dcaa28931138ULL },
	{ "popup.top.compose",               0x1af19fe5837af2eaULL },
	{ "popup.bot.nowall.compose",        0x88b01908713a8294ULL },
	{ "bead.row",                        0xeecc54c20a9573fdULL },
	{ "bead.cell",                       0xd7b291093c2fa32fULL },
	{ "bead.pop",                        0x9b75b0a7059ae600ULL },
	{ "bead.travel",                     0xe233110cc786799cULL },
	{ "bead.edge",                       0xce8a17964a54faecULL },
	{ "bead.alpha.row",                  0x25579d2833e4bb00ULL },
	{ "bead.on_rest",                    0x4fdbc49c9eee82c1ULL },
};

#define N_GOLDEN (sizeof(golden) / sizeof(golden[0]))

typedef struct {
	barny_state_t  state;
	barny_output_t out;
} bar_t;

static bool        print_mode;
static const char *png_dir;
static bool        seen[N_GOLDEN];
static int         failures;
static int         checked;
static int         asserts;

static uint64_t
fnv1a(cairo_surface_t *s)
{
	uint64_t       h = 0xcbf29ce484222325ULL;
	const uint8_t *data;
	int            w;
	int            rows;
	int            stride;
	int            x;
	int            y;

	if (!s || cairo_surface_status(s) != CAIRO_STATUS_SUCCESS)
		return 0;

	cairo_surface_flush(s);
	data   = cairo_image_surface_get_data(s);
	w      = cairo_image_surface_get_width(s);
	rows   = cairo_image_surface_get_height(s);
	stride = cairo_image_surface_get_stride(s);
	if (!data)
		return 0;

	for (y = 0; y < rows; y++) {
		const uint8_t *row = data + (size_t)y * stride;

		for (x = 0; x < w * 4; x++) {
			h ^= row[x];
			h *= 0x100000001b3ULL;
		}
	}

	return h;
}

static void
check(const char *name, cairo_surface_t *s)
{
	uint64_t got = fnv1a(s);
	char     quoted[64];
	size_t   i;

	checked++;
	if (png_dir && s) {
		char path[512];

		snprintf(path, sizeof(path), "%s/%s.png", png_dir, name);
		cairo_surface_write_to_png(s, path);
	}

	if (print_mode) {
		snprintf(quoted, sizeof(quoted), "\"%s\",", name);
		printf("\t{ %-34s 0x%016" PRIx64 "ULL },\n", quoted, got);
		return;
	}

	for (i = 0; i < N_GOLDEN; i++) {
		if (strcmp(golden[i].name, name) == 0)
			break;
	}
	if (i == N_GOLDEN) {
		printf("FAIL %-32s no golden hash, got 0x%016" PRIx64 "\n",
		       name, got);
		failures++;
		return;
	}
	if (seen[i]) {
		printf("FAIL %-32s hashed twice\n", name);
		failures++;
		return;
	}
	seen[i] = true;
	if (golden[i].hash != got) {
		printf("FAIL %-32s expected 0x%016" PRIx64 " got 0x%016" PRIx64
		       "\n",
		       name, golden[i].hash, got);
		failures++;
		return;
	}
	printf("ok   %-32s 0x%016" PRIx64 "\n", name, got);
}

static int
diff_bytes(cairo_surface_t *a, cairo_surface_t *b)
{
	const uint8_t *da;
	const uint8_t *db;
	int            w;
	int            rows;
	int            sa;
	int            sb;
	int            x;
	int            y;
	int            n = 0;

	w    = cairo_image_surface_get_width(a);
	rows = cairo_image_surface_get_height(a);
	if (w != cairo_image_surface_get_width(b)
	    || rows != cairo_image_surface_get_height(b))
		return -1;

	cairo_surface_flush(a);
	cairo_surface_flush(b);
	da = cairo_image_surface_get_data(a);
	db = cairo_image_surface_get_data(b);
	sa = cairo_image_surface_get_stride(a);
	sb = cairo_image_surface_get_stride(b);

	for (y = 0; y < rows; y++) {
		for (x = 0; x < w * 4; x++) {
			if (da[(size_t)y * sa + x] != db[(size_t)y * sb + x])
				n++;
		}
	}

	return n;
}

static void
expect(const char *name, bool ok, const char *what, int got)
{
	FILE *f = print_mode ? stderr : stdout;

	asserts++;
	if (!ok) {
		fprintf(f, "FAIL %-32s %s %d\n", name, what, got);
		failures++;
		return;
	}
	if (!print_mode)
		printf("ok   %s\n", name);
}

static uint32_t
mix(uint32_t x, uint32_t y, uint32_t seed)
{
	uint32_t n = (x * 0x9e3779b1U) ^ (y * 0x85ebca77U)
	             ^ (seed * 0xc2b2ae3dU);

	n ^= n >> 15;
	n *= 0x2c1b3c6dU;
	n ^= n >> 12;
	n *= 0x297a2d39U;
	n ^= n >> 15;

	return n;
}

static cairo_surface_t *
texture(int w, int h, uint32_t seed, int amin, int amax)
{
	cairo_surface_t *s;
	uint8_t         *data;
	int              stride;
	int              x;
	int              y;

	s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_surface_flush(s);
	data   = cairo_image_surface_get_data(s);
	stride = cairo_image_surface_get_stride(s);

	for (y = 0; y < h; y++) {
		uint8_t *row = data + (size_t)y * stride;

		for (x = 0; x < w; x++) {
			uint32_t n      = mix((uint32_t)x, (uint32_t)y, seed);
			int      phase  = (x + 3 * y + (int)seed) % 97;
			int      a      = amin + (amax - amin) * phase / 96;
			bool     stripe = (x / 4 + y / 3 + (int)seed) % 9 == 0;
			int      c[3];
			int      i;

			c[0] = x * 255 / (w > 1 ? w - 1 : 1);
			c[1] = y * 255 / (h > 1 ? h - 1 : 1);
			c[2] = ((x + 2 * y) * 5 + (int)seed * 31) & 255;

			for (i = 0; i < 3; i++) {
				int v = c[(i + (int)seed) % 3]
				        + (int)((n >> (i * 8)) & 31) - 16;

				if (stripe)
					v = 255 - (255 - v) / 4;
				if (v < 0)
					v = 0;
				if (v > 255)
					v = 255;
				row[x * 4 + i] = (uint8_t)((v * a + 127) / 255);
			}
			row[x * 4 + 3] = (uint8_t)a;
		}
	}

	cairo_surface_mark_dirty(s);

	return s;
}

static cairo_surface_t *
content(int w, int h)
{
	cairo_surface_t *s;
	uint8_t         *data;
	int              stride;
	int              x;
	int              y;

	s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	cairo_surface_flush(s);
	data   = cairo_image_surface_get_data(s);
	stride = cairo_image_surface_get_stride(s);

	for (y = BARNY_POPUP_PAD_Y; y < h - BARNY_POPUP_PAD_Y; y++) {
		uint8_t *row  = data + (size_t)y * stride;
		int      line = (y - BARNY_POPUP_PAD_Y) / 22;
		int      dy   = (y - BARNY_POPUP_PAD_Y) % 22;

		if (dy < 5 || dy >= 17)
			continue;

		for (x = BARNY_POPUP_PAD_X; x < w - BARNY_POPUP_PAD_X; x++) {
			bool value = x >= w / 2 + 8;
			int  ink   = x * 7 + line * 13 + dy * (value ? 3 : 1);
			int  a     = 235;
			int  r     = value ? 120 : 255;
			int  g     = value ? 220 : 255;
			int  b     = 255;

			if (ink % 11 >= 6)
				continue;
			if (x > w / 2 - 6 && !value)
				continue;
			row[x * 4 + 0] = (uint8_t)((b * a + 127) / 255);
			row[x * 4 + 1] = (uint8_t)((g * a + 127) / 255);
			row[x * 4 + 2] = (uint8_t)((r * a + 127) / 255);
			row[x * 4 + 3] = (uint8_t)a;
		}
	}

	cairo_surface_mark_dirty(s);

	return s;
}

static cairo_surface_t *
menu_src(int amin)
{
	cairo_surface_t *s = texture(MENU_W, MENU_H, 53, amin, 255);
	uint8_t         *data;
	int              stride;
	int              x;
	int              y;

	cairo_surface_flush(s);
	data   = cairo_image_surface_get_data(s);
	stride = cairo_image_surface_get_stride(s);

	for (y = 0; y < MENU_H; y++) {
		bool edge_row = y < 3 || y >= MENU_H - 3;

		for (x = 0; x < MENU_W; x++) {
			if (!edge_row && x >= 3 && x < MENU_W - 3)
				continue;
			memset(data + (size_t)y * stride + (size_t)x * 4, 0, 4);
		}
	}

	cairo_surface_mark_dirty(s);

	return s;
}

static bar_t *
bar_new(cairo_surface_t *wall, bool top, double prism)
{
	bar_t *b = calloc(1, sizeof(*b));

	if (!b) {
		fprintf(stderr, "out of memory\n");
		exit(1);
	}

	b->state.config.dynamic_glass = true;
	b->state.config.glass_gleam   = 0.24;
	b->state.config.glass_bulge   = 15.0;
	b->state.config.glass_prism   = prism;
	b->state.config.border_radius = BAR_R;
	b->state.config.position_top  = top;
	b->state.blurred_wallpaper    = wall;
	b->state.dyn_output           = &b->out;
	b->state.lens_scale           = 1.0;

	b->out.state                  = &b->state;
	b->out.scale                  = 1;
	b->out.width                  = BAR_W;
	b->out.height                 = BAR_H;
	b->out.pad_left               = 8;
	b->out.pad_right              = 8;
	b->out.pad_top                = top ? 6 : 28;
	b->out.pad_bottom             = top ? 28 : 6;
	b->out.surf_width             = b->out.pad_left + BAR_W
	                                + b->out.pad_right;
	b->out.surf_height            = b->out.pad_top + BAR_H
	                                + b->out.pad_bottom;

	return b;
}

static void
bar_free(bar_t *b)
{
	barny_output_free_lens_cache(&b->out);
	if (b->out.bg_cache)
		cairo_surface_destroy(b->out.bg_cache);
	if (b->out.lens_map)
		cairo_surface_destroy(b->out.lens_map);
	if (b->out.shadow_cache)
		cairo_surface_destroy(b->out.shadow_cache);
	if (b->out.glass_clean)
		cairo_surface_destroy(b->out.glass_clean);
	free(b);
}

static cairo_surface_t *
bar_target(const bar_t *b, int scale)
{
	return cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
	                                  b->out.surf_width * scale,
	                                  b->out.surf_height * scale);
}

static void
bar_draw(bar_t *b, cairo_surface_t *surf, double lens_x, double vx,
         double s, const int *clip)
{
	cairo_t *cr    = cairo_create(surf);
	int      scale = cairo_image_surface_get_width(surf)
	                 / b->out.surf_width;

	b->out.scale        = scale;
	b->state.lens_x     = lens_x;
	b->state.lens_vx    = vx;
	b->state.lens_scale = s;

	if (scale > 1)
		cairo_scale(cr, scale, scale);
	if (clip) {
		cairo_rectangle(cr, clip[0], clip[1], clip[2], clip[3]);
		cairo_clip(cr);
	}
	barny_render_liquid_glass(&b->out, cr);
	cairo_destroy(cr);
}

static void
bar_check(bar_t *b, const char *name, int scale, double lens_x, double vx,
          double s)
{
	cairo_surface_t *surf = bar_target(b, scale);

	bar_draw(b, surf, lens_x, vx, s, NULL);
	check(name, surf);
	cairo_surface_destroy(surf);
}

static void
bar_caches(bar_t *b, const char *prefix)
{
	char name[64];

	snprintf(name, sizeof(name), "%s.bg_cache", prefix);
	check(name, b->out.bg_cache);
	snprintf(name, sizeof(name), "%s.glass_clean", prefix);
	check(name, b->out.glass_clean);
	snprintf(name, sizeof(name), "%s.shadow_cache", prefix);
	check(name, b->out.shadow_cache);
}

static bar_t *
lens_bar(bool top, int amin)
{
	bar_t *b    = bar_new(NULL, top, 0.8);
	int    over = top ? b->out.pad_top : b->out.pad_bottom;

	b->out.bg_cache           = texture(b->out.surf_width,
	                                    b->out.surf_height, 11, 255, 255);
	b->out.shadow_cache       = texture(b->out.surf_width,
	                                    b->out.surf_height, 37, 0, 180);
	b->out.glass_clean        = texture(BAR_W, BAR_H + 2 * over, 23, amin,
	                                    255);
	b->out.glass_clean_opaque = amin == 255;

	return b;
}

static void
lens_cases(void)
{
	bar_t *b;

	b = lens_bar(false, 255);
	bar_check(b, "lens.bot.centre", 1, 517.37, 0.0, 1.0);
	bar_check(b, "lens.bot.vx+1500", 1, 517.37, 1500.0, 1.0);
	bar_check(b, "lens.bot.vx-1500", 1, 517.37, -1500.0, 1.0);
	bar_check(b, "lens.bot.pop50", 1, 517.37, 0.0, 0.5);
	b->state.config.glass_prism = 0.0;
	bar_check(b, "lens.bot.prism0", 1, 517.37, 300.0, 1.0);
	bar_free(b);

	b = lens_bar(false, 90);
	bar_check(b, "lens.bot.alpha.centre", 1, 517.37, 0.0, 1.0);
	bar_check(b, "lens.bot.alpha.vx+1500", 1, 517.37, 1500.0, 1.0);
	bar_free(b);

	b = lens_bar(true, 255);
	bar_check(b, "lens.top.centre", 1, 517.37, -300.0, 1.0);
	bar_free(b);
}

static void
bar_cases(cairo_surface_t *wall, cairo_surface_t *veil)
{
	bar_t *b;

	b = bar_new(wall, false, 0.8);
	bar_check(b, "bar.bot.centre", 1, 517.37, 0.0, 1.0);
	bar_caches(b, "bar.bot");
	bar_check(b, "bar.bot.rest", 1, 517.37, 0.0, 0.0);
	bar_check(b, "bar.bot.vx+300", 1, 517.37, 300.0, 1.0);
	bar_check(b, "bar.bot.vx+1500", 1, 517.37, 1500.0, 1.0);
	bar_check(b, "bar.bot.vx-1500", 1, 517.37, -1500.0, 1.0);
	bar_check(b, "bar.bot.pop50", 1, 517.37, 0.0, 0.5);
	bar_check(b, "bar.bot.nearleft", 1, 160.5, 0.0, 1.0);
	bar_check(b, "bar.bot.left", 1, 60.0, 0.0, 1.0);
	bar_check(b, "bar.bot.right", 1, 1190.0, 900.0, 1.0);
	bar_check(b, "bar2.bot.centre", 2, 517.37, 0.0, 1.0);
	bar_check(b, "bar2.bot.vx-1500", 2, 517.37, -1500.0, 1.0);
	bar_check(b, "bar2.bot.left", 2, 60.0, 0.0, 1.0);
	bar_free(b);

	b = bar_new(wall, false, 0.0);
	bar_check(b, "bar.bot.prism0.centre", 1, 517.37, 0.0, 1.0);
	bar_caches(b, "bar.bot.prism0");
	bar_check(b, "bar.bot.prism0.right", 1, 1190.0, 0.0, 1.0);
	bar_free(b);

	b = bar_new(veil, false, 0.8);
	bar_check(b, "bar.bot.alpha.centre", 1, 517.37, 0.0, 1.0);
	bar_caches(b, "bar.bot.alpha");
	bar_check(b, "bar.bot.alpha.vx+1500", 1, 517.37, 1500.0, 1.0);
	bar_check(b, "bar.bot.alpha.left", 1, 60.0, 0.0, 1.0);
	bar_free(b);

	b = bar_new(NULL, false, 0.8);
	bar_check(b, "bar.bot.nowall.centre", 1, 517.37, 0.0, 1.0);
	bar_caches(b, "bar.bot.nowall");
	bar_free(b);

	b = bar_new(wall, true, 0.8);
	bar_check(b, "bar.top.centre", 1, 517.37, 0.0, 1.0);
	bar_caches(b, "bar.top");
	bar_check(b, "bar.top.vx-1500", 1, 517.37, -1500.0, 1.0);
	bar_check(b, "bar.top.pop50", 1, 517.37, 0.0, 0.5);
	bar_check(b, "bar.top.left", 1, 60.0, 0.0, 1.0);
	bar_check(b, "bar.top.right", 1, 1190.0, -900.0, 1.0);
	bar_check(b, "bar2.top.centre", 2, 517.37, 0.0, 1.0);
	bar_free(b);

	b = bar_new(veil, true, 0.8);
	bar_check(b, "bar.top.alpha.centre", 1, 517.37, 0.0, 1.0);
	bar_caches(b, "bar.top.alpha");
	bar_free(b);
}

static void
strip_rect(bar_t *b, double xa, double xb, int *clip)
{
	int ra[4] = { 0 };
	int rb[4] = { 0 };

	b->state.lens_x = xa;
	barny_lens_rect(&b->out, &ra[0], &ra[1], &ra[2], &ra[3]);
	b->state.lens_x = xb;
	barny_lens_rect(&b->out, &rb[0], &rb[1], &rb[2], &rb[3]);

	clip[0] = ra[0] < rb[0] ? ra[0] : rb[0];
	clip[1] = ra[1] < rb[1] ? ra[1] : rb[1];
	clip[2] = ra[0] + ra[2] > rb[0] + rb[2] ? ra[0] + ra[2] : rb[0] + rb[2];
	clip[3] = ra[1] + ra[3] > rb[1] + rb[3] ? ra[1] + ra[3] : rb[1] + rb[3];
	clip[2] -= clip[0];
	clip[3] -= clip[1];
}

static void
partial_case(cairo_surface_t *wall, bool top, const char *name, int scale,
             double xa, double xb)
{
	bar_t           *b = bar_new(wall, top, 0.8);
	cairo_surface_t *full;
	cairo_surface_t *part;
	cairo_surface_t *at_a;
	char             label[64];
	int              clip[4];
	int              n;

	full = bar_target(b, scale);
	part = bar_target(b, scale);
	at_a = bar_target(b, scale);

	bar_draw(b, full, xb, 1500.0, 1.0, NULL);
	bar_draw(b, at_a, xa, 0.0, 1.0, NULL);
	bar_draw(b, part, xa, 0.0, 1.0, NULL);
	strip_rect(b, xa, xb, clip);
	bar_draw(b, part, xb, 1500.0, 1.0, clip);

	snprintf(label, sizeof(label), "%s strip is narrow", name);
	expect(label, clip[2] > 0 && clip[2] < b->out.surf_width / 2,
	       "strip width", clip[2]);
	n = diff_bytes(full, part);
	snprintf(label, sizeof(label), "%s strip==full", name);
	expect(label, n == 0, "bytes differ", n);
	n = diff_bytes(full, at_a);
	snprintf(label, sizeof(label), "%s droplet moved", name);
	expect(label, n > 0, "bytes differ", n);

	cairo_surface_destroy(full);
	cairo_surface_destroy(part);
	cairo_surface_destroy(at_a);
	bar_free(b);
}

static void
panel_init(barny_glass_panel_t *panel, bool top)
{
	panel->w            = PANEL_W;
	panel->h            = PANEL_H;
	panel->body_w       = PANEL_W;
	panel->body_h       = PANEL_H - NECK_H;
	panel->body_y       = top ? NECK_H : 0;
	panel->anchor_x     = 72;
	panel->glass_x      = 412;
	panel->glass_y      = top ? 59 : BAR_H - 59 - PANEL_H;
	panel->position_top = top;
}

static void
morph_check(const char *name, const barny_glass_panel_t *panel,
            cairo_surface_t *bg, cairo_surface_t *body, double m)
{
	cairo_surface_t *surf;
	cairo_t         *cr;

	surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, panel->w,
	                                  panel->h);
	cr   = cairo_create(surf);
	barny_glass_panel_morph(cr, panel, bg, body, m);
	cairo_destroy(cr);
	check(name, surf);
	cairo_surface_destroy(surf);
}

static void
morph_cases(void)
{
	static const double ms[] = {
		0.02, 0.05, 0.10, 0.20, 0.35, 0.50, 0.75, 0.95, 1.00,
	};
	barny_glass_panel_t panel;
	cairo_surface_t    *bg;
	char                name[64];
	size_t              i;
	int                 top;
	int                 a;

	for (top = 0; top < 2; top++) {
		panel_init(&panel, top);
		for (a = 0; a < 2; a++) {
			bg = texture(panel.w, panel.h, 41, a ? 96 : 255, 255);
			for (i = 0; i < sizeof(ms) / sizeof(ms[0]); i++) {
				snprintf(name, sizeof(name),
				         "morph.%s.%s.m%03d",
				         top ? "top" : "bot",
				         a ? "alpha" : "opaque",
				         (int)(ms[i] * 100.0 + 0.5));
				morph_check(name, &panel, bg, NULL, ms[i]);
			}
			if (!a) {
				snprintf(name, sizeof(name),
				         "morph.%s.opaque.m101",
				         top ? "top" : "bot");
				morph_check(name, &panel, bg, NULL, 1.01);
			}
			cairo_surface_destroy(bg);
		}
	}
}

static void
popup_case(cairo_surface_t *wall, bool top, const char *side)
{
	static const double ms[] = { 0.75, 0.95, 1.00 };
	barny_glass_panel_t panel;
	cairo_surface_t    *glass;
	cairo_surface_t    *body;
	cairo_surface_t    *surf;
	cairo_t            *cr;
	bar_t              *b;
	char                name[64];
	size_t              i;

	b    = bar_new(wall, top, 0.8);
	body = content(PANEL_W, PANEL_H - NECK_H);
	panel_init(&panel, top);

	if (wall) {
		glass = barny_glass_panel_bg(&b->state, &b->out, &panel);
		snprintf(name, sizeof(name), "popup.%s.glass_src", side);
		check(name, glass);
		for (i = 0; i < sizeof(ms) / sizeof(ms[0]); i++) {
			snprintf(name, sizeof(name), "popup.%s.m%03d", side,
			         (int)(ms[i] * 100.0 + 0.5));
			morph_check(name, &panel, glass, body, ms[i]);
		}
		cairo_surface_destroy(glass);
	}

	surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, panel.w,
	                                  panel.h);
	cr   = cairo_create(surf);
	barny_glass_panel_compose(cr, &b->state, &b->out, &panel, body);
	cairo_destroy(cr);
	snprintf(name, sizeof(name), "popup.%s.compose", side);
	check(name, surf);

	cairo_surface_destroy(surf);
	cairo_surface_destroy(body);
	bar_free(b);
}

static void
bead_check(const char *name, cairo_surface_t *src, bool on_src, double cx,
           double cy, double hw, double hh, double alpha)
{
	barny_glass_bubble_t bead;
	cairo_surface_t     *surf;
	cairo_t             *cr;

	bead.cx     = cx;
	bead.cy     = cy;
	bead.hw     = hw;
	bead.hh     = hh;
	bead.radius = 9;
	bead.alpha  = alpha;

	surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, MENU_W, MENU_H);
	cr   = cairo_create(surf);
	if (on_src) {
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_paint(cr);
		cairo_set_source_rgba(cr, 0, 0, 0, 0);
	}
	barny_glass_bubble_draw(cr, src, &bead);
	cairo_destroy(cr);
	check(name, surf);
	cairo_surface_destroy(surf);
}

static void
bead_cases(void)
{
	cairo_surface_t *src   = menu_src(255);
	cairo_surface_t *alpha = menu_src(140);

	bead_check("bead.row", src, false, 132.0, 58.0, 128.0, 13.0, 1.0);
	bead_check("bead.cell", src, false, 40.0, 40.0, 16.0, 16.0, 1.0);
	bead_check("bead.pop", src, false, 132.0, 86.0, 110.08, 11.18, 0.5);
	bead_check("bead.travel", src, false, 117.3, 61.6, 140.2, 10.4, 0.9);
	bead_check("bead.edge", src, false, 6.5, 20.0, 20.0, 12.0, 1.0);
	bead_check("bead.alpha.row", alpha, false, 132.0, 58.0, 128.0, 13.0,
	           1.0);
	bead_check("bead.on_rest", src, true, 132.0, 102.0, 128.0, 13.0, 1.0);

	cairo_surface_destroy(src);
	cairo_surface_destroy(alpha);
}

int
main(int argc, char **argv)
{
	cairo_surface_t *wall;
	cairo_surface_t *veil;
	size_t           i;
	int              arg;

	for (arg = 1; arg < argc; arg++) {
		if (strcmp(argv[arg], "--print") == 0) {
			print_mode = true;
		} else if (strcmp(argv[arg], "--png") == 0 && arg + 1 < argc) {
			png_dir = argv[++arg];
		} else {
			fprintf(stderr, "usage: %s [--print] [--png DIR]\n",
			        argv[0]);
			return 2;
		}
	}

	wall = texture(WALL_W, WALL_H, 7, 255, 255);
	veil = texture(WALL_W, WALL_H, 7, 150, 255);

	if (print_mode)
		printf("static const golden_t golden[] = {\n");

	lens_cases();
	bar_cases(wall, veil);
	partial_case(wall, false, "partial.bot", 1, 517.37, 560.9);
	partial_case(wall, false, "partial.bot.corner", 1, 60.0, 200.0);
	partial_case(wall, true, "partial.top", 1, 700.2, 655.0);
	partial_case(wall, false, "partial2.bot", 2, 517.37, 560.9);
	partial_case(wall, false, "partial2.bot.corner", 2, 60.0, 200.0);
	morph_cases();
	popup_case(wall, false, "bot");
	popup_case(wall, true, "top");
	popup_case(NULL, false, "bot.nowall");
	bead_cases();

	cairo_surface_destroy(wall);
	cairo_surface_destroy(veil);

	if (print_mode) {
		printf("};\n");
		return failures ? 1 : 0;
	}

	for (i = 0; i < N_GOLDEN; i++) {
		if (!seen[i]) {
			printf("FAIL %-32s golden hash never produced\n",
			       golden[i].name);
			failures++;
		}
	}

	printf("\n%d hashes, %d asserts: %s\n", checked, asserts,
	       failures ? "FAILED" : "all match");
	if (failures)
		printf("%d failure(s); after an intended change, re-baseline "
		       "with --print\n",
		       failures);

	return failures ? 1 : 0;
}
