/*
 * Tests for static functions in workspace.c
 * We #include the source file directly to access static functions.
 */

#include "test_framework.h"
#include "barny.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static int test_ipc_send_called = 0;
static uint32_t test_ipc_last_type = 0;
static char test_ipc_last_payload[256];

static void
reset_ipc_state(void)
{
	test_ipc_send_called = 0;
	test_ipc_last_type = 0;
	test_ipc_last_payload[0] = '\0';
}

static int
test_sway_ipc_send(barny_state_t *state, uint32_t type, const char *payload)
{
	(void)state;
	test_ipc_send_called++;
	test_ipc_last_type = type;
	if (payload) {
		strncpy(test_ipc_last_payload, payload, sizeof(test_ipc_last_payload) - 1);
		test_ipc_last_payload[sizeof(test_ipc_last_payload) - 1] = '\0';
	} else {
		test_ipc_last_payload[0] = '\0';
	}
	return 0;
}

static char *
test_sway_ipc_recv(barny_state_t *state, uint32_t *type)
{
	(void)state;
	if (type) {
		*type = 0;
	}
	return strdup("ok");
}

static char *
test_sway_ipc_recv_sync(barny_state_t *state, uint32_t *type, int timeout_ms)
{
	(void)timeout_ms;
	return test_sway_ipc_recv(state, type);
}

static int
test_sway_ipc_subscribe(barny_state_t *state, const char *events)
{
	(void)state;
	(void)events;
	return 0;
}

#define barny_sway_ipc_send test_sway_ipc_send
#define barny_sway_ipc_recv test_sway_ipc_recv
#define barny_sway_ipc_recv_sync test_sway_ipc_recv_sync
#define barny_sway_ipc_subscribe test_sway_ipc_subscribe

/* Include workspace.c directly to access static functions */
#include "../src/modules/workspace.c"

#undef barny_sway_ipc_send
#undef barny_sway_ipc_recv
#undef barny_sway_ipc_recv_sync
#undef barny_sway_ipc_subscribe

void
test_parse_workspaces(void)
{
	TEST_SUITE_BEGIN("parse_workspaces");

	workspace_data_t data;
	memset(&data, 0, sizeof(data));

	TEST("parses empty JSON array")
	{
		parse_workspaces(&data, "[]");
		ASSERT_EQ_INT(0, data.workspace_count);
	}

	TEST("parses single workspace")
	{
		parse_workspaces(&data,
			"[{\"num\": 1, \"name\": \"1\", \"focused\": true, \"visible\": true, \"urgent\": false}]");
		ASSERT_EQ_INT(1, data.workspace_count);
		ASSERT_EQ_INT(1, data.workspaces[0].num);
		ASSERT_EQ_STR("1", data.workspaces[0].name);
		ASSERT_TRUE(data.workspaces[0].focused);
		ASSERT_TRUE(data.workspaces[0].visible);
		ASSERT_FALSE(data.workspaces[0].urgent);
	}

	TEST("parses multiple workspaces")
	{
		parse_workspaces(&data,
			"["
			"{\"num\": 1, \"name\": \"code\", \"focused\": false, \"visible\": false, \"urgent\": false},"
			"{\"num\": 2, \"name\": \"web\", \"focused\": true, \"visible\": true, \"urgent\": false},"
			"{\"num\": 3, \"name\": \"chat\", \"focused\": false, \"visible\": false, \"urgent\": true}"
			"]");
		ASSERT_EQ_INT(3, data.workspace_count);
		ASSERT_EQ_INT(1, data.workspaces[0].num);
		ASSERT_EQ_INT(2, data.workspaces[1].num);
		ASSERT_EQ_INT(3, data.workspaces[2].num);
		ASSERT_TRUE(data.workspaces[1].focused);
		ASSERT_TRUE(data.workspaces[2].urgent);
	}

	TEST("handles missing fields")
	{
		parse_workspaces(&data, "[{\"num\": 5}]");
		ASSERT_EQ_INT(1, data.workspace_count);
		ASSERT_EQ_INT(5, data.workspaces[0].num);
		/* Missing name defaults to "?" */
		ASSERT_EQ_STR("?", data.workspaces[0].name);
		ASSERT_FALSE(data.workspaces[0].focused);
	}

	TEST("respects MAX_WORKSPACES limit")
	{
		/* Create JSON with 12 workspaces - should only parse MAX_WORKSPACES (10) */
		const char *json =
			"["
			"{\"num\": 1, \"name\": \"1\"},"
			"{\"num\": 2, \"name\": \"2\"},"
			"{\"num\": 3, \"name\": \"3\"},"
			"{\"num\": 4, \"name\": \"4\"},"
			"{\"num\": 5, \"name\": \"5\"},"
			"{\"num\": 6, \"name\": \"6\"},"
			"{\"num\": 7, \"name\": \"7\"},"
			"{\"num\": 8, \"name\": \"8\"},"
			"{\"num\": 9, \"name\": \"9\"},"
			"{\"num\": 10, \"name\": \"10\"},"
			"{\"num\": 11, \"name\": \"11\"},"
			"{\"num\": 12, \"name\": \"12\"}"
			"]";
		parse_workspaces(&data, json);
		ASSERT_EQ_INT(10, data.workspace_count);
	}

	TEST("replaces existing workspaces on re-parse")
	{
		parse_workspaces(&data, "[{\"num\": 1, \"name\": \"old\"}]");
		ASSERT_EQ_INT(1, data.workspace_count);
		ASSERT_EQ_STR("old", data.workspaces[0].name);

		parse_workspaces(&data, "[{\"num\": 2, \"name\": \"new\"}]");
		ASSERT_EQ_INT(1, data.workspace_count);
		ASSERT_EQ_STR("new", data.workspaces[0].name);
		ASSERT_EQ_INT(2, data.workspaces[0].num);
	}

	TEST("handles invalid JSON")
	{
		/* Clear existing */
		parse_workspaces(&data, "[]");
		ASSERT_EQ_INT(0, data.workspace_count);

		/* Try to parse invalid JSON */
		parse_workspaces(&data, "not valid json");
		/* Should not crash, workspace_count should still be 0 */
		ASSERT_EQ_INT(0, data.workspace_count);
	}

	/* Cleanup */
	for (int i = 0; i < data.workspace_count; i++) {
		free(data.workspaces[i].name);
	}

	TEST_SUITE_END();
}

