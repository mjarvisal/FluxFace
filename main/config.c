#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "main.h"

#include "json.h"
#include "config.h"
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//#define DEBUG_PRINT

#include "debug.h"
#include "countries.h"
#include "mutex.h"

#define CONFIGBUFLEN 2048

config_t gen_config;
esp_err_t config_updated = ESP_FAIL;

static char configbuf[CONFIGBUFLEN];

// Define types for the different kinds of data in your config
typedef enum {
    TYPE_BOOL,
    TYPE_INT,
    TYPE_STRING,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_COLOR, // For RGB/OverLimit keys
    TYPE_COUNTRY // For the special Country parsing logic
} json_data_type_t;

// Structure to map a JSON key to its target location and data type
typedef struct {
    const char *json_key;
    json_data_type_t type;
    void *target_ptr; // Pointer to the variable in the gen_config structure
    size_t max_len;   // For strings, the size of the target buffer
} config_item_t;

int hex_to_dec(char *hex, int len)
{
    int val = 0;
    for (int i = 0; i < len; i++) {
        char byte = hex[i];
        if (byte >= '0' && byte <= '9') {
            byte = byte - '0';
        } else if (byte >= 'A' && byte <= 'F') {
            byte = byte - 'A' + 10;
        } else if (byte >= 'a' && byte <= 'f') {
            byte = byte - 'a' + 10;
        }
        val = (val << 4) | (byte & 0xF);
    }
    return val;
}

const config_item_t main_config_map[] = {
// Nordpool
    // Special case for Country
    {"Country", TYPE_COUNTRY, &gen_config.timezones_info, 0},

// Limits
    // Number of limits active
    {"LimitsNumber", TYPE_INT, &gen_config.Limits_number, 0},
    // Limits
    {"Limit_1", TYPE_DOUBLE, &gen_config.Limits[0].LED_limit, 0},
    {"Limit_2", TYPE_DOUBLE, &gen_config.Limits[1].LED_limit, 0},
    {"Limit_3", TYPE_DOUBLE, &gen_config.Limits[2].LED_limit, 0},
    {"Limit_4", TYPE_DOUBLE, &gen_config.Limits[3].LED_limit, 0},
    {"Limit_5", TYPE_DOUBLE, &gen_config.Limits[4].LED_limit, 0},
    {"Limit_6", TYPE_DOUBLE, &gen_config.Limits[5].LED_limit, 0},
    {"Limit_7", TYPE_DOUBLE, &gen_config.Limits[6].LED_limit, 0},

    // Limit Colors
    {"Limit_1_color", TYPE_COLOR, &gen_config.Limits[0].LED_color, 0},
    {"Limit_2_color", TYPE_COLOR, &gen_config.Limits[1].LED_color, 0},
    {"Limit_3_color", TYPE_COLOR, &gen_config.Limits[2].LED_color, 0},
    {"Limit_4_color", TYPE_COLOR, &gen_config.Limits[3].LED_color, 0},
    {"Limit_5_color", TYPE_COLOR, &gen_config.Limits[4].LED_color, 0},
    {"Limit_6_color", TYPE_COLOR, &gen_config.Limits[5].LED_color, 0},
    {"Limit_7_color", TYPE_COLOR, &gen_config.Limits[6].LED_color, 0},
    // OverLimit color (Requires a color parsing helper)
    {"OverLimit", TYPE_COLOR, &gen_config.Limits[MAX_LIMITS].LED_color, 0},

// Display
    // String keys
    {"WIFISSID", TYPE_STRING, gen_config.WIFISSID, sizeof(gen_config.WIFISSID)},
    {"WIFIPassword", TYPE_STRING, gen_config.WIFIPassword, sizeof(gen_config.WIFIPassword)},

    // Integer key
    {"DayBrightness", TYPE_INT, &gen_config.DayBrightness, 0},

    {"CurrentPulsingEnabled", TYPE_BOOL, &gen_config.CurrentPulsingEnabled, 0},
    {"CurrentPulsingAmount", TYPE_INT, &gen_config.CurrentPulsingAmount, 0},
    {"CurrentPulsingSpeed", TYPE_DOUBLE, &gen_config.CurrentPulsingSpeed, 0},

    {"NightModeEnabled", TYPE_BOOL, &gen_config.NightModeEnabled, 0},
    {"NightBrightness", TYPE_INT, &gen_config.NightBrightness, 0},
    {"NightStart", TYPE_STRING, &gen_config.NightStart, sizeof(gen_config.NightStart)},
    {"NightEnd", TYPE_STRING, &gen_config.NightEnd, sizeof(gen_config.NightEnd)},
    {"PartyMode", TYPE_INT, &gen_config.PartyMode, 0},
};

