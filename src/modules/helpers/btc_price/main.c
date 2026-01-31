#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define TICKER "BTC-USDT-SWAP"
#define WS_URL "wss://ws.okx.com:8443/ws/v5/public"
#define OUTPUT_PATH "/opt/barny/modules/btc_price"
#define OUTPUT_TMP_PATH "/opt/barny/modules/btc_price.tmp"
#define RECONNECT_DELAY 5

static volatile int running = 1;

static void
signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static void
write_price(double price)
{
	FILE *f = fopen(OUTPUT_TMP_PATH, "w");
	if (!f) {
		fprintf(stderr, "Error: Failed to open output file\n");
		return;
	}
	fprintf(f, "%lg\n", price);
	fclose(f);
	if (rename(OUTPUT_TMP_PATH, OUTPUT_PATH) != 0) {
		fprintf(stderr, "Error: Failed to rename output file\n");
	}
}

static void
process_message(const char *data, size_t len)
{
	cJSON *json = cJSON_ParseWithLength(data, len);
	if (!json)
		return;

	/* Check if this is a data message (not subscribe confirmation) */
	cJSON *data_arr = cJSON_GetObjectItem(json, "data");
	if (data_arr && cJSON_IsArray(data_arr) && cJSON_GetArraySize(data_arr) > 0) {
		cJSON *first = cJSON_GetArrayItem(data_arr, 0);
		cJSON *markPx = cJSON_GetObjectItem(first, "markPx");
		if (markPx && markPx->valuestring) {
			double price = strtod(markPx->valuestring, NULL);
			write_price(price);
		}
	}

	cJSON_Delete(json);
}

static int
websocket_loop(CURL *curl)
{
	const char *subscribe_msg =
	        "{\"op\":\"subscribe\",\"args\":[{\"channel\":\"mark-price\",\"instId\":\"" TICKER "\"}]}";

	/* Send subscription message */
	size_t sent;
	CURLcode res = curl_ws_send(curl, subscribe_msg, strlen(subscribe_msg),
	                            &sent, 0, CURLWS_TEXT);
	if (res != CURLE_OK) {
		fprintf(stderr, "Failed to send subscribe: %s\n", curl_easy_strerror(res));
		return -1;
	}
	fprintf(stderr, "Subscribed to %s\n", TICKER);

	/* Receive loop */
	char buffer[4096];
	while (running) {
		size_t rlen;
		const struct curl_ws_frame *frame;

		res = curl_ws_recv(curl, buffer, sizeof(buffer) - 1, &rlen, &frame);

		if (res == CURLE_AGAIN) {
			/* No data available, wait a bit */
			usleep(10000); /* 10ms */
			continue;
		}

		if (res != CURLE_OK) {
			fprintf(stderr, "WebSocket recv error: %s\n", curl_easy_strerror(res));
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
	CURL *curl = curl_easy_init();
	if (!curl)
		return NULL;

	curl_easy_setopt(curl, CURLOPT_URL, WS_URL);
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L); /* WebSocket mode */

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "WebSocket connect failed: %s\n", curl_easy_strerror(res));
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

	while (running) {
		CURL *curl = create_websocket();
		if (curl) {
			websocket_loop(curl);
			curl_easy_cleanup(curl);
		}

		if (running) {
			fprintf(stderr, "Reconnecting in %d seconds...\n", RECONNECT_DELAY);
			sleep(RECONNECT_DELAY);
		}
	}

	curl_global_cleanup();
	fprintf(stderr, "Shutdown complete\n");

	return 0;
}
