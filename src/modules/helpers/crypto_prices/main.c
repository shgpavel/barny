#define _DEFAULT_SOURCE
#include <ctype.h>
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "helper_util.h"

#define WS_URL          "wss://ws.okx.com:8443/ws/v5/public"
#define OUTPUT_DIR      "/opt/barny/modules"
#define CONFIG_PATH     "/etc/barny/barny.conf"
#define RECONNECT_DELAY 5
#define MARKET_LEN      64
#define FILE_LEN        64

static const char *default_crypto_pairs[] = {
	"BTC-USDT-SWAP",
	"ETH-USDT-SWAP",
	"SOL-USDT-SWAP",
	"XRP-USDT-SWAP",
	"ADA-USDT-SWAP",
	"DOGE-USDT-SWAP",
	"DOT-USDT-SWAP",
};

typedef struct {
	char inst_id[MARKET_LEN];
	char file_name[FILE_LEN];
} tracked_pair_t;

static volatile int  running    = 1;
static tracked_pair_t *pairs    = NULL;
static int            pair_count = 0;

static void
signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static void
pair_file_from_market(const char *market, char *buf, size_t buf_size)
{
	char   slug[FILE_LEN];
	size_t out = 0;

	if (!buf || buf_size == 0)
		return;

	buf[0] = '\0';

	while (*market && *market != '-' && out + 1 < sizeof(slug)) {
		if (isalnum((unsigned char)*market))
			slug[out++] = (char)tolower((unsigned char)*market);
		market++;
	}

	if (out == 0) {
		snprintf(buf, buf_size, "crypto_price");
		return;
	}

	slug[out] = '\0';
	snprintf(buf, buf_size, "%s_price", slug);
}

static void
clear_pairs(void)
{
	free(pairs);
	pairs      = NULL;
	pair_count = 0;
}

static void
set_pairs_from_csv(const char *value)
{
	clear_pairs();

	size_t  token_count = 0;
	char  **tokens      = helper_parse_csv(value, &token_count);
	if (!tokens || token_count == 0) {
		helper_free_string_array(tokens, token_count);
		return;
	}

	pairs = calloc(token_count, sizeof(*pairs));
	if (!pairs) {
		helper_free_string_array(tokens, token_count);
		return;
	}

	for (size_t i = 0; i < token_count; i++) {
		snprintf(pairs[pair_count].inst_id,
		         sizeof(pairs[pair_count].inst_id), "%s", tokens[i]);
		pair_file_from_market(
		        tokens[i], pairs[pair_count].file_name,
		        sizeof(pairs[pair_count].file_name));
		pair_count++;
	}

	helper_free_string_array(tokens, token_count);

	if (pair_count == 0)
		clear_pairs();
}

static void
set_default_pairs(void)
{
	size_t count = sizeof(default_crypto_pairs)
	               / sizeof(default_crypto_pairs[0]);

	clear_pairs();
	pairs = calloc(count, sizeof(*pairs));
	if (!pairs)
		return;

	for (size_t i = 0; i < count; i++) {
		snprintf(pairs[i].inst_id, sizeof(pairs[i].inst_id), "%s",
		         default_crypto_pairs[i]);
		pair_file_from_market(default_crypto_pairs[i], pairs[i].file_name,
		                      sizeof(pairs[i].file_name));
	}
	pair_count = (int)count;
}

static void
load_config_file(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return;

	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *trimmed = helper_trim(line);
		char *eq;
		char *key;
		char *value;
		size_t value_len;

		if (*trimmed == '#' || *trimmed == '\0')
			continue;

		eq = strchr(trimmed, '=');
		if (!eq)
			continue;

		*eq   = '\0';
		key   = helper_trim(trimmed);
		value = helper_trim(eq + 1);
		if (strcmp(key, "crypto_pairs") != 0)
			continue;

		{
			bool in_quotes = false;
			for (char *p = value; *p; p++) {
				if (*p == '"') {
					in_quotes = !in_quotes;
				} else if (*p == '#'
				           && !in_quotes
				           && p > value
				           && isspace((unsigned char)*(p - 1))) {
					char *end = p - 1;
					while (end > value
					       && isspace((unsigned char)*end))
						end--;
					*(end + 1) = '\0';
					break;
				}
			}
		}

		value_len = strlen(value);
		if (value_len >= 2 && value[0] == '"'
		    && value[value_len - 1] == '"') {
			value[value_len - 1] = '\0';
			value++;
		}

		set_pairs_from_csv(value);
	}

	fclose(f);
}

static void
read_config(void)
{
	char        user_path[512];
	const char *home = getenv("HOME");

	set_default_pairs();
	load_config_file(CONFIG_PATH);

	if (home) {
		snprintf(user_path, sizeof(user_path),
		         "%s/.config/barny/barny.conf", home);
		load_config_file(user_path);
	}

	if (pair_count == 0)
		set_default_pairs();
}

