#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "helper_util.h"

#define UPDATE_INTERVAL 600 /* 10 minutes */
#define OUTPUT_PATH     "/opt/barny/modules/weather"
#define OUTPUT_TMP_PATH "/opt/barny/modules/weather.tmp"
#define API_KEY_PATH    "/opt/barny/modules/weather_api_key"
#define HTTP_TIMEOUT    30L

static volatile int running = 1;

static void
signal_handler(int sig)
{
	(void)sig;
	running = 0;
}

static int
get_location(double *lat, double *lon)
{
	cJSON *json = helper_fetch_json("https://ipinfo.io/json", HTTP_TIMEOUT);
	if (!json)
		return -1;

	cJSON *loc = cJSON_GetObjectItemCaseSensitive(json, "loc");
	if (!cJSON_IsString(loc) || !loc->valuestring) {
		cJSON_Delete(json);
		return -1;
	}

	/* Parse "lat,lon" format */
	char locbuf[64];
	snprintf(locbuf, sizeof(locbuf), "%s", loc->valuestring);

	char *comma = strchr(locbuf, ',');
	if (!comma) {
		cJSON_Delete(json);
		return -1;
	}

	*comma = '\0';
	*lat   = strtod(locbuf, NULL);
	*lon   = strtod(comma + 1, NULL);

	cJSON_Delete(json);
	return 0;
}

static int
get_weather(double lat, double lon, const char *api_key, double *temp,
            char *weather, size_t weather_size)
{
	char url[512];
	snprintf(
	        url, sizeof(url),
	        "https://api.openweathermap.org/data/2.5/weather?lat=%lg&lon=%lg&appid=%s&units=metric",
	        lat, lon, api_key);

	cJSON *json = helper_fetch_json(url, HTTP_TIMEOUT);
	if (!json)
		return -1;

	/* Get temperature */
	cJSON *main_obj = cJSON_GetObjectItem(json, "main");
	cJSON *temp_obj = main_obj ? cJSON_GetObjectItem(main_obj, "temp") : NULL;
	if (!temp_obj || !cJSON_IsNumber(temp_obj)) {
		fprintf(stderr, "Failed to get temperature\n");
		cJSON_Delete(json);
		return -1;
	}
	*temp              = temp_obj->valuedouble;

	/* Get weather description */
	cJSON *weather_arr = cJSON_GetObjectItem(json, "weather");
	if (cJSON_IsArray(weather_arr) && cJSON_GetArraySize(weather_arr) > 0) {
		cJSON *first    = cJSON_GetArrayItem(weather_arr, 0);
		cJSON *main_str = cJSON_GetObjectItem(first, "main");
		if (main_str && main_str->valuestring) {
			snprintf(weather, weather_size, "%s",
			         main_str->valuestring);
		} else {
			snprintf(weather, weather_size, "Unknown");
		}
	} else {
		snprintf(weather, weather_size, "Unknown");
	}

	cJSON_Delete(json);
	return 0;
}

static void
write_output(double temp, const char *weather)
{
	FILE *f = fopen(OUTPUT_TMP_PATH, "w");
	if (!f) {
		fprintf(stderr, "Failed to open output file\n");
		return;
	}

	fprintf(f, "%lg %s\n", temp, weather);
	fclose(f);

	if (rename(OUTPUT_TMP_PATH, OUTPUT_PATH) != 0) {
		fprintf(stderr, "Failed to rename output file\n");
	}
}

static int
read_api_key(char *key)
{
	FILE *f = fopen(API_KEY_PATH, "r");
	if (!f) {
		fprintf(stderr, "Failed to open API key file: %s\n", API_KEY_PATH);
		return -1;
	}

	if (fscanf(f, "%99s", key) != 1) {
		fprintf(stderr, "Failed to read API key\n");
		fclose(f);
		return -1;
	}

	fclose(f);
	return 0;
}

int
main(void)
{
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	curl_global_init(CURL_GLOBAL_ALL);

	/* Read API key */
	char api_key[100];
	if (read_api_key(api_key) != 0)
		return 1;

	/* Get location once at startup */
	double lat, lon;
	if (get_location(&lat, &lon) != 0) {
		fprintf(stderr, "Failed to get location\n");
		return 1;
	}
	fprintf(stderr, "Location: %.4f, %.4f\n", lat, lon);

	while (running) {
		double temp;
		char   weather[64];

		if (get_weather(lat, lon, api_key, &temp, weather, sizeof(weather))
		    == 0) {
			write_output(temp, weather);
			fprintf(stderr, "Updated: %.1f°C %s\n", temp, weather);
		}

		/* Sleep in small increments to allow signal handling */
		for (int i = 0; i < UPDATE_INTERVAL && running; i++) {
			sleep(1);
		}
	}

	curl_global_cleanup();
	fprintf(stderr, "Shutdown complete\n");

	return 0;
}
