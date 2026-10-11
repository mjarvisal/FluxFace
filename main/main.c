/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "init.h"
#include "led.h"
#include "prices.h"
#include "debug.h"
#include "mutex.h"

/* TaskHandles */
TaskHandle_t LEDTaskHandle = NULL;
TaskHandle_t DebugTaskHandle = NULL;
TaskHandle_t PricesTaskHandle = NULL;

void app_main(void)
{
    init();
    xTaskCreatePinnedToCore(LED_Task, "LED_task", 2*8192, NULL, 11, &LEDTaskHandle, APP_CPU_NUM);
    
    mutex_init();
    initwifi();
    xTaskCreatePinnedToCore(Prices_Task, "Prices_task", 2*8192, NULL, 10, &PricesTaskHandle, APP_CPU_NUM);
#ifdef DEBUG_ENABLE
    xTaskCreate(Debug_Task, "Debug_task", 8192, NULL, 2, &DebugTaskHandle);
#endif

}
