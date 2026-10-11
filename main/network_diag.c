#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lwip/sockets.h"

#if CONFIG_HEAP_TASK_TRACKING
#include "esp_heap_task_info.h"
#endif

#if CONFIG_HEAP_TRACING_STANDALONE
#include "esp_heap_trace.h"
#endif

#include "network_diag.h"

#define MONITOR_INTERVAL_MS 60000

static const char *TAG = "mon";

#if CONFIG_HEAP_TRACING_STANDALONE
static heap_trace_record_t s_heap_trace_records[256];

typedef struct {
    void *stack[CONFIG_HEAP_TRACING_STACK_DEPTH];
    size_t count;
    size_t bytes;
} leak_group_t;

static void log_heap_trace(void)
{
    enum { MAX_LEAK_GROUPS = 16 };
    leak_group_t groups[MAX_LEAK_GROUPS] = {0};
    size_t group_count = 0;
    size_t records = heap_trace_get_count();
    size_t ungrouped = 0;

    for (size_t i = 0; i < records; ++i) {
        heap_trace_record_t record;
        if (heap_trace_get(i, &record) != ESP_OK || record.freed) {
            continue;
        }

        size_t group = 0;
        while (group < group_count &&
               memcmp(groups[group].stack, record.alloced_by,
                      sizeof(groups[group].stack)) != 0) {
            ++group;
        }
        if (group == group_count) {
            if (group_count == MAX_LEAK_GROUPS) {
                ++ungrouped;
                continue;
            }
            memcpy(groups[group].stack, record.alloced_by,
                   sizeof(groups[group].stack));
            ++group_count;
        }
        ++groups[group].count;
        groups[group].bytes += record.size;
    }

    ESP_LOGI(TAG, "heap trace: %zu outstanding allocation records", records);
    for (size_t i = 0; i < group_count; ++i) {
        ESP_LOGI(TAG, "heap trace group: %zu allocations, %zu bytes, stack %p %p %p %p",
                 groups[i].count, groups[i].bytes,
                 groups[i].stack[0], groups[i].stack[1],
                 groups[i].stack[2], groups[i].stack[3]);
    }
    if (ungrouped != 0) {
        ESP_LOGW(TAG, "heap trace omitted %zu allocations from additional stacks",
                 ungrouped);
    }
}
#endif

static int open_socks(void)
{
    int count = 0;

    for (int fd = LWIP_SOCKET_OFFSET;
         fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS;
         ++fd) {
        int type;
        socklen_t length = sizeof(type);
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) == 0) {
            ++count;
        }
    }
    return count;
}

network_diag_sample_t network_diag_snapshot(const char *phase)
{
    network_diag_sample_t sample = {
        .free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        .min_free_bytes = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
        .largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
        .open_sockets = open_socks(),
    };

    ESP_LOGI(TAG, "%s free=%zu min=%zu maxblk=%zu socks=%d",
             phase, sample.free_bytes, sample.min_free_bytes,
             sample.largest_free_block, sample.open_sockets);
    return sample;
}

static int64_t size_delta(size_t after, size_t before)
{
    return after >= before ? (int64_t)(after - before) : -(int64_t)(before - after);
}

void network_diag_log_delta(const char *operation,
                            const network_diag_sample_t *before,
                            const network_diag_sample_t *after)
{
    ESP_LOGI(TAG, "%s delta free=%" PRId64 " maxblk=%" PRId64 " socks=%d",
             operation,
             size_delta(after->free_bytes, before->free_bytes),
             size_delta(after->largest_free_block, before->largest_free_block),
             after->open_sockets - before->open_sockets);
}

#if CONFIG_HEAP_TASK_TRACKING
static void log_task_heap_usage(void)
{
    enum { MAX_TRACKED_TASKS = 24 };
    heap_task_totals_t totals[MAX_TRACKED_TASKS] = {0};
    size_t total_count = 0;
    heap_task_info_params_t params = {
        .caps = {0},
        .mask = {0},
        .tasks = NULL,
        .num_tasks = 0,
        .totals = totals,
        .num_totals = &total_count,
        .max_totals = MAX_TRACKED_TASKS,
        .blocks = NULL,
        .max_blocks = 0,
    };

    (void)heap_caps_get_per_task_info(&params);
    for (size_t i = 0; i < total_count; ++i) {
        const char *name = pcTaskGetName(totals[i].task);
        ESP_LOGI(TAG, "task_heap task=%s current=%zu blocks=%zu",
                 name != NULL ? name : "deleted",
                 totals[i].size[0], totals[i].count[0]);
    }
}
#endif

#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
static void log_task_list(void)
{
    static char task_list[2048];
    vTaskList(task_list);
    ESP_LOGI(TAG, "tasks:\n%s", task_list);
}
#endif

static void monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        (void)network_diag_snapshot("periodic");
#if CONFIG_HEAP_TASK_TRACKING
        log_task_heap_usage();
#endif
#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
        log_task_list();
#endif
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

esp_err_t network_diag_start(void)
{
#if CONFIG_HEAP_TRACING_STANDALONE
    esp_err_t err = heap_trace_init_standalone(s_heap_trace_records,
                                               sizeof(s_heap_trace_records) /
                                                   sizeof(s_heap_trace_records[0]));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize heap tracing: %s", esp_err_to_name(err));
        return err;
    }
#endif

    if (xTaskCreate(monitor_task, "heap_monitor", 4096, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create heap monitor task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void network_diag_fetch_begin(void)
{
#if CONFIG_HEAP_TRACING_STANDALONE
    esp_err_t err = heap_trace_start(HEAP_TRACE_LEAKS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start fetch heap trace: %s", esp_err_to_name(err));
    }
#endif
}

void network_diag_fetch_end(const network_diag_sample_t *before)
{
#if CONFIG_HEAP_TRACING_STANDALONE
    esp_err_t err = heap_trace_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop fetch heap trace: %s", esp_err_to_name(err));
    }
#endif
    network_diag_sample_t after = network_diag_snapshot("prices after");
    network_diag_log_delta("price fetch cycle", before, &after);
#if CONFIG_HEAP_TRACING_STANDALONE
    if (err == ESP_OK) {
        log_heap_trace();
    }
#endif
}
