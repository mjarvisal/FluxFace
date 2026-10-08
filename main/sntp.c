#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "main.h"
#include "esp_sntp.h"
#include "config.h"

time_t last_sync = 0;

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
    const struct tm *timeinfo_ptr;
    struct config_t *config = CONFIG_config_file();

    time(&now); // Get current time (seconds since epoch)

    last_sync = now;

    // Set Timezone to UTC for this initial print
    // For local time, TZ needs to be set correctly before localtime_r
    setenv("TZ", config->timezones_info.country_string, 1);
    tzset();

    timeinfo_ptr = localtime(&now);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", timeinfo_ptr);
    ESP_LOGI(TAG, "Current time %s after sync: %s", config->timezones_info.country_string, strftime_buf);
}

void initialize_sntp(void) {
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