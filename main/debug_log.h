#ifndef MAIN_DEBUG_LOG_H_
#define MAIN_DEBUG_LOG_H_

#include "esp_err.h"

/* Install the ESP_LOG output hook before filesystem initialization, then start
 * the file writer after LittleFS has been mounted. */
esp_err_t debug_log_init(void);
esp_err_t debug_log_start(void);

#endif /* MAIN_DEBUG_LOG_H_ */