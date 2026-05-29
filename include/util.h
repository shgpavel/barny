#ifndef BARNY_UTIL_H
#define BARNY_UTIL_H

#include <stddef.h>
#include <stdint.h>

/*
 * Trim leading and trailing ASCII whitespace from `s` in-place.
 *
 * The trailing whitespace is replaced with '\0'; the returned pointer
 * is `s` advanced past any leading whitespace (so it may point inside
 * the original buffer). The caller still owns `s` and is responsible
 * for freeing the original allocation, not the returned pointer.
 *
 * Returns `s` (advanced) on success; behavior is undefined when `s`
 * is NULL.
 */
char *
barny_trim(char *s);

/*
 * Split `input` on commas, trim each element, and return a newly
 * allocated array of `strdup`'d strings. Empty elements (after trim)
 * are skipped.
 *
 * On success, *out_count is set to the number of returned strings
 * and the returned array has that many entries plus a trailing NULL
 * sentinel. The caller must release the array with
 * barny_free_string_array().
 *
 * Returns NULL and sets *out_count to 0 when:
 *   - input is NULL or empty
 *   - all elements are empty after trimming
 *   - allocation fails
 */
char **
barny_parse_csv(const char *input, size_t *out_count);

/*
 * Free an array produced by barny_parse_csv. Safe to call with
 * NULL `arr`. Frees `count` element pointers and then the array
 * itself.
 */
void
barny_free_string_array(char **arr, size_t count);

uint64_t
barny_now_ms(void);

#endif /* BARNY_UTIL_H */
