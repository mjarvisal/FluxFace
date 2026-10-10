#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "mutex.h"

struct mutex_t {
    mutex_type_t type;
    SemaphoreHandle_t mutex;
};
typedef struct mutex_t mutex_t;

mutex_t mutexes[MUTEX_TYPE_COUNT] = {
    {MUTEX_TYPE_FILE, NULL},
    {MUTEX_TYPE_LED_STRIP, NULL},
    {MUTEX_TYPE_OTA, NULL}
};

void mutex_init ( void )
{
    for (int i = 0; i < MUTEX_TYPE_COUNT; i++) {
        mutexes[i].mutex = xSemaphoreCreateMutex();
    }
}

void mutex_lock ( mutex_type_t type )
{
    SemaphoreHandle_t mutex = mutexes[type].mutex;
    if (mutex != NULL) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
}

void mutex_unlock ( mutex_type_t type )
{
    SemaphoreHandle_t mutex = mutexes[type].mutex;
    if (mutex != NULL) {
        xSemaphoreGive(mutex);
    }
}