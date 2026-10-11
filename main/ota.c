#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_wifi.h"

#include "ota.h"
#include "mutex.h"
#include "network_diag.h"
#include "wifi.h"

static const char *TAG = "ota";

/* URL of the latest firmware image. The host serves an ESP-IDF app binary
 * (the same .bin produced by the build) over HTTPS with a Let's Encrypt cert,
 * which chains to a root already present in the ESP-IDF certificate bundle. */
#define OTA_FIRMWARE_URL "https://kello.xn--jrvisalo-0za.fi/kello.update"
#define OTA_CHECK_CACHE_MS (10 * 1000)

/* Shared OTA status, guarded by MUTEX_TYPE_OTA. */
static ota_state_t s_state = OTA_STATE_IDLE;
static int s_progress = -1;
static char s_message[96] = {0};
static ota_check_result_t s_cached_check_result;
static TickType_t s_cached_check_tick;
static bool s_cached_check_valid = false;

static bool ota_get_cached_check(ota_check_result_t *result, TickType_t now)
{
    mutex_lock(MUTEX_TYPE_OTA);
    bool valid = s_cached_check_valid &&
                 (TickType_t)(now - s_cached_check_tick) <
                     pdMS_TO_TICKS(OTA_CHECK_CACHE_MS);
    if (valid) {
        *result = s_cached_check_result;
    }
    mutex_unlock(MUTEX_TYPE_OTA);
    return valid;
}

static void ota_store_cached_check(const ota_check_result_t *result)
{
    if (result->error[0] != '\0') {
        return;
    }
    mutex_lock(MUTEX_TYPE_OTA);
    s_cached_check_result = *result;
    s_cached_check_tick = xTaskGetTickCount();
    s_cached_check_valid = true;
    mutex_unlock(MUTEX_TYPE_OTA);
}

static void ota_set_status(ota_state_t state, int progress, const char *message)
{
    mutex_lock(MUTEX_TYPE_OTA);
    s_state = state;
    s_progress = progress;
    if (message != NULL) {
        strlcpy(s_message, message, sizeof(s_message));
    }
    mutex_unlock(MUTEX_TYPE_OTA);
}

void ota_get_current_version(char *dst, size_t dst_len)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }
    const esp_app_desc_t *desc = esp_app_get_description();
    strlcpy(dst, desc->version, dst_len);
}

void ota_get_status(ota_state_t *state, int *progress, char *message, size_t message_len)
{
    mutex_lock(MUTEX_TYPE_OTA);
    if (state != NULL) {
        *state = s_state;
    }
    if (progress != NULL) {
        *progress = s_progress;
    }
    if (message != NULL && message_len > 0) {
        strlcpy(message, s_message, message_len);
    }
    mutex_unlock(MUTEX_TYPE_OTA);
}

static esp_err_t ota_check_update_impl(ota_check_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(*result));
    ota_get_current_version(result->current, sizeof(result->current));
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Skipping OTA check while Wi-Fi is disconnected");
        strlcpy(result->error, "Wi-Fi is not connected", sizeof(result->error));
        return ESP_OK;
    }

    esp_http_client_config_t http_config = {
        .url = OTA_FIRMWARE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        strlcpy(result->error, "Could not reach the update server", sizeof(result->error));
        return ESP_OK;
    }

    esp_app_desc_t remote_desc = {0};
    err = esp_https_ota_get_img_desc(handle, &remote_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(err));
        strlcpy(result->error, "Downloaded file is not a valid firmware image", sizeof(result->error));
        esp_https_ota_abort(handle);
        return ESP_OK;
    }

    /* Only the header was read; discard the connection without writing flash. */
    esp_https_ota_abort(handle);

    strlcpy(result->remote, remote_desc.version, sizeof(result->remote));
    result->available = (strncmp(result->remote, result->current, OTA_VERSION_MAX_LEN) != 0);

    ESP_LOGI(TAG, "OTA check: current=%s remote=%s available=%d",
             result->current, result->remote, (int)result->available);
    return ESP_OK;
}

