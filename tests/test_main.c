#include "test_framework.h"

extern void
test_config_defaults(void);
extern void
test_config_load(void);

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
test_brightness(void);
extern void
test_apply_displacement(void);
extern void
test_file_extension(void);

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

TEST_MAIN_BEGIN()

printf("\n--- Configuration Tests ---\n");
RUN_SUITE(test_config_defaults);
RUN_SUITE(test_config_load);

printf("\n--- Liquid Glass Effect Tests ---\n");
RUN_SUITE(test_perlin_math);
RUN_SUITE(test_perlin_noise);
RUN_SUITE(test_perlin_fbm);
RUN_SUITE(test_displacement_map);
RUN_SUITE(test_blur_surface);
RUN_SUITE(test_brightness);
RUN_SUITE(test_apply_displacement);
RUN_SUITE(test_file_extension);

printf("\n--- Module System Tests ---\n");
RUN_SUITE(test_module_register);
RUN_SUITE(test_module_lifecycle);
RUN_SUITE(test_module_factories);
RUN_SUITE(test_module_positions);
RUN_SUITE(test_module_data);

TEST_MAIN_END()
