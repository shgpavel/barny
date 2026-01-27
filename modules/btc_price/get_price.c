#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "structs.h"
#include "write_callback.h"

price_t
get_price(char ticker[30])
{
	int     _exit = 0;

	price_t returned;
	memcpy(returned.ticker, ticker, strlen(ticker));

	char template_str[]
	        = "https://www.okx.com/api/v5/public/mark-price?instType=%s&instId=%s";
	char result_str[100];
	sprintf(result_str, template_str, "SWAP", ticker);

	CURL *curl = curl_easy_init();
	if (!curl) {
		fprintf(stderr, "Error: Failed to initialize curl\n");
		exit(1);
	}

	curl_easy_setopt(curl, CURLOPT_URL, result_str);
	cJSON *json = NULL;

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &json);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "Error: curl_easy_perform() failed: %s\n",
		        curl_easy_strerror(res));
		_exit = 1;
		goto clear;
	}

	if (!json) {
		fprintf(stderr, "Error: No JSON data received\n");
		_exit = 1;
		goto clear;
	}

	cJSON *data_array = cJSON_GetObjectItem(json, "data");
	if (!data_array) {
		fprintf(stderr, "Error: 'data' array not found\n");
		_exit = 1;
		goto clear;
	}

	if (cJSON_IsArray(data_array) && cJSON_GetArraySize(data_array) > 0) {
		cJSON *first_item = cJSON_GetArrayItem(data_array, 0);
		cJSON *markPx     = cJSON_GetObjectItem(first_item, "markPx");
		cJSON *ts         = cJSON_GetObjectItem(first_item, "ts");
		if (markPx) {
			returned.price = strtod(markPx->valuestring, NULL);
		} else {
			fprintf(stderr, "Error: 'markPx' field not found\n");
			_exit = 1;
			goto clear;
		}
		if (ts) {
			returned.time = atol(ts->valuestring);
		} else {
			fprintf(stderr, "Error: 'ts' field not found\n");
			_exit = 1;
			goto clear;
		}
	} else {
		fprintf(stderr, "Error: 'data' array is empty\n");
		_exit = 1;
	}

clear:
	curl_easy_cleanup(curl);
	cJSON_Delete(json);

	if (_exit == 1) {
		exit(1);
	}

	return returned;
}
