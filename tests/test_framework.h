#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

static int _test_pass_count = 0;
static int _test_fail_count = 0;
static int _test_total_count = 0;
static const char *_current_test_name = NULL;

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_RESET   "\x1b[0m"

#define TEST_SUITE_BEGIN(name) \
    do { \
        printf(COLOR_BLUE "\n=== Test Suite: %s ===\n" COLOR_RESET, name); \
        _test_pass_count = 0; \
        _test_fail_count = 0; \
        _test_total_count = 0; \
    } while(0)

#define TEST_SUITE_END() \
    do { \
        printf(COLOR_BLUE "\n--- Results: %d/%d passed", \
               _test_pass_count, _test_total_count); \
        if (_test_fail_count > 0) { \
            printf(COLOR_RED " (%d failed)" COLOR_RESET, _test_fail_count); \
        } else { \
            printf(COLOR_GREEN " (all passed)" COLOR_RESET); \
        } \
        printf(" ---\n" COLOR_RESET); \
    } while(0)

#define TEST(name) \
    for (int _test_loop = (_current_test_name = name, _test_total_count++, printf("  [TEST] %s... ", name), 1); \
         _test_loop; \
         _test_loop = 0, (_test_pass_count++, printf(COLOR_GREEN "PASS\n" COLOR_RESET)))

#define TEST_FAIL(msg, ...) \
    do { \
        _test_fail_count++; \
        _test_pass_count--; /* Will be incremented at end of TEST block */ \
        printf(COLOR_RED "FAIL\n" COLOR_RESET); \
        printf(COLOR_RED "         -> " msg "\n" COLOR_RESET, ##__VA_ARGS__); \
        break; \
    } while(0)

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            TEST_FAIL("Expected TRUE: %s", #expr); \
        } \
    } while(0)

#define ASSERT_FALSE(expr) \
    do { \
        if (expr) { \
            TEST_FAIL("Expected FALSE: %s", #expr); \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            TEST_FAIL("Expected %s == %s", #expected, #actual); \
        } \
    } while(0)

#define ASSERT_EQ_INT(expected, actual) \
    do { \
        int _e = (expected); \
        int _a = (actual); \
        if (_e != _a) { \
            TEST_FAIL("Expected %d, got %d", _e, _a); \
        } \
    } while(0)

#define ASSERT_EQ_DBL(expected, actual, epsilon) \
    do { \
        double _e = (expected); \
        double _a = (actual); \
        if (fabs(_e - _a) > (epsilon)) { \
            TEST_FAIL("Expected %.6f, got %.6f (epsilon=%.6f)", _e, _a, (double)(epsilon)); \
        } \
    } while(0)

#define ASSERT_EQ_STR(expected, actual) \
    do { \
        const char *_e = (expected); \
        const char *_a = (actual); \
        if (_e == NULL && _a == NULL) break; \
        if (_e == NULL || _a == NULL || strcmp(_e, _a) != 0) { \
            TEST_FAIL("Expected \"%s\", got \"%s\"", _e ? _e : "(null)", _a ? _a : "(null)"); \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            TEST_FAIL("Expected non-NULL: %s", #ptr); \
        } \
    } while(0)

#define ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            TEST_FAIL("Expected NULL: %s", #ptr); \
        } \
    } while(0)

#define ASSERT_IN_RANGE(val, min, max) \
    do { \
        double _v = (val); \
        double _min = (min); \
        double _max = (max); \
        if (_v < _min || _v > _max) { \
            TEST_FAIL("Value %.4f not in range [%.4f, %.4f]", _v, _min, _max); \
        } \
    } while(0)

#define TEST_GET_FAIL_COUNT() (_test_fail_count)

#define TEST_MAIN_BEGIN() \
    int main(void) { \
        int _total_failures = 0; \
        printf(COLOR_YELLOW "\n╔══════════════════════════════════════╗\n" \
               "║       BARNY UNIT TEST RUNNER         ║\n" \
               "╚══════════════════════════════════════╝\n" COLOR_RESET);

#define TEST_MAIN_END() \
        printf(COLOR_YELLOW "\n╔══════════════════════════════════════╗\n"); \
        if (_total_failures == 0) { \
            printf("║  " COLOR_GREEN "ALL TESTS PASSED" COLOR_YELLOW "                    ║\n"); \
        } else { \
            printf("║  " COLOR_RED "%d TEST(S) FAILED" COLOR_YELLOW "                   ║\n", _total_failures); \
        } \
        printf("╚══════════════════════════════════════╝\n" COLOR_RESET); \
        return _total_failures > 0 ? 1 : 0; \
    }

#define RUN_SUITE(suite_func) \
    do { \
        suite_func(); \
        _total_failures += _test_fail_count; \
    } while(0)

#endif /* TEST_FRAMEWORK_H */
