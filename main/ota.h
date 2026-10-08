#ifndef MAIN_OTA_H_
#define MAIN_OTA_H_

#include "esp_err.h"
#include <stdbool.h>

/* Maximum length of a firmware version string (esp_app_desc_t.version is 32). */
#define OTA_VERSION_MAX_LEN 32

typedef enum {
    OTA_STATE_IDLE,        /* nothing in progress */
    OTA_STATE_CHECKING,    /* reading remote firmware header */
    OTA_STATE_DOWNLOADING, /* downloading + writing the new image */
    OTA_STATE_SUCCESS,     /* image written, device about to reboot */
    OTA_STATE_ERROR        /* last operation failed, see message */
} ota_state_t;

/* Result of an update check. */
typedef struct {
    char current[OTA_VERSION_MAX_LEN];
    char remote[OTA_VERSION_MAX_LEN];
    bool available;   /* true if remote differs from current */
    char error[96];   /* empty on success, otherwise a human readable reason */
} ota_check_result_t;

/* Copies the running firmware version into dst (at most OTA_VERSION_MAX_LEN). */
void ota_get_current_version(char *dst, size_t dst_len);

/* Connects to the update server, reads only the firmware image header and
 * compares its version with the running firmware. No flash is written.
 * Always fills result->current; fills result->remote/available on success and
 * result->error on failure. Returns ESP_OK when the check completed. */
esp_err_t ota_check_update(ota_check_result_t *result);

/* Starts a background task that downloads and flashes the firmware, then
 * reboots into it. Returns ESP_ERR_INVALID_STATE if an OTA is already running.
 * Progress is observable through ota_get_status(). */
esp_err_t ota_start_update(void);

/* Fills the current OTA state, download progress percent (0-100, -1 if unknown)
 * and the last status/error message. */
void ota_get_status(ota_state_t *state, int *progress, char *message, size_t message_len);

/* Confirms the running image is healthy so the bootloader keeps it. Call once
 * the system has reached a known-good state; otherwise a pending image is
 * rolled back on the next reboot. Safe to call when no rollback is pending. */
void ota_mark_valid(void);

#endif /* MAIN_OTA_H_ */
