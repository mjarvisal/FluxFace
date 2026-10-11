#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "main.h"
#include "esp_sntp.h"
#include "config.h"
#include "mutex.h"

time_t last_sync = 0;
static bool s_sntp_initialized = false;
static bool s_time_synchronized = false;

// SNTP Time Sync Notification Callback
void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Time synchronization event received!");
    ESP_LOGI(TAG, "Time successfully synchronized: %lld seconds, %ld microseconds since epoch",
             (long long)tv->tv_sec, (long)tv->tv_usec);

    // The system time is now set. We can try to get and print it.
    // Note: settimeofday is usually called by the SNTP service itself.
    // If not, you would call it here: settimeofday(tv, NULL);

    // Example: Get current time and print it as a basic string
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;
    struct config_t *config = CONFIG_config_file();

    if (config == NULL) {
        ESP_LOGE(TAG, "Cannot set local time: configuration is unavailable");
        return;
    }

    time(&now); // Get current time (seconds since epoch)

    last_sync = now;

    mutex_lock(MUTEX_TYPE_TIMEZONE);
    if (localtime_r(&now, &timeinfo) == NULL) {
        mutex_unlock(MUTEX_TYPE_TIMEZONE);
        ESP_LOGE(TAG, "Failed to convert synchronized time to local time");
        return;
    }
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    s_time_synchronized = true;
    mutex_unlock(MUTEX_TYPE_TIMEZONE);
    ESP_LOGI(TAG, "Current time %s after sync: %s", config->timezones_info.country_string, strftime_buf);
}

bool sntp_time_is_set(void)
{
    mutex_lock(MUTEX_TYPE_TIMEZONE);
    bool synchronized = s_time_synchronized;
    mutex_unlock(MUTEX_TYPE_TIMEZONE);
    return synchronized;
}

void initialize_sntp(void) {
    mutex_lock(MUTEX_TYPE_TIMEZONE);
    if (s_sntp_initialized) {
        mutex_unlock(MUTEX_TYPE_TIMEZONE);
        ESP_LOGI(TAG, "SNTP client is already initialized");
        return;
    }
    s_sntp_initialized = true;
    mutex_unlock(MUTEX_TYPE_TIMEZONE);

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    // You can use a specific server or a pool
    esp_sntp_setservername(0, "pool.ntp.org"); 
    // For multiple servers:
    // esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    // esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH); // Optional: for smooth adjustment
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP initialized. Waiting for time synchronization...");
}