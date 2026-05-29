#define _DEFAULT_SOURCE
#include "helper_util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

char *
helper_trim(char *s)
{
	if (!s)
		return NULL;

	while (isspace((unsigned char)*s))
		s++;

	if (*s == '\0')
		return s;

	char *end = s + strlen(s) - 1;
	while (end > s && isspace((unsigned char)*end))
		end--;
	end[1] = '\0';

	return s;
}

void
helper_free_string_array(char **arr, size_t count)
{
	if (!arr)
		return;
	for (size_t i = 0; i < count; i++)
		free(arr[i]);
	free((void *)arr);
}

char **
helper_parse_csv(const char *input, size_t *out_count)
{
	if (out_count)
		*out_count = 0;

	if (!input || !*input)
		return NULL;

	/* Upper bound on token count: commas + 1. */
	size_t max_tokens = 1;
	for (const char *p = input; *p; p++) {
		if (*p == ',')
			max_tokens++;
	}

	char **result = (char **)calloc(max_tokens, sizeof(*result));
	if (!result)
		return NULL;

	char *tmp = strdup(input);
	if (!tmp) {
		free((void *)result);
		return NULL;
	}

	size_t count   = 0;
	char  *saveptr = NULL;
	char  *token   = strtok_r(tmp, ",", &saveptr);
	while (token) {
		char *trimmed = helper_trim(token);
		if (*trimmed) {
			result[count] = strdup(trimmed);
			if (!result[count]) {
				helper_free_string_array(result, count);
				free(tmp);
				return NULL;
			}
			count++;
		}
		token = strtok_r(NULL, ",", &saveptr);
	}

	free(tmp);

	if (count == 0) {
		free((void *)result);
		return NULL;
	}

	if (out_count)
		*out_count = count;
	return result;
}

struct helper_response_buf {
	char  *data;
	size_t size;
};

static size_t
helper_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	size_t                       realsize = size * nmemb;
	struct helper_response_buf  *buf      = userdata;

	char *new_data = realloc(buf->data, buf->size + realsize + 1);
	if (!new_data)
		return 0;

	buf->data = new_data;
	memcpy(buf->data + buf->size, ptr, realsize);
	buf->size            += realsize;
	buf->data[buf->size]  = '\0';
	return realsize;
}

char *
helper_http_fetch(const char *url, long timeout_seconds)
{
	if (!url)
		return NULL;

	CURL *curl = curl_easy_init();
	if (!curl)
		return NULL;

	struct helper_response_buf buf = { .data = NULL, .size = 0 };

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, helper_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
	if (timeout_seconds > 0)
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		fprintf(stderr, "curl failed: %s\n", curl_easy_strerror(res));
		free(buf.data);
		return NULL;
	}

	if (!buf.data) {
		/* Successful zero-byte response: return an empty C-string. */
		buf.data = calloc(1, 1);
	}

	return buf.data;
}

cJSON *
helper_fetch_json(const char *url, long timeout_seconds)
{
	char *body = helper_http_fetch(url, timeout_seconds);
	if (!body)
		return NULL;

	cJSON *json = cJSON_Parse(body);
	free(body);
	return json;
}