static void
write_price(const char *name, double price)
{
	char path[256];
	char tmp_path[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", OUTPUT_DIR, name);
	snprintf(tmp_path, sizeof(tmp_path), "%s/%s.tmp", OUTPUT_DIR, name);

	f = fopen(tmp_path, "w");
	if (!f)
		return;

	fprintf(f, "%lg\n", price);
	fclose(f);
	rename(tmp_path, path);
}

static void
process_message(const char *data, size_t len)
{
	cJSON *json = cJSON_ParseWithLength(data, len);
	cJSON *data_arr;
	cJSON *first;
	cJSON *inst_id;
	cJSON *mark_px;

	if (!json)
		return;

	data_arr = cJSON_GetObjectItem(json, "data");
	if (!data_arr || !cJSON_IsArray(data_arr)
	    || cJSON_GetArraySize(data_arr) < 1) {
		cJSON_Delete(json);
		return;
	}

	first   = cJSON_GetArrayItem(data_arr, 0);
	inst_id = cJSON_GetObjectItem(first, "instId");
	mark_px = cJSON_GetObjectItem(first, "markPx");
	if (!inst_id || !inst_id->valuestring || !mark_px
	    || !mark_px->valuestring) {
		cJSON_Delete(json);
		return;
	}

	double price = strtod(mark_px->valuestring, NULL);
	for (int i = 0; i < pair_count; i++) {
		if (strcmp(inst_id->valuestring, pairs[i].inst_id) == 0) {
			write_price(pairs[i].file_name, price);
			break;
		}
	}

	cJSON_Delete(json);
}

static int
websocket_loop(CURL *curl)
{
	cJSON   *msg = cJSON_CreateObject();
	cJSON   *args;
	char    *sub_str;
	size_t   sent;
	CURLcode res;

	if (!msg)
		return -1;

	args = cJSON_CreateArray();
	if (!args) {
		cJSON_Delete(msg);
		return -1;
	}

	cJSON_AddStringToObject(msg, "op", "subscribe");
	for (int i = 0; i < pair_count; i++) {
		cJSON *arg = cJSON_CreateObject();
		if (!arg)
			continue;
		cJSON_AddStringToObject(arg, "channel", "mark-price");
		cJSON_AddStringToObject(arg, "instId", pairs[i].inst_id);
		cJSON_AddItemToArray(args, arg);
	}
	cJSON_AddItemToObject(msg, "args", args);

	sub_str = cJSON_PrintUnformatted(msg);
	cJSON_Delete(msg);
	if (!sub_str)
		return -1;

	res = curl_ws_send(curl, sub_str, strlen(sub_str), &sent, 0,
	                   CURLWS_TEXT);
	free(sub_str);
	if (res != CURLE_OK) {
		fprintf(stderr, "Failed to subscribe: %s\n",
		        curl_easy_strerror(res));
		return -1;
	}

	fprintf(stderr, "Subscribed to %d crypto pairs\n", pair_count);

	char buffer[4096];
	while (running) {
		size_t                      rlen;
		const struct curl_ws_frame *frame;

		res = curl_ws_recv(curl, buffer, sizeof(buffer) - 1, &rlen,
		                   &frame);
		if (res == CURLE_AGAIN) {
			usleep(10000);
			continue;
		}
		if (res != CURLE_OK) {
			fprintf(stderr, "WebSocket recv error: %s\n",
			        curl_easy_strerror(res));
			return -1;
		}
		if (frame->flags & CURLWS_TEXT) {
			buffer[rlen] = '\0';
			process_message(buffer, rlen);
		} else if (frame->flags & CURLWS_CLOSE) {
			fprintf(stderr, "Server closed connection\n");
			return -1;
		}
	}

	return 0;
}

static CURL *
create_websocket(void)
{
	CURL     *curl = curl_easy_init();
	CURLcode  res;

	if (!curl)
		return NULL;

	curl_easy_setopt(curl, CURLOPT_URL, WS_URL);
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "WebSocket connect failed: %s\n",
		        curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		return NULL;
	}

	fprintf(stderr, "Connected to OKX WebSocket\n");
	return curl;
}

int
main(void)
{
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	curl_global_init(CURL_GLOBAL_ALL);
	read_config();

	while (running) {
		CURL *curl = create_websocket();
		if (curl) {
			websocket_loop(curl);
			curl_easy_cleanup(curl);
		}

		if (running) {
			fprintf(stderr, "Reconnecting in %d seconds...\n",
			        RECONNECT_DELAY);
			sleep(RECONNECT_DELAY);
		}
	}

	clear_pairs();
	curl_global_cleanup();
	fprintf(stderr, "Shutdown complete\n");
	return 0;
}
