/*
 * SPDX-FileCopyrightText: 2010-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs.h"
#include "cJSON.h"
#include "wifi.h"
#include "config.h"
#include "prices.h"
#include "ota.h"
#include "mutex.h"

static const char *TAG = "esp-rest";

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];

    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    bool is_debug_log = (strcmp(filepath, "/www/debug.log") == 0);

    /* prices.json is rewritten by Prices_Task; serialize access so we never
     * read it while it is being truncated/rewritten. */
    bool lock_prices = (strstr(filepath, "prices.json") != NULL);
    if (lock_prices) {
        mutex_lock(MUTEX_TYPE_FILE);
    }

    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        if (lock_prices) {
            mutex_unlock(MUTEX_TYPE_FILE);
        }
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);
    if (is_debug_log) {
        /* The log changes continuously; never let a browser reuse a stale or
         * previously empty cached response. */
        httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
    }

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    size_t total_bytes = 0;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            total_bytes += (size_t)read_bytes;
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                if (lock_prices) {
                    mutex_unlock(MUTEX_TYPE_FILE);
                }
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    if (lock_prices) {
        mutex_unlock(MUTEX_TYPE_FILE);
    }
    if (is_debug_log) {
        ESP_LOGI(TAG, "Served %s: %zu bytes", filepath, total_bytes);
    }
    ESP_LOGI(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Settings handler */
static esp_err_t settings_post_handler(httpd_req_t *req)
{
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to update settings");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    mutex_lock(MUTEX_TYPE_FILE);
    FILE *f = fopen("/www/config.json", "w");
    if (f == NULL)
    {
            mutex_unlock(MUTEX_TYPE_FILE);
            ESP_LOGE(TAG, "Failed to open file for writing");
            return ESP_FAIL;
    }
    fwrite(buf, total_len, 1, f);
    fclose(f);
    mutex_unlock(MUTEX_TYPE_FILE);
    ESP_LOGI(TAG, "File written");
    httpd_resp_sendstr(req, "Settings updated successfully");
    (void) CONFIG_read_config_file();
    return ESP_OK;
}

/* Reboot handler */
static esp_err_t reboot_handler(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "Rebooting system");
    ESP_LOGI(TAG, "Rebooting system");
    esp_restart();
    return ESP_OK;
}

static const char *ota_state_to_string(ota_state_t state)
{
    switch (state) {
        case OTA_STATE_CHECKING:    return "checking";
        case OTA_STATE_DOWNLOADING: return "downloading";
        case OTA_STATE_SUCCESS:     return "success";
        case OTA_STATE_ERROR:       return "error";
        case OTA_STATE_IDLE:
        default:                    return "idle";
    }
}

/* Reads the remote firmware header and reports whether an update is available. */
static esp_err_t ota_check_handler(httpd_req_t *req)
{
    ota_check_result_t result;
    ota_check_update(&result);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "current", result.current);
    cJSON_AddStringToObject(root, "remote", result.remote);
    cJSON_AddBoolToObject(root, "available", result.available);
    cJSON_AddStringToObject(root, "error", result.error);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json != NULL ? json : "{}");

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Starts the download + flash of the new firmware. */
static esp_err_t ota_update_handler(httpd_req_t *req)
{
    esp_err_t err = ota_start_update();
    httpd_resp_set_type(req, "application/json");
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_sendstr(req, "{\"started\":false,\"error\":\"An update is already in progress\"}");
    } else if (err != ESP_OK) {
        httpd_resp_sendstr(req, "{\"started\":false,\"error\":\"Could not start the update\"}");
    } else {
        httpd_resp_sendstr(req, "{\"started\":true}");
    }
    return ESP_OK;
}

/* Reports the progress of an in-flight OTA update for the UI to poll. */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    ota_state_t state;
    int progress;
    char message[96];
    ota_get_status(&state, &progress, message, sizeof(message));

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "state", ota_state_to_string(state));
    cJSON_AddNumberToObject(root, "progress", progress);
    cJSON_AddStringToObject(root, "message", message);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json != NULL ? json : "{}");

    free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* Settings handler */
static esp_err_t init_wifi_handler(httpd_req_t *req)
{
    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Reinitializing wifi");
    esp_err_t err = init_wifi();
    if (err != ESP_OK)
    {
            ESP_LOGE(TAG, "Failed to reinitialize wifi");
            err = ESP_FAIL;
    }
    else
    {
        httpd_resp_sendstr(req, "Wifi reinitialization ok");
        ESP_LOGI(TAG, "Wifi reinitialization ok");
    }
    return err;
}

esp_err_t start_rest_server(const char *base_path)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(base_path && strlen(base_path) < ESP_VFS_PATH_MAX, ESP_ERR_INVALID_ARG, TAG, "Invalid base path");
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    ESP_RETURN_ON_FALSE(rest_context, ESP_ERR_NO_MEM, TAG, "No memory for rest context");
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    /* The /api/ota/check handler performs a TLS handshake (esp_https_ota_begin)
     * directly in the server task, which needs far more stack than the httpd
     * default (~4 KB). Enlarge it to avoid a stack overflow. */
    config.stack_size = 10240;

    ESP_LOGI(TAG, "Starting HTTP Server");
    ESP_GOTO_ON_ERROR(httpd_start(&server, &config), err, TAG, "Failed to start http server");

    /* URI handler for reinitializing wifi */
    httpd_uri_t wifi_reinit_get_uri = {
        .uri = "/api/wifi",
        .method = HTTP_GET,
        .handler = init_wifi_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &wifi_reinit_get_uri);

    /* URI handler for reboot */
    httpd_uri_t reboot_get_uri = {
        .uri = "/api/reboot",
        .method = HTTP_GET,
        .handler = reboot_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &reboot_get_uri);

    /* URI handler for settings */
    httpd_uri_t settings_post_uri = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = settings_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &settings_post_uri);

    /* URI handler for checking whether a firmware update is available */
    httpd_uri_t ota_check_get_uri = {
        .uri = "/api/ota/check",
        .method = HTTP_GET,
        .handler = ota_check_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_check_get_uri);

    /* URI handler for starting a firmware update */
    httpd_uri_t ota_update_post_uri = {
        .uri = "/api/ota/update",
        .method = HTTP_POST,
        .handler = ota_update_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_update_post_uri);

    /* URI handler for polling firmware update progress */
    httpd_uri_t ota_status_get_uri = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = ota_status_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &ota_status_get_uri);

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &common_get_uri);

    return ESP_OK;
err:
    if (rest_context) {
        free(rest_context);
    }
    return ret;
}