void parse_hex_color(cJSON *key, LED_color_t *item)
{
    DEBUG_PRINTF("Parsing color key: %s\n", key->string);
    strcpy((char*)item->RGB, key->valuestring);
    item->red = hex_to_dec(&key->valuestring[1], 2);
    item->green = hex_to_dec(&key->valuestring[3], 2);
    item->blue = hex_to_dec(&key->valuestring[5], 2);
    DEBUG_PRINTF("Parsed color R:%d G:%d B:%d\n", item->red, item->green, item->blue);
}

void parse_country_logic(cJSON *key, timezones_t *item)
{
    DEBUG_PRINTF("Parsing country key: %s\n", key->string);
    enum countries country_enum = get_country_enum_from_string(key->valuestring);
    item->country_enum = country_enum;
    strcpy(item->country_string, get_country_string_from_enum(country_enum));
    strcpy(item->TZ, get_timezone_string_from_enum(country_enum));
    DEBUG_PRINTF("Parsed country: %s, TZstring: %s\n", item->country_string, item->TZ);
    setenv("TZ", item->TZ, 1);
    tzset();
}

esp_err_t parse_config_map(cJSON *root, const config_item_t *map, size_t map_size) {
    esp_err_t status = ESP_OK;

    for (size_t i = 0; i < map_size; i++) {
        const config_item_t *item = &map[i];
        cJSON *key = recursively_parse_JSON(root, item->json_key, NULL, 0, 0);

        if (key != NULL && (cJSON_IsString(key) || cJSON_IsBool(key))) {
            if (cJSON_IsString(key))
            {
                DEBUG_PRINTF("Parsing config key: %s\n", item->json_key);
            }
            else if (cJSON_IsBool(key))
            {
                DEBUG_PRINTF("Parsing config key: %s\n", item->json_key);
            }

            switch (item->type) {
                case TYPE_BOOL:
                    *(bool *)item->target_ptr = cJSON_IsBool(key) == true ? cJSON_IsTrue(key) : false;
                    break;
                case TYPE_INT:
                    *(int *)item->target_ptr = atoi(key->valuestring);
                    break;

                case TYPE_STRING:
                    // Use strncpy for safety instead of memcpy
                    strncpy((char *)item->target_ptr, key->valuestring, item->max_len - 1);
                    ((char *)item->target_ptr)[item->max_len - 1] = '\0'; // Ensure termination
                    break;

                case TYPE_FLOAT:
                     *(float *)item->target_ptr = atoff(key->valuestring);
                    break;

                case TYPE_DOUBLE:
                     *(double *)item->target_ptr = atof(key->valuestring);
                    break;

                case TYPE_COLOR:
                    // Requires a helper function to parse the hex string to RGB
                    parse_hex_color(key, (LED_color_t*) item->target_ptr);
                    break;

                case TYPE_COUNTRY:
                    // Requires the helper function for complex country logic
                    parse_country_logic(key, (timezones_t *)item->target_ptr);
                    break;

                default:
                    DEBUG_PRINTF("Unknown type for key: %s\n", item->json_key);
                    status = ESP_FAIL;
                    break;
            }
        } else {
            DEBUG_PRINTF("Failed to parse %s\n", item->json_key);
            status = ESP_FAIL; // Indicate a missing/badly formatted required key
        }
    }
    //vTaskDelay(pdMS_TO_TICKS(500));
    status = ESP_OK;
    return status;
}

config_t* CONFIG_config_file( void )
{
    config_t* gen_config_ptr = NULL;

    if (config_updated != ESP_OK) {
        config_updated = CONFIG_read_config_file();
    }
    if (config_updated == ESP_OK) {
        gen_config_ptr = &gen_config;
    }
    return gen_config_ptr;
}

esp_err_t CONFIG_read_config_file( void )
{
    esp_err_t err = ESP_FAIL;

    mutex_lock(MUTEX_TYPE_FILE);
    FILE *f = fopen("/www/config.json", "r");
    if (f == NULL)
    {
        mutex_unlock(MUTEX_TYPE_FILE);
        ESP_LOGE(TAG, "Failed to read config.json");
    }
    else
    {
        size_t read_bytes = 0, total_len = 0;
        char* buf_ptr = configbuf;
        do
        {
            read_bytes = fread(buf_ptr++, 1, 1, f);
            if (read_bytes > 0) total_len += read_bytes;
        } while ( read_bytes > 0  && total_len <= CONFIGBUFLEN);
        fclose(f);
        mutex_unlock(MUTEX_TYPE_FILE);
        err = ESP_OK;

        if (total_len > 0)
        {
        cJSON *root = cJSON_Parse(configbuf);
            if (root != NULL) {
                // Use the data-driven approach for all single-value keys
                err = parse_config_map(root, main_config_map, 
                                    sizeof(main_config_map) / sizeof(config_item_t));

                cJSON_Delete(root);
            } else {
                // Handle cJSON_Parse failure
                err = ESP_FAIL;
            }
        }
    }

    if( err == ESP_OK ) {
        config_updated = ESP_OK;
    } else {
        config_updated = ESP_FAIL;
    }

    return err;
}