#ifndef WCB_H
#define WCB_H

#include <stdio.h>
#include <cjson/cJSON.h>

size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
  cJSON **json = (cJSON **)userdata;
  *json = cJSON_Parse(ptr);
  return size * nmemb;
}

#endif
