#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

char *
barny_trim(char *s)
{
	while (isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '\0') {
		return s;
	}

	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end)) {
		end--;
	}
	end[1] = '\0';

	return s;
}

void
barny_free_string_array(char **arr, size_t count)
{
	if (!arr) {
		return;
	}
	for (size_t i = 0; i < count; i++) {
		free(arr[i]);
	}
	free(arr);
}

char **
barny_parse_csv(const char *input, size_t *out_count)
{
	if (out_count) {
		*out_count = 0;
	}
	if (!input || !*input) {
		return NULL;
	}

	/* Upper bound on element count: commas + 1. */
	size_t max_items = 1;
	for (const char *p = input; *p; p++) {
		if (*p == ',') {
			max_items++;
		}
	}

	char **items = (char **)calloc(max_items + 1, sizeof(*items));
	if (!items) {
		return NULL;
	}

	char *tmp = strdup(input);
	if (!tmp) {
		free(items);
		return NULL;
	}

	size_t idx     = 0;
	char  *saveptr = NULL;
	char  *token   = strtok_r(tmp, ",", &saveptr);
	while (token) {
		char *trimmed = barny_trim(token);
		if (*trimmed) {
			char *dup = strdup(trimmed);
			if (!dup) {
				/* Allocation failure: roll back partial result. */
				for (size_t i = 0; i < idx; i++) {
					free(items[i]);
				}
				free(items);
				free(tmp);
				return NULL;
			}
			items[idx++] = dup;
		}
		token = strtok_r(NULL, ",", &saveptr);
	}

	free(tmp);

	if (idx == 0) {
		free(items);
		return NULL;
	}

	items[idx] = NULL;
	if (out_count) {
		*out_count = idx;
	}
	return items;
}
