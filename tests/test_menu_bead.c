#define _GNU_SOURCE
#include "test_framework.h"
#include "barny.h"

barny_menu_item_t *
barny_dbusmenu_get_layout(barny_state_t *state, const char *service,
                          const char *menu_path)
{
	(void)state;
	(void)service;
	(void)menu_path;
	return NULL;
}

void
barny_dbusmenu_free(barny_menu_item_t *root)
{
	(void)root;
}

void
barny_dbusmenu_about_to_show(barny_state_t *state, const char *service,
                             const char *menu_path, int id)
{
	(void)state;
	(void)service;
	(void)menu_path;
	(void)id;
}

void
barny_dbusmenu_event_clicked(barny_state_t *state, const char *service,
                             const char *menu_path, int id)
{
	(void)state;
	(void)service;
	(void)menu_path;
	(void)id;
}

sni_item_t *
barny_sni_host_get_items(barny_state_t *state)
{
	(void)state;
	return NULL;
}

void
barny_sni_item_activate(barny_state_t *state, sni_item_t *item, int x, int y)
{
	(void)state;
	(void)item;
	(void)x;
	(void)y;
}

void
barny_sni_item_secondary_activate(barny_state_t *state, sni_item_t *item, int x,
                                  int y)
{
	(void)state;
	(void)item;
	(void)x;
	(void)y;
}

bool
barny_sni_item_is_menu(barny_state_t *state, sni_item_t *item)
{
	(void)state;
	(void)item;
	return false;
}

char *
barny_sni_item_menu_path(barny_state_t *state, sni_item_t *item)
{
	(void)state;
	(void)item;
	return NULL;
}

#include "../src/modules/menu.c"

static barny_state_t bead_state;

static void
bead_setup(barny_menu_t *m, menu_row_t *rows)
{
	memset(&bead_state, 0, sizeof(bead_state));
	memset(m, 0, sizeof(*m));
	bead_state.config.popup_animations = true;
	rows[0].y   = 10;
	rows[0].h   = 30;
	rows[1].y   = 40;
	rows[1].h   = 30;
	m->state     = &bead_state;
	m->kind      = MENU_KIND_SNI;
	m->rows      = rows;
	m->row_count = 2;
	m->menu_w    = 200;
	m->hover     = 0;
}

void
test_menu_bead_advance(void)
{
	TEST_SUITE_BEGIN("Menu Bead Stepper");

	TEST("bead pops in place below the visibility threshold")
	{
		barny_menu_t m;
		menu_row_t   rows[2];

		bead_setup(&m, rows);
		m.bead_x = 3.0;
		m.bead_y = 300.0;
		ASSERT_TRUE(menu_bead_advance(&m, 0.01));
		ASSERT_EQ_DBL(100.0, m.bead_x, 0.0);
		ASSERT_EQ_DBL(25.0, m.bead_y, 0.0);
		ASSERT_EQ_DBL(0.0, m.bead_vx, 0.0);
		ASSERT_EQ_DBL(0.0, m.bead_vy, 0.0);
		ASSERT_TRUE(m.bead_scale > 0.0);
	}

	TEST("bead travels to the next row and settles exactly")
	{
		barny_menu_t m;
		menu_row_t   rows[2];
		int          in_unit = 1;
		int          i;

		bead_setup(&m, rows);
		for (i = 0; i < 300 && menu_bead_advance(&m, 0.01); i++)
			;
		ASSERT_TRUE(i < 300);
		ASSERT_EQ_DBL(1.0, m.bead_scale, 0.0);
		m.hover = 1;
		for (i = 0; i < 300 && menu_bead_advance(&m, 0.01); i++)
			in_unit &= m.bead_scale >= 0.0 && m.bead_scale <= 1.0;
		ASSERT_TRUE(i < 300);
		ASSERT_TRUE(in_unit);
		ASSERT_EQ_DBL(55.0, m.bead_y, 0.0);
		ASSERT_EQ_DBL(0.0, m.bead_vy, 0.0);
	}

	TEST("zero elapsed time does not move the bead")
	{
		barny_menu_t m;
		menu_row_t   rows[2];

		bead_setup(&m, rows);
		m.bead_x     = 100.0;
		m.bead_y     = 25.0;
		m.bead_scale = 1.0;
		m.hover      = 1;
		ASSERT_TRUE(menu_bead_advance(&m, 0.0));
		ASSERT_EQ_DBL(25.0, m.bead_y, 0.0);
		ASSERT_EQ_DBL(1.0, m.bead_scale, 0.0);
	}

	TEST_SUITE_END();
}
