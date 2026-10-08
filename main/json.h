#ifndef MAIN_JSON_H_
#define MAIN_JSON_H_

#include "cJSON.h"
#include "prices.h"

cJSON* recursively_parse_JSON(cJSON *json, const char* key, struct prices_t *prices, int size, int multi);

#endif /* MAIN_JSON_H_ */