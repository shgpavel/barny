#ifndef BARNY_HELPER_UTIL_H
#define BARNY_HELPER_UTIL_H

#include <stddef.h>
#include <cjson/cJSON.h>

/*
 * Shared utilities for barny helper executables.
 *
 * All allocations returned by these helpers are owned by the caller and
 * must be released with the documented free routines (or the standard
 * free()/cJSON_Delete() where appropriate).
 */

/*
 * helper_trim: trim ASCII whitespace from both ends of `s` in place.
 * Returns a pointer to the first non-space character within the same
 * underlying buffer. Safe to call with NULL (returns NULL).
 */
char *helper_trim(char *s);

/*
 * helper_parse_csv: split `input` on commas, allocate an array of
 * strdup'd, trimmed values, skip empty tokens. On success, `*out_count`
 * receives the number of tokens and the return value is a malloc'd
 * array of malloc'd strings (free with helper_free_string_array).
 *
 * Returns NULL and sets *out_count to 0 on allocation failure or if
 * `input` is NULL/empty/contains only empty tokens.
 */
char **helper_parse_csv(const char *input, size_t *out_count);

/*
 * helper_free_string_array: free an array previously returned by
 * helper_parse_csv. Safe with NULL.
 */
void helper_free_string_array(char **arr, size_t count);

/*
 * helper_http_fetch: perform a libcurl HTTP GET on `url`. Returns a
 * malloc'd, NUL-terminated body buffer on success, or NULL on any
 * failure. Caller frees with free().
 */
char *helper_http_fetch(const char *url, long timeout_seconds);

/*
 * helper_fetch_json: helper_http_fetch + cJSON_Parse. Returns a parsed
 * cJSON tree on success, or NULL on fetch/parse failure. Caller frees
 * with cJSON_Delete().
 */
cJSON *helper_fetch_json(const char *url, long timeout_seconds);

#endif /* BARNY_HELPER_UTIL_H */
