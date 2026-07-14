#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char *
barny_trim(char *s)
{
	char *end;

	while (isspace((unsigned char)*s)) {
		s++;
	}

	if (*s == '\0') {
		return s;
	}

	end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) {
		end--;
	}
	end[1] = '\0';

	return s;
}

void
barny_free_string_array(char **arr, size_t count)
{
	size_t i;

	if (!arr) {
		return;
	}

	for (i = 0; i < count; i++) {
		free(arr[i]);
	}

	free((void *)arr);
}

char **
barny_parse_csv(const char *input, size_t *out_count)
{
	size_t      max_items;
	const char *p;
	char      **items;
	char       *tmp;
	size_t      idx;
	char       *saveptr;
	char       *token;
	char       *trimmed;
	char       *dup;
	size_t      i;

	if (out_count) {
		*out_count = 0;
	}

	if (!input || !*input) {
		return NULL;
	}

	max_items = 1;
	for (p = input; *p; p++) {
		if (*p == ',') {
			max_items++;
		}
	}

	items = (char **)calloc(max_items + 1, sizeof(*items));
	if (!items) {
		return NULL;
	}

	tmp = strdup(input);
	if (!tmp) {
		free((void *)items);
		return NULL;
	}

	idx     = 0;
	saveptr = NULL;
	token   = strtok_r(tmp, ",", &saveptr);
	while (token) {
		trimmed = barny_trim(token);
		if (*trimmed) {
			dup = strdup(trimmed);
			if (!dup) {
				for (i = 0; i < idx; i++) {
					free(items[i]);
				}
				free((void *)items);
				free(tmp);
				return NULL;
			}
			items[idx++] = dup;
		}
		token = strtok_r(NULL, ",", &saveptr);
	}

	free(tmp);

	if (idx == 0) {
		free((void *)items);
		return NULL;
	}

	items[idx] = NULL;
	if (out_count) {
		*out_count = idx;
	}

	return items;
}

uint64_t
barny_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* Springs integrate against this: at 60 Hz a millisecond clock quantises the
   frame delta to 16 or 17 ms, and that 3% jitter is visible as a shimmer in a
   stiff spring. */
uint64_t
barny_now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

void
barny_format_bytes(char *buf, size_t buflen, unsigned long long bytes,
                   int decimals, bool unit_space)
{
	const char *sp   = unit_space ? " " : "";
	int         prec = decimals == 0 ? 0 : (decimals == 2 ? 2 : 1);
	double      gb   = bytes / (1024.0 * 1024.0 * 1024.0);
	double      mb;

	if (gb >= 1000.0) {
		snprintf(buf, buflen, "%.*f%sT", prec, gb / 1024.0, sp);
	} else if (gb >= 1.0) {
		snprintf(buf, buflen, "%.*f%sG", prec, gb, sp);
	} else {
		mb = bytes / (1024.0 * 1024.0);
		snprintf(buf, buflen, "%.*f%sM", prec, mb, sp);
	}
}
