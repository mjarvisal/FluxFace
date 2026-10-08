
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "json.h"
#include "esp_err.h"

#include "config.h"

#include "debug.h"

time_t my_timegm(struct tm *tm) {
    time_t ret;
    // 1. Save the current TZ environment variable
    config_t *config = CONFIG_config_file();
    
    // 2. Temporarily set the TZ environment variable to UTC/GMT
    // The empty string "" or "GMT0" works to set UTC/GMT
    setenv("TZ", "GMT0", 1); 
    
    // 3. Update the C library's time zone data
    tzset(); 

    // 4. Call mktime. Since TZ is set to GMT, mktime will treat 'tm' as UTC
    // and return a time_t that is correct for UTC.
    ret = mktime(tm); 

    // 5. Restore the original TZ setting
    setenv("TZ", config->timezones_info.TZ, 1);
    
    // 6. Restore the C library's original time zone data
    tzset(); 
    
    return ret;
}

// Parse ISO 8601 UTC string into struct tm and time_t
int parse_iso8601_utc(const char *iso_str, struct tm *out_tm, time_t *out_time) {
    if (!iso_str || !out_tm || !out_time) {
        return -1; // Invalid arguments
    }

    // Temporary variables
    int year, month, day, hour, minute, second, millisecond;
    char tz_char;

    // Parse the string (expecting format: YYYY-MM-DDTHH:MM:SS.sssZ)
    if (sscanf(iso_str, "%4d-%2d-%2dT%2d:%2d:%2d.%3d%c",
               &year, &month, &day, &hour, &minute, &second, &millisecond, &tz_char) < 7) {
        return -2; // Parsing failed
    }

    if (tz_char != 'Z') {
        return -3; // Only UTC 'Z' supported in this example
    }

    // Fill struct tm (UTC)
    memset(out_tm, 0, sizeof(struct tm));
    out_tm->tm_year = year - 1900; // tm_year is years since 1900
    out_tm->tm_mon  = month - 1;   // tm_mon is 0-based
    out_tm->tm_mday = day;
    out_tm->tm_hour = hour;
    out_tm->tm_min  = minute;
    out_tm->tm_sec  = second;
    out_tm->tm_isdst = 0; // UTC has no DST

    // Convert to time_t (UTC)
    *out_time = my_timegm(out_tm);

    if (*out_time == (time_t)-1) {
        return -4; // Conversion failed
    }

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