esp_err_t ota_check_update(ota_check_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    network_diag_sample_t before = network_diag_snapshot("OTA check before");
    TickType_t now = xTaskGetTickCount();
    if (ota_get_cached_check(result, now)) {
        ESP_LOGI(TAG, "Using cached OTA check result");
        network_diag_sample_t after = network_diag_snapshot("OTA check after");
        network_diag_log_delta("OTA check", &before, &after);
        return ESP_OK;
    }

    memset(result, 0, sizeof(*result));
    ota_get_current_version(result->current, sizeof(result->current));
    if (!mutex_try_lock(MUTEX_TYPE_OUTBOUND_TLS)) {
        strlcpy(result->error, "Another network request is in progress; try again shortly",
                sizeof(result->error));
        ESP_LOGW(TAG, "Skipping OTA check while another TLS request is active");
        network_diag_sample_t after = network_diag_snapshot("OTA check after");
        network_diag_log_delta("OTA check", &before, &after);
        return ESP_OK;
    }

    if (ota_get_cached_check(result, xTaskGetTickCount())) {
        mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
        ESP_LOGI(TAG, "Using cached OTA check result");
        network_diag_sample_t after = network_diag_snapshot("OTA check after");
        network_diag_log_delta("OTA check", &before, &after);
        return ESP_OK;
    }

    esp_err_t err = ota_check_update_impl(result);
    if (err == ESP_OK) {
        ota_store_cached_check(result);
    }
    mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
    network_diag_sample_t after = network_diag_snapshot("OTA check after");
    network_diag_log_delta("OTA check", &before, &after);
    return err;
}

static void ota_update_task(void *arg)
{
    (void)arg;
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Skipping OTA download while Wi-Fi is disconnected");
        ota_set_status(OTA_STATE_ERROR, -1, "Wi-Fi is not connected");
        vTaskDelete(NULL);
        return;
    }
    if (!mutex_try_lock(MUTEX_TYPE_OUTBOUND_TLS)) {
        ESP_LOGW(TAG, "Skipping OTA download while another TLS request is active");
        ota_set_status(OTA_STATE_ERROR, -1, "Another network request is in progress");
        vTaskDelete(NULL);
        return;
    }
    ota_set_status(OTA_STATE_DOWNLOADING, 0, "Starting download");

    esp_http_client_config_t http_config = {
        .url = OTA_FIRMWARE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        ota_set_status(OTA_STATE_ERROR, -1, "Could not reach the update server");
        mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
        vTaskDelete(NULL);
        return;
    }

    esp_app_desc_t remote_desc = {0};
    if (esp_https_ota_get_img_desc(handle, &remote_desc) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read image header");
        ota_set_status(OTA_STATE_ERROR, -1, "Downloaded file is not a valid firmware image");
        esp_https_ota_abort(handle);
        mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
        vTaskDelete(NULL);
        return;
    }

    int image_size = esp_https_ota_get_image_size(handle);
    ESP_LOGI(TAG, "Downloading firmware %s (%d bytes)", remote_desc.version, image_size);

    while (1) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int read = esp_https_ota_get_image_len_read(handle);
        int progress = (image_size > 0) ? (int)((int64_t)read * 100 / image_size) : -1;
        ota_set_status(OTA_STATE_DOWNLOADING, progress, "Downloading firmware");
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(err));
        ota_set_status(OTA_STATE_ERROR, -1, "Download failed");
        esp_https_ota_abort(handle);
        mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
        vTaskDelete(NULL);
        return;
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "Incomplete firmware received");
        ota_set_status(OTA_STATE_ERROR, -1, "Incomplete download");
        esp_https_ota_abort(handle);
        mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
        vTaskDelete(NULL);
        return;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ota_set_status(OTA_STATE_ERROR, -1, "Image validation failed");
        } else {
            ota_set_status(OTA_STATE_ERROR, -1, "Could not finalize the update");
        }
        mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
        vTaskDelete(NULL);
        return;
    }

    mutex_unlock(MUTEX_TYPE_OUTBOUND_TLS);
    ESP_LOGI(TAG, "OTA update successful, rebooting");
    ota_set_status(OTA_STATE_SUCCESS, 100, "Update complete, rebooting");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

esp_err_t ota_start_update(void)
{
    if (!wifi_is_connected()) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }

    mutex_lock(MUTEX_TYPE_OTA);
    bool busy = (s_state == OTA_STATE_CHECKING || s_state == OTA_STATE_DOWNLOADING);
    if (!busy) {
        s_state = OTA_STATE_DOWNLOADING;
        s_progress = 0;
        strlcpy(s_message, "Starting download", sizeof(s_message));
    }
    mutex_unlock(MUTEX_TYPE_OTA);

    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 8 KB stack: TLS + OTA writing needs a generous stack. */
    if (xTaskCreate(ota_update_task, "ota_update", 8192, NULL, 5, NULL) != pdPASS) {
        ota_set_status(OTA_STATE_ERROR, -1, "Could not start update task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) != ESP_OK) {
        return;
    }
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "Running image marked valid, rollback cancelled");
        } else {
            ESP_LOGE(TAG, "Failed to mark running image valid");
        }
    }
}