void
test_get_workspace_label(void)
{
	TEST_SUITE_BEGIN("get_workspace_label");

	workspace_data_t data;
	barny_state_t state;
	memset(&data, 0, sizeof(data));
	memset(&state, 0, sizeof(state));
	barny_config_defaults(&state.config);
	data.state = &state;

	workspace_info_t ws;
	char buf[16];

	TEST("returns configured name when available")
	{
		/* Set up workspace names */
		char *names[] = { "term", "code", "web" };
		state.config.workspace_names = names;
		state.config.workspace_name_count = 3;

		ws.num = 2;
		ws.name = strdup("2");

		const char *label = get_workspace_label(&data, &ws, buf, sizeof(buf));
		ASSERT_EQ_STR("code", label);

		free(ws.name);
	}

	TEST("falls back to number when no configured name")
	{
		state.config.workspace_names = NULL;
		state.config.workspace_name_count = 0;

		ws.num = 5;
		ws.name = strdup("5");

		const char *label = get_workspace_label(&data, &ws, buf, sizeof(buf));
		ASSERT_EQ_STR("5", label);

		free(ws.name);
	}

	TEST("falls back to number for out-of-range workspace")
	{
		char *names[] = { "term", "code" };
		state.config.workspace_names = names;
		state.config.workspace_name_count = 2;

		ws.num = 5;  /* Beyond configured names */
		ws.name = strdup("5");

		const char *label = get_workspace_label(&data, &ws, buf, sizeof(buf));
		ASSERT_EQ_STR("5", label);

		free(ws.name);
	}

	TEST("handles workspace num=0")
	{
		state.config.workspace_names = NULL;
		state.config.workspace_name_count = 0;

		ws.num = 0;
		ws.name = strdup("scratch");

		const char *label = get_workspace_label(&data, &ws, buf, sizeof(buf));
		ASSERT_EQ_STR("0", label);

		free(ws.name);
	}

	TEST_SUITE_END();
}

void
test_is_square_shape(void)
{
	TEST_SUITE_BEGIN("is_square_shape");

	barny_config_t config;
	barny_config_defaults(&config);

	TEST("returns true for 'square'")
	{
		config.workspace_shape = "square";
		ASSERT_TRUE(is_square_shape(&config));
	}

	TEST("returns false for 'circle'")
	{
		config.workspace_shape = "circle";
		ASSERT_FALSE(is_square_shape(&config));
	}

	TEST("returns false for NULL (default)")
	{
		config.workspace_shape = NULL;
		ASSERT_FALSE(is_square_shape(&config));
	}

	TEST("returns false for empty string")
	{
		config.workspace_shape = "";
		ASSERT_FALSE(is_square_shape(&config));
	}

	TEST_SUITE_END();
}

void
test_workspace_click_uses_render_x(void)
{
	TEST_SUITE_BEGIN("workspace_click");

	barny_state_t state;
	memset(&state, 0, sizeof(state));
	barny_config_defaults(&state.config);
	state.config.workspace_indicator_size = 20;
	state.config.workspace_spacing = 5;

	workspace_data_t data;
	memset(&data, 0, sizeof(data));
	data.state = &state;
	data.workspace_count = 2;
	data.workspaces[0].num = 1;
	data.workspaces[1].num = 2;
	data.render_x = 100;

	barny_module_t mod;
	memset(&mod, 0, sizeof(mod));
	mod.data = &data;

	TEST("click uses render_x offset for hit testing")
	{
		reset_ipc_state();
		workspace_click(&mod, 272, data.render_x + 10, 0);
		ASSERT_EQ_INT(1, test_ipc_send_called);
		ASSERT_EQ_INT(0, (int)test_ipc_last_type);
		ASSERT_EQ_STR("workspace number 1", test_ipc_last_payload);
	}

	TEST("second workspace hit is offset-aware")
	{
		reset_ipc_state();
		workspace_click(&mod, 272, data.render_x + 10 + 20 + 5 + 10, 0);
		ASSERT_EQ_INT(1, test_ipc_send_called);
		ASSERT_EQ_STR("workspace number 2", test_ipc_last_payload);
	}

	TEST("click outside does not trigger IPC")
	{
		reset_ipc_state();
		workspace_click(&mod, 272, 10, 0);
		ASSERT_EQ_INT(0, test_ipc_send_called);
	}

	TEST_SUITE_END();
}
