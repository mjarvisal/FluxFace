#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "main.h"
#include "debug.h"

void Debug_Task ( void * pvParameters )
{
    time_t now;
    char strftime_buf[64];
    struct tm timeinfo;

    while (1)
    {
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "Current time (UTC) debug: %s", strftime_buf);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}