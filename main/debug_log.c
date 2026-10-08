#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "debug_log.h"

#define DEBUG_LOG_PATH "/www/debug.log"
#define DEBUG_LOG_MAX_BYTES (64 * 1024)
#define DEBUG_LOG_QUEUE_DEPTH 48
#define DEBUG_LOG_LINE_SIZE 384
#define DEBUG_LOG_TASK_STACK 4096

typedef struct {
    size_t length;
    char text[DEBUG_LOG_LINE_SIZE];
} debug_log_record_t;

static QueueHandle_t s_log_queue;
static vprintf_like_t s_previous_vprintf;

static int debug_log_vprintf(const char *format, va_list args)
{
    if (s_log_queue != NULL) {
        debug_log_record_t record;
        va_list file_args;
        va_copy(file_args, args);
        int formatted_length = vsnprintf(record.text, sizeof(record.text), format, file_args);
        va_end(file_args);

        if (formatted_length > 0) {
            record.length = (size_t)formatted_length;
            if (record.length >= sizeof(record.text)) {
                record.length = sizeof(record.text) - 1;
            }
            /* Never block ESP-IDF's logging path; the bounded queue intentionally
             * drops a record if the filesystem writer cannot keep up. */
            (void)xQueueSend(s_log_queue, &record, 0);
        }
    }

    if (s_previous_vprintf != NULL) {
        return s_previous_vprintf(format, args);
    }
    return vprintf(format, args);
}

static int open_log_file(size_t *file_size)
{
    int fd = open(DEBUG_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        return -1;
    }

    struct stat file_info;
    if (fstat(fd, &file_info) == 0 && file_info.st_size >= 0) {
        *file_size = (size_t)file_info.st_size;
    } else {
        *file_size = 0;
    }

    if (*file_size > DEBUG_LOG_MAX_BYTES) {
        if (ftruncate(fd, 0) != 0) {
            close(fd);
            return -1;
        }
        *file_size = 0;
    }
    return fd;
}

static bool write_log_record(int *fd, size_t *file_size, const debug_log_record_t *record)
{
    if (*fd < 0) {
        *fd = open_log_file(file_size);
        if (*fd < 0) {
            return false;
        }
    }

    if (*file_size + record->length > DEBUG_LOG_MAX_BYTES) {
        if (ftruncate(*fd, 0) != 0) {
            close(*fd);
            *fd = -1;
            return false;
        }
        *file_size = 0;
    }

    size_t written = 0;
    while (written < record->length) {
        ssize_t result = write(*fd, record->text + written, record->length - written);
        if (result <= 0) {
            close(*fd);
            *fd = -1;
            return false;
        }
        written += (size_t)result;
    }

    *file_size += written;
    return true;
}

static bool sync_log_file(int *fd)
{
    if (*fd < 0 || fsync(*fd) == 0) {
        return true;
    }

    close(*fd);
    *fd = -1;
    return false;
}

static void debug_log_writer_task(void *arg)
{
    (void)arg;
    int fd = -1;
    size_t file_size = 0;
    bool file_dirty = false;
    TickType_t last_sync = xTaskGetTickCount();
    debug_log_record_t record;

    for (;;) {
        BaseType_t received = xQueueReceive(s_log_queue, &record, pdMS_TO_TICKS(500));
        if (received == pdTRUE) {
            if (write_log_record(&fd, &file_size, &record)) {
                file_dirty = true;
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (file_dirty && (received != pdTRUE ||
                           (TickType_t)(now - last_sync) >= pdMS_TO_TICKS(500))) {
            (void)sync_log_file(&fd);
            file_dirty = false;
            last_sync = now;
        }
    }
}

esp_err_t debug_log_init(void)
{
    if (s_log_queue != NULL) {
        return ESP_OK;
    }

    s_log_queue = xQueueCreate(DEBUG_LOG_QUEUE_DEPTH, sizeof(debug_log_record_t));
    if (s_log_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_previous_vprintf = esp_log_set_vprintf(debug_log_vprintf);
    return ESP_OK;
}

esp_err_t debug_log_start(void)
{
    if (s_log_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t result = xTaskCreate(debug_log_writer_task, "debug_log", DEBUG_LOG_TASK_STACK,
                                    NULL, tskIDLE_PRIORITY + 1, NULL);
    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}