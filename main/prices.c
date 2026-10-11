#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "main.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "system.h"

#include "json.h"
#include "mutex.h"
#include "network_diag.h"
#include "wifi.h"

//#define DEBUG_ENABLE

#define MAX_HTTP_OUTPUT_BUFFER 10240
static char json_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};   // Buffer to store response of http request
// In-memory copy of the last successfully parsed prices.json content, used to
// detect whether the file actually changed before re-parsing and updating the
// price data the LEDs are driven from.
static char cached_prices_json[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
static size_t cached_prices_len = 0;
static time_t now = 0;

static prices_t prices[192]; // 2 days of 15min prices
static time_t last_price_update = 0;
static portMUX_TYPE s_price_update_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_price_update_in_progress = false;

const price_update_info_t price_update_info = {
    .prices = &prices[0],
    .last_update = &last_price_update
};

void parse_dates ( char* dst );

static void set_price_update_in_progress(bool in_progress)
{
    taskENTER_CRITICAL(&s_price_update_mux);
    s_price_update_in_progress = in_progress;
    taskEXIT_CRITICAL(&s_price_update_mux);
}

bool prices_update_is_in_progress(void)
{
    taskENTER_CRITICAL(&s_price_update_mux);
    bool in_progress = s_price_update_in_progress;
    taskEXIT_CRITICAL(&s_price_update_mux);
    return in_progress;
}

/*
 *  http_native_request() demonstrates use of low level APIs to connect to a server,
 *  make a http request and read response. Event handler is not used in this case.
 *  Note: This approach should only be used in case use of low level APIs is required.
 *  The easiest way is to use esp_http_perform()
 */
static esp_err_t http_native_request(void)
{
    // Declare local_response_buffer with size (MAX_HTTP_OUTPUT_BUFFER + 1) to prevent out of bound access when
    // it is used by functions like strlen(). The buffer should only be used upto size MAX_HTTP_OUTPUT_BUFFER
    char uri[200] = {0};
    parse_dates(uri);

    ESP_LOGI(TAG, "%s", uri);
    esp_http_client_config_t config = {
        .url = uri,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    ESP_LOGI(TAG, "HTTP native request =>");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = esp_http_client_set_method(client, HTTP_METHOD_GET);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set HTTP method: %s", esp_err_to_name(result));
        esp_http_client_cleanup(client);
        return result;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        result = err;
    } else {
        int content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) {
            ESP_LOGE(TAG, "HTTP client fetch headers failed: %d", content_length);
            result = ESP_FAIL;
        } else {
            int status = esp_http_client_get_status_code(client);
            if (status < 200 || status >= 300) {
                ESP_LOGE(TAG, "Price server returned HTTP status %d", status);
                result = ESP_FAIL;
            }
            int data_read = result == ESP_OK
                                ? esp_http_client_read_response(client, json_buffer,
                                                                MAX_HTTP_OUTPUT_BUFFER)
                                : -1;
            if (data_read >= 0) {
                if (data_read == 0) {
                    ESP_LOGE(TAG, "Price server returned an empty response");
                    result = ESP_FAIL;
                }
            }
            if (data_read > 0 && result == ESP_OK) {
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                         status, esp_http_client_get_content_length(client));
                ESP_LOGI(TAG, "Opening file");
                mutex_lock(MUTEX_TYPE_FILE);
                FILE *f = fopen("/www/prices.json", "w");
                if (f == NULL)
                {
                        ESP_LOGE(TAG, "Failed to open file for writing");
                        mutex_unlock(MUTEX_TYPE_FILE);
                        result = ESP_FAIL;
                } else {
                    if (fwrite(json_buffer, 1, data_read, f) != (size_t)data_read) {
                        ESP_LOGE(TAG, "Failed to write complete price response");
                        result = ESP_FAIL;
                    }
                    if (fclose(f) != 0) {
                        ESP_LOGE(TAG, "Failed to close prices.json after writing");
                        result = ESP_FAIL;
                    }
                    mutex_unlock(MUTEX_TYPE_FILE);
                    if (result == ESP_OK) {
                        ESP_LOGI(TAG, "File written");
                    }
                }
            } else if (data_read < 0 && result == ESP_OK) {
                ESP_LOGE(TAG, "Failed to read price response");
                result = ESP_FAIL;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result;
}

const price_update_info_t* get_prices( void )
{
    return &price_update_info;
}

static esp_err_t parse_price_data (void) {
    esp_err_t err = ESP_FAIL;
    size_t total_len = 0;

    mutex_lock(MUTEX_TYPE_FILE);
    FILE *f = fopen("/www/prices.json", "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to read prices.json");
        mutex_unlock(MUTEX_TYPE_FILE);
        return ESP_FAIL;
    }
    total_len = fread(json_buffer, 1, MAX_HTTP_OUTPUT_BUFFER, f);
    fclose(f);
    mutex_unlock(MUTEX_TYPE_FILE);
    json_buffer[total_len] = '\0';

    if (total_len == 0)
    {
        return ESP_FAIL;
    }

    // Only re-parse and update the in-memory price structure (and therefore the
    // LEDs) when the file content has actually changed since the last read.

    if (total_len == cached_prices_len &&
        memcmp(json_buffer, cached_prices_json, total_len) == 0)
    {
        ESP_LOGI(TAG, "prices.json unchanged since last read, skipping update");
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(json_buffer);
    if (root != NULL)
    {
        (void) recursively_parse_JSON(root, "date", &prices[0], 192, 1);
        (void) recursively_parse_JSON(root, "value", &prices[0], 192, 1);
        last_price_update = time(NULL);
        cJSON_Delete(root);

        // Cache the parsed content so subsequent reads can be diffed against it.
        memcpy(cached_prices_json, json_buffer, total_len);
        cached_prices_len = total_len;
        err = ESP_OK;
        ESP_LOGI(TAG, "prices.json changed, price data updated (%u bytes)", (unsigned)total_len);
    }
#ifdef DEBUG_ENABLE
    mutex_lock(MUTEX_TYPE_TIMEZONE);
    for (int i=0; i<192; i++) {
        char strftime_buf[64];
        struct tm timeinfo;
        localtime_r(&prices[i].timestamp, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Price %d: time: %s, price: %.3f", i, strftime_buf, prices[i].price_per_kwh);
    }
    mutex_unlock(MUTEX_TYPE_TIMEZONE);
#endif
    return err;
}

void parse_dates ( char* dst )
{
    char uri[200];
    char strftime_buf[64];
    struct tm timeinfo,timeinfo_future;
    char datenow[36];
    char datefuture[36];

    time_t future = now + (48 * 60 * 60);

    gmtime_r(&now, &timeinfo);
    gmtime_r(&future, &timeinfo_future);
    sprintf(datenow, "%04d-%02d-%02d", timeinfo.tm_year+1900,timeinfo.tm_mon+1, timeinfo.tm_mday);
    sprintf(datefuture, "%04d-%02d-%02d", timeinfo_future.tm_year+1900,timeinfo_future.tm_mon+1, timeinfo_future.tm_mday);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    sprintf (uri, "https://sahkotin.fi/prices?quarter&fix&start=%sT%02d:%02d:00.000Z&end=%sT00:00:00.000Z", datenow, timeinfo.tm_hour, (timeinfo.tm_min/15)*15, datefuture);
    strcpy(dst, uri);
}

void Prices_Task ( void * pvParameters )
{
    int delay = 1000;

    while (1)
    {
        if (wifi_is_connected() &&
            get_system_state() == SYSTEM_STATE_RUNNING &&
            sntp_time_is_set())
        {
            if (!mutex_try_lock(MUTEX_TYPE_OUTBOUND_TLS)) {
                ESP_LOGI(TAG, "Skipping price fetch while another TLS request is active");
                delay = 60000;
                vTaskDelay(pdMS_TO_TICKS(delay));
                continue;
            }

            set_price_update_in_progress(true);
            time(&now);
            network_diag_sample_t diag_before = network_diag_snapshot("prices before");
            network_diag_fetch_begin();
            esp_err_t fetch_err = http_native_request();
            mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
            if (fetch_err == ESP_OK)
            {
                esp_err_t parse_err = parse_price_data();
                if (parse_err == ESP_OK) {
                    delay = 60 * 60000;
                } else {
                    ESP_LOGW(TAG, "Price data could not be parsed; retrying in one minute");
                    delay = 60000;
                }
            } else {
                ESP_LOGW(TAG, "Price fetch failed; retrying in one minute");
                delay = 60000;
            }
            network_diag_fetch_end(&diag_before);
            set_price_update_in_progress(false);
        } else {
            delay = 1000;
        }
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}