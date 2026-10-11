#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "json.h"
#include "esp_err.h"

#include "debug.h"

static bool is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && is_leap_year(year));
}

static int64_t days_from_epoch(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    unsigned day_of_year = (153 * adjusted_month + 2) / 5 + day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
                          year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + day_of_era - 719468;
}

// Parse ISO 8601 UTC string into struct tm and time_t
int parse_iso8601_utc(const char *iso_str, struct tm *out_tm, time_t *out_time) {
    if (!iso_str || !out_tm || !out_time) {
        return -1; // Invalid arguments
    }

    int year, month, day, hour, minute, second, millisecond;
    char tz_char;

    if (sscanf(iso_str, "%4d-%2d-%2dT%2d:%2d:%2d.%3d%c",
               &year, &month, &day, &hour, &minute, &second, &millisecond, &tz_char) != 8 ||
        iso_str[strlen(iso_str) - 1] != 'Z') {
        return -2; // Parsing failed
    }

    if (tz_char != 'Z') {
        return -3; // Only UTC 'Z' supported in this example
    }
    if (month < 1 || month > 12 || day < 1 ||
        day > days_in_month(year, month) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59 ||
        millisecond < 0 || millisecond > 999) {
        return -2;
    }

    memset(out_tm, 0, sizeof(struct tm));
    out_tm->tm_year = year - 1900;
    out_tm->tm_mon  = month - 1;
    out_tm->tm_mday = day;
    out_tm->tm_hour = hour;
    out_tm->tm_min  = minute;
    out_tm->tm_sec  = second;
    out_tm->tm_isdst = 0;

    int day_of_year = day - 1;
    for (int current_month = 1; current_month < month; ++current_month) {
        day_of_year += days_in_month(year, current_month);
    }
    int64_t days = days_from_epoch(year, (unsigned)month, (unsigned)day);
    out_tm->tm_yday = day_of_year;
    out_tm->tm_wday = (int)((days + 4) % 7);
    if (out_tm->tm_wday < 0) {
        out_tm->tm_wday += 7;
    }
    int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    time_t converted = (time_t)seconds;
    if ((int64_t)converted != seconds || converted == (time_t)-1) {
        return -4; // Conversion failed
    }
    *out_time = converted;

    return 0; // Success
}

cJSON* recursively_parse_JSON(cJSON *json, const char* key, struct prices_t *prices, int size, int multi) {
    cJSON *component = NULL;
    cJSON *outcomponent = NULL;
    int index = 0;
    cJSON_ArrayForEach(component,json) {
        // If the component is an array or object, recurse into it.
        if(cJSON_IsArray(component) || cJSON_IsObject(component)) {
            outcomponent = recursively_parse_JSON(component, key, prices, size, multi);
            if (!multi)
            {
                if (outcomponent != NULL)
                {
                    return outcomponent; // Propagate the found key up the recursion chain
                }
            }
        }
        else if(cJSON_IsString(component) || cJSON_IsNumber(component) || cJSON_IsBool(component)) {
            if (component->string && strcmp(component->string, key) == 0)
            {
                switch(component->type)
                {
                    case cJSON_String:
                        //DEBUG_PRINTF("%s: %s\n",component->string,component->valuestring);
                        //if (prices) //!= NULL)
                        {
                            struct tm temp_tm;
                            (void) parse_iso8601_utc(component->valuestring, &temp_tm, &prices->timestamp);
                        }
                        DEBUG_PRINTF("Found string key %s -> depth: %d, index: %d\n", component->string, multi, index);
                        return component;
                    break;
                    case cJSON_Number:
                        if (prices != NULL)
                        {
                            prices->price_per_kwh = component->valuedouble;
                        }
                        DEBUG_PRINTF("%s: %.3f, depth: %d, index: %d\n",component->string,component->valuedouble, multi, index);
                        return component;
                    break;
                    case cJSON_False:
                    case cJSON_True:
                        return component;
                    break;
                    default:
                        DEBUG_PRINTF("Unknown value\n");
                        outcomponent = NULL;
                    break;
                }
            }
        }
        // Increment index and prices only when a valid entry is processed
        if (size > 0 && outcomponent != NULL)
        {
            index++;
            if (index >= size) break;
            prices++;
        }
    }
    return outcomponent;
}