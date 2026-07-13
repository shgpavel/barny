#include "test_framework.h"

extern void
test_parse_hex_color(void);
extern void
test_trim(void);
extern void
test_parse_bool(void);
extern void
test_parse_int_clamped(void);

extern void
test_build_time_string(void);
extern void
test_build_date_string(void);
extern void
test_clock_display_str(void);

extern void
test_format_bytes(void);

extern void
test_format_size(void);

extern void
test_is_physical_interface(void);

extern void
test_parse_workspaces(void);
extern void
test_get_workspace_label(void);
extern void
test_is_square_shape(void);
extern void
test_workspace_click_uses_module_rect(void);

extern void
test_tray_update_width_and_dirty(void);
extern void
test_tray_click_handling(void);

TEST_MAIN_BEGIN()

printf("\n--- Config Internal Functions ---\n");
RUN_SUITE(test_parse_hex_color);
RUN_SUITE(test_trim);
RUN_SUITE(test_parse_bool);
RUN_SUITE(test_parse_int_clamped);

printf("\n--- Clock Internal Functions ---\n");
RUN_SUITE(test_build_time_string);
RUN_SUITE(test_build_date_string);
RUN_SUITE(test_clock_display_str);

printf("\n--- Disk Internal Functions ---\n");
RUN_SUITE(test_format_bytes);

printf("\n--- RAM Internal Functions ---\n");
RUN_SUITE(test_format_size);

printf("\n--- Network Internal Functions ---\n");
RUN_SUITE(test_is_physical_interface);

printf("\n--- Workspace Internal Functions ---\n");
RUN_SUITE(test_parse_workspaces);
RUN_SUITE(test_get_workspace_label);
RUN_SUITE(test_is_square_shape);
RUN_SUITE(test_workspace_click_uses_module_rect);

printf("\n--- Tray Internal Functions ---\n");
RUN_SUITE(test_tray_update_width_and_dirty);
RUN_SUITE(test_tray_click_handling);

TEST_MAIN_END()
