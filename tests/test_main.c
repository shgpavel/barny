#include "test_framework.h"

extern void
test_config_defaults(void);
extern void
test_config_load(void);
extern void
test_config_edge_cases(void);

extern void
test_perlin_math(void);
extern void
test_perlin_noise(void);
extern void
test_perlin_fbm(void);
extern void
test_displacement_map(void);
extern void
test_blur_surface(void);
extern void
test_apply_displacement(void);
extern void
test_file_extension(void);
extern void
test_damage_rects(void);
extern void
test_lens_partial_redraw(void);
extern void
test_rim_flow(void);
extern void
test_lens_channel_tap(void);
extern void
test_lens_opaque_fast_path(void);
extern void
test_prism_stops(void);
extern void
test_lens_map_lifetime(void);
extern void
test_spring_step(void);
extern void
test_shm_create(void);
extern void
test_lens_advance(void);
extern void
test_glass_panel_advance(void);
extern void
test_menu_bead_advance(void);

extern void
test_module_register(void);
extern void
test_module_lifecycle(void);
extern void
test_module_factories(void);
extern void
test_module_positions(void);
extern void
test_module_data(void);
extern void
test_module_layout_basics(void);
extern void
test_module_layout_parsing_and_ops(void);
extern void
test_module_layout_runtime_apply(void);
extern void
test_module_layout_edge_cases(void);
extern void
test_module_layout_config_write(void);

extern void
test_clock_module_behavior(void);
extern void
test_ram_module_behavior(void);
extern void
test_disk_module_behavior(void);
extern void
test_sysinfo_module_behavior(void);
extern void
test_fileread_module_behavior(void);
extern void
test_network_module_behavior(void);
extern void
test_weather_module_behavior(void);
extern void
test_crypto_module_behavior(void);
extern void
test_module_destroy_safety(void);
extern void
test_module_null_font(void);

TEST_MAIN_BEGIN()

printf("\n--- Configuration Tests ---\n");
RUN_SUITE(test_config_defaults);
RUN_SUITE(test_config_load);
RUN_SUITE(test_config_edge_cases);
RUN_SUITE(test_shm_create);

printf("\n--- Liquid Glass Effect Tests ---\n");
RUN_SUITE(test_spring_step);
RUN_SUITE(test_lens_advance);
RUN_SUITE(test_glass_panel_advance);
RUN_SUITE(test_menu_bead_advance);
RUN_SUITE(test_perlin_math);
RUN_SUITE(test_perlin_noise);
RUN_SUITE(test_perlin_fbm);
RUN_SUITE(test_displacement_map);
RUN_SUITE(test_blur_surface);
RUN_SUITE(test_apply_displacement);
RUN_SUITE(test_file_extension);
RUN_SUITE(test_lens_channel_tap);
RUN_SUITE(test_prism_stops);
RUN_SUITE(test_lens_opaque_fast_path);
RUN_SUITE(test_damage_rects);
RUN_SUITE(test_lens_partial_redraw);
RUN_SUITE(test_rim_flow);
RUN_SUITE(test_lens_map_lifetime);

printf("\n--- Module System Tests ---\n");
RUN_SUITE(test_module_register);
RUN_SUITE(test_module_lifecycle);
RUN_SUITE(test_module_factories);
RUN_SUITE(test_module_positions);
RUN_SUITE(test_module_data);
RUN_SUITE(test_module_layout_basics);
RUN_SUITE(test_module_layout_parsing_and_ops);
RUN_SUITE(test_module_layout_runtime_apply);
RUN_SUITE(test_module_layout_edge_cases);
RUN_SUITE(test_module_layout_config_write);

printf("\n--- New Module Behavior Tests ---\n");
RUN_SUITE(test_clock_module_behavior);
RUN_SUITE(test_ram_module_behavior);
RUN_SUITE(test_disk_module_behavior);
RUN_SUITE(test_sysinfo_module_behavior);
RUN_SUITE(test_fileread_module_behavior);
RUN_SUITE(test_network_module_behavior);
RUN_SUITE(test_weather_module_behavior);
RUN_SUITE(test_crypto_module_behavior);

printf("\n--- Module Safety Tests ---\n");
RUN_SUITE(test_module_destroy_safety);
RUN_SUITE(test_module_null_font);

TEST_MAIN_END()
