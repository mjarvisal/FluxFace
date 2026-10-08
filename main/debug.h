#ifndef MAIN_DEBUG_H_
#define MAIN_DEBUG_H_

#include "esp_log.h"

//#define DEBUG_ENABLE
//#define DEBUG_PRINT

void Debug_Task ( void * pvParameters );
#ifdef DEBUG_PRINT
#define DEBUG_PRINTF(...) ESP_LOGI("APP_DEBUG", __VA_ARGS__)
#else
#define DEBUG_PRINTF(... )
#endif

#endif /* MAIN_DEBUG_H_ */