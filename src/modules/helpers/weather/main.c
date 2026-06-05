#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#include "helper_util.h"

#define UPDATE_INTERVAL 600
#define OUTPUT_PATH     "/opt/barny/modules/weather"
#define OUTPUT_TMP_PATH "/opt/barny/modules/weather.tmp"
#define API_KEY_PATH    "/opt/barny/modules/weather_api_key"
#define HTTP_TIMEOUT    30L

typedef struct {
	double temp;
	double feels_like;
	int    humidity;
	int    pressure;
	double wind_speed;
	int    wind_deg;
	char   condition[64];
	char   description[128];
	char   location[64];
	bool   have_feels_like;
	bool   have_humidity;
	bool   have_pressure;
	bool   have_wind;
	bool   have_location;
	bool   have_description;
} weather_t;

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
	cJSON *json;
	cJSON *loc;
	char   locbuf[64];
	char  *comma;

	json = helper_fetch_json("https://ipinfo.io/json", HTTP_TIMEOUT);
	if (!json)
		return -1;

	loc = cJSON_GetObjectItemCaseSensitive(json, "loc");
	if (!cJSON_IsString(loc) || !loc->valuestring) {
		cJSON_Delete(json);
		return -1;
	}

	snprintf(locbuf, sizeof(locbuf), "%s", loc->valuestring);

	comma = strchr(locbuf, ',');
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

static const char *
deg_to_compass(int deg)
{
	static const char *dirs[] = { "N", "NE", "E", "SE",
		                      "S", "SW", "W", "NW" };
	int                idx    = (int)((deg + 22) / 45) & 7;
	return dirs[idx];
}

static int
get_weather(double lat, double lon, const char *api_key, weather_t *out)
{
	char   url[512];
	cJSON *json;
	cJSON *main_obj;
	cJSON *temp_obj;
	cJSON *fl;
	cJSON *h;
	cJSON *p;
	cJSON *wind;
	cJSON *ws;
	cJSON *wd;
	cJSON *name;
	cJSON *weather_arr;
	cJSON *first;
	cJSON *main_str;
	cJSON *desc;

	snprintf(
	        url, sizeof(url),
	        "https://api.openweathermap.org/data/2.5/weather?lat=%lg&lon=%lg&appid=%s&units=metric",
	        lat, lon, api_key);

	json = helper_fetch_json(url, HTTP_TIMEOUT);
	if (!json)
		return -1;

	memset(out, 0, sizeof(*out));

	main_obj = cJSON_GetObjectItem(json, "main");
	temp_obj = main_obj ? cJSON_GetObjectItem(main_obj, "temp") : NULL;
	if (!temp_obj || !cJSON_IsNumber(temp_obj)) {
		fprintf(stderr, "Failed to get temperature\n");
		cJSON_Delete(json);
		return -1;
	}
	out->temp = temp_obj->valuedouble;

	if (main_obj) {
		fl = cJSON_GetObjectItem(main_obj, "feels_like");
		if (cJSON_IsNumber(fl)) {
			out->feels_like      = fl->valuedouble;
			out->have_feels_like = true;
		}

		h = cJSON_GetObjectItem(main_obj, "humidity");
		if (cJSON_IsNumber(h)) {
			out->humidity      = (int)h->valuedouble;
			out->have_humidity = true;
		}

		p = cJSON_GetObjectItem(main_obj, "pressure");
		if (cJSON_IsNumber(p)) {
			out->pressure      = (int)p->valuedouble;
			out->have_pressure = true;
		}
	}

	wind = cJSON_GetObjectItem(json, "wind");
	if (wind) {
		ws = cJSON_GetObjectItem(wind, "speed");
		wd = cJSON_GetObjectItem(wind, "deg");
		if (cJSON_IsNumber(ws)) {
			out->wind_speed = ws->valuedouble;
			out->wind_deg   = cJSON_IsNumber(wd) ? (int)wd->valuedouble : 0;
			out->have_wind  = true;
		}
	}

	name = cJSON_GetObjectItem(json, "name");
	if (cJSON_IsString(name) && name->valuestring && *name->valuestring) {
		snprintf(out->location, sizeof(out->location), "%s",
		         name->valuestring);
		out->have_location = true;
	}

	weather_arr = cJSON_GetObjectItem(json, "weather");
	if (cJSON_IsArray(weather_arr) && cJSON_GetArraySize(weather_arr) > 0) {
		first    = cJSON_GetArrayItem(weather_arr, 0);
		main_str = cJSON_GetObjectItem(first, "main");
		desc     = cJSON_GetObjectItem(first, "description");
		if (main_str && cJSON_IsString(main_str) && main_str->valuestring) {
			snprintf(out->condition, sizeof(out->condition), "%s",
			         main_str->valuestring);
		} else {
			snprintf(out->condition, sizeof(out->condition),
			         "Unknown");
		}
		if (desc && cJSON_IsString(desc) && desc->valuestring && *desc->valuestring) {
			snprintf(out->description, sizeof(out->description),
			         "%s", desc->valuestring);
			out->have_description = true;
		}
	} else {
		snprintf(out->condition, sizeof(out->condition), "Unknown");
	}

	cJSON_Delete(json);
	return 0;
}

static void
write_output(const weather_t *w)
{
	FILE *f;

	f = fopen(OUTPUT_TMP_PATH, "w");
	if (!f) {
		fprintf(stderr, "Failed to open output file\n");
		return;
	}

	fprintf(f, "temp=%lg\n", w->temp);
	fprintf(f, "condition=%s\n", w->condition);
	if (w->have_description)
		fprintf(f, "description=%s\n", w->description);
	if (w->have_location)
		fprintf(f, "location=%s\n", w->location);
	if (w->have_feels_like)
		fprintf(f, "feels_like=%lg\n", w->feels_like);
	if (w->have_humidity)
		fprintf(f, "humidity=%d\n", w->humidity);
	if (w->have_wind) {
		fprintf(f, "wind_speed=%lg\n", w->wind_speed);
		fprintf(f, "wind_deg=%d\n", w->wind_deg);
		fprintf(f, "wind_dir=%s\n", deg_to_compass(w->wind_deg));
	}
	if (w->have_pressure)
		fprintf(f, "pressure=%d\n", w->pressure);

	fclose(f);

	if (rename(OUTPUT_TMP_PATH, OUTPUT_PATH) != 0) {
		fprintf(stderr, "Failed to rename output file\n");
	}
}

static int
read_api_key(char *key)
{
	FILE *f;

	f = fopen(API_KEY_PATH, "r");
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
	char      api_key[100];
	double    lat;
	double    lon;
	weather_t w;
	int       i;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	curl_global_init(CURL_GLOBAL_ALL);

	if (read_api_key(api_key) != 0)
		return 1;

	if (get_location(&lat, &lon) != 0) {
		fprintf(stderr, "Failed to get location\n");
		return 1;
	}
	fprintf(stderr, "Location: %.4f, %.4f\n", lat, lon);

	while (running) {
		if (get_weather(lat, lon, api_key, &w) == 0) {
			write_output(&w);
			fprintf(stderr, "Updated: %.1f C %s\n", w.temp,
			        w.condition);
		}

		for (i = 0; i < UPDATE_INTERVAL && running; i++) {
			sleep(1);
		}
	}

	curl_global_cleanup();
	fprintf(stderr, "Shutdown complete\n");

	return 0;
}
