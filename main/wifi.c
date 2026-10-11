#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "main.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mdns.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "json.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "system.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAXIMUM_RETRY 3U

#define ESP_WIFI_SSID "sahkokello"
#define ESP_WIFI_PASS "sahkokello"
#define ESP_WIFI_CHANNEL 1
#define MAX_STA_CONN 2

#define MDNS_INSTANCE "kello web server"
#define MDNS_HOST_NAME "kello"

static int s_retry_num = 0;
static bool s_netif_initialized = false;
static bool s_wifi_initialized = false;
static bool s_wifi_started = false;
static bool s_wifi_event_handler_registered = false;
static bool s_wifi_connect_handler_registered = false;
static bool s_ip_event_handler_registered = false;
static bool s_mdns_initialized = false;
static bool s_mdns_service_added = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static esp_event_handler_instance_t s_wifi_event_handler_instance;
static esp_event_handler_instance_t s_wifi_connect_handler_instance;
static esp_event_handler_instance_t s_ip_event_handler_instance;

config_t *wifi_gen_config = NULL;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static void wifi_connect_event_handler(void *arg, esp_event_base_t event_base,
                                       int32_t event_id, void *event_data);

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data){
    DEBUG_PRINTF("Event nr: %ld!\n", event_id);
}

static esp_err_t initialise_mdns(void)
{
    esp_err_t err;

    if (!s_mdns_initialized) {
        err = mdns_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize mDNS: %s", esp_err_to_name(err));
            return err;
        }
        s_mdns_initialized = true;
    }

    err = mdns_hostname_set(MDNS_HOST_NAME);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname: %s", esp_err_to_name(err));
        return err;
    }
    err = mdns_instance_name_set(MDNS_INSTANCE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS instance name: %s", esp_err_to_name(err));
        return err;
    }

    if (s_mdns_service_added) {
        return ESP_OK;
    }

    mdns_txt_item_t serviceTxtData[] = {
        {"chip", CONFIG_IDF_TARGET},
        {"path", "/"}
    };

    err = mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData,
                           sizeof(serviceTxtData) / sizeof(serviceTxtData[0]));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add mDNS service: %s", esp_err_to_name(err));
        return err;
    }
    s_mdns_service_added = true;
    return ESP_OK;
}

static esp_err_t wifi_stack_init(wifi_interface_t interface)
{
    esp_err_t err;

    if (!s_netif_initialized) {
        err = esp_netif_init();
        if (err != ESP_OK) {
            return err;
        }
        s_netif_initialized = true;
    }

    if (interface == WIFI_IF_STA && s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
        if (s_sta_netif == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (interface == WIFI_IF_AP && s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_wifi_initialized) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&cfg);
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_initialized = true;
    }

    if (!s_wifi_event_handler_registered) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  &wifi_event_handler, NULL,
                                                  &s_wifi_event_handler_instance);
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_event_handler_registered = true;
    }
    if (!s_wifi_connect_handler_registered) {
        err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                  &wifi_connect_event_handler, NULL,
                                                  &s_wifi_connect_handler_instance);
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_connect_handler_registered = true;
    }
    if (!s_ip_event_handler_registered) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                  &wifi_connect_event_handler, NULL,
                                                  &s_ip_event_handler_instance);
        if (err != ESP_OK) {
            return err;
        }
        s_ip_event_handler_registered = true;
    }

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

static esp_err_t wifi_start_with_config(wifi_mode_t mode, wifi_interface_t interface,
                                        wifi_config_t *config)
{
    esp_err_t err;

    if (s_wifi_started) {
        err = esp_wifi_stop();
        if (err != ESP_OK) {
            return err;
        }
        s_wifi_started = false;
    }

    err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(interface, config);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_start();
    if (err == ESP_OK) {
        s_wifi_started = true;
    }
    return err;
}

esp_err_t wifi_init_softap ( void )
{
    esp_err_t err = wifi_stack_init(WIFI_IF_AP);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),
            .channel = ESP_WIFI_CHANNEL,
            .password = ESP_WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };

    err = wifi_start_with_config(WIFI_MODE_AP, WIFI_IF_AP, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }
    set_wifi_mode(WIFI_MODE_AP);
    set_wifi_state(WIFI_STATE_CONNECTED);

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s channel:%d",
             ESP_WIFI_SSID, ESP_WIFI_CHANNEL);
    return ESP_OK;
}

static void wifi_connect_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        set_wifi_state(WIFI_STATE_CONNECTING);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        set_wifi_state(WIFI_STATE_DISCONNECTED);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_num < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        set_wifi_state(WIFI_STATE_CONNECTED);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_init_sta ( void )
{
    esp_err_t err = wifi_stack_init(WIFI_IF_STA);
    if (err != ESP_OK) {
        return err;
    }
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry_num = 0;
    set_wifi_state(WIFI_STATE_DISCONNECTED);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {'\0'},
            .password = {'\0'},
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED,
            .sae_h2e_identifier = {'\0'},
        },
    };
    memcpy(&wifi_config.sta.ssid, &wifi_gen_config->WIFISSID, sizeof(wifi_config.sta.ssid));
    memcpy(&wifi_config.sta.password, &wifi_gen_config->WIFIPassword, sizeof(wifi_config.sta.password));
    set_wifi_mode(WIFI_MODE_STA);
    set_wifi_state(WIFI_STATE_CONNECTING);
    err = wifi_start_with_config(WIFI_MODE_STA, WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to the configured access point");
        set_wifi_state(WIFI_STATE_CONNECTED);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to the configured access point");
        set_wifi_state(WIFI_STATE_DISCONNECTED);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return ESP_FAIL;
    }
}

bool wifi_is_connected ( void )
{
    return (get_wifi_mode() == WIFI_MODE_STA && get_wifi_state() == WIFI_STATE_CONNECTED);
}

esp_err_t init_wifi ( void )
{
    esp_err_t err = ESP_FAIL;
    wifi_gen_config = CONFIG_config_file();
    if (wifi_gen_config != NULL ){
        if (wifi_gen_config->WIFISSID[0] != '\0' && wifi_gen_config->WIFIPassword[0] != '\0')
        {
            err = wifi_init_sta();
        }
    }

    if (err != ESP_OK) //Wifi connection to external AP error
    {
        err = wifi_init_softap();
        if (err != ESP_OK) {
            return err;
        }
    }
    if (wifi_is_connected())
    {
        initialize_sntp();
    }
    err = initialise_mdns();
    return err;
}