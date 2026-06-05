#ifndef BARNY_HELPER_UTIL_H
#define BARNY_HELPER_UTIL_H

#include <stddef.h>
#include <cjson/cJSON.h>

char *
helper_trim(char *s);

char **
helper_parse_csv(const char *input, size_t *out_count);

void
helper_free_string_array(char **arr, size_t count);

char *
helper_http_fetch(const char *url, long timeout_seconds);

cJSON *
helper_fetch_json(const char *url, long timeout_seconds);

#endif
