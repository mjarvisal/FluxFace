#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "system.h"
#include "main.h"
#include "wifi.h"
#include "config.h"
#include "led.h"
#include "ota.h"
#include "debug_log.h"

extern esp_err_t start_rest_server(const char *base_path);

#define WEB_PAGE_MOUNT_POINT_IN_FS "/www"

esp_err_t init_fs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = WEB_PAGE_MOUNT_POINT_IN_FS,
        .partition_label = "www",
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
        esp_littlefs_format(conf.partition_label);
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    return ESP_OK;
}

esp_err_t check_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", reason);
    if (reason == ESP_RST_BROWNOUT) {
        ESP_LOGI(TAG, "Resetting LED strip colors to defaults after brownout");
        set_system_state(SYSTEM_STATE_ERROR);
    }
    return ESP_OK;
}

void init ( void )
{
    ESP_ERROR_CHECK(debug_log_init());
    set_system_state(SYSTEM_STATE_INIT);
    ESP_ERROR_CHECK(check_reset_reason());
    ESP_LOGI(TAG, "System state: %s", get_system_state() == SYSTEM_STATE_ERROR ? "SYSTEM_STATE_ERROR" : "SYSTEM_STATE_INIT");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(init_fs());
    ESP_ERROR_CHECK(debug_log_start());
    ESP_ERROR_CHECK(CONFIG_read_config_file());
}

void initwifi ( void )
{
    ESP_ERROR_CHECK(init_wifi());
    ESP_ERROR_CHECK(start_rest_server(WEB_PAGE_MOUNT_POINT_IN_FS));
    if (get_system_state() != SYSTEM_STATE_ERROR)
    {
        set_system_state(SYSTEM_STATE_RUNNING);
        /* Reached a known-good state: confirm any freshly flashed OTA image so
         * the bootloader keeps it instead of rolling back on the next boot. */
        ota_mark_valid();
    }
}
