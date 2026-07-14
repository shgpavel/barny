#ifndef BARNY_UTIL_H
#define BARNY_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

char *
barny_trim(char *s);

char **
barny_parse_csv(const char *input, size_t *out_count);

void
barny_free_string_array(char **arr, size_t count);

uint64_t
barny_now_ms(void);

uint64_t
barny_now_us(void);

void
barny_format_bytes(char *buf, size_t buflen, unsigned long long bytes,
                   int decimals, bool unit_space);

#endif
