#ifndef MAIN_COUNTRIES_H_
#define MAIN_COUNTRIES_H_
#include "config.h"

extern struct timezones_t timezones[];
enum countries get_country_enum_from_string ( const char* country_string );
const char* get_country_string_from_enum ( enum countries country_enum );
const char* get_timezone_string_from_enum ( enum countries country_enum );
#endif /* MAIN_COUNTRIES_H_ */
