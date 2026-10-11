#ifndef MAIN_NETWORK_DIAG_H_
#define MAIN_NETWORK_DIAG_H_

#include <stddef.h>

#include "esp_err.h"

typedef struct {
    size_t free_bytes;
    size_t min_free_bytes;
    size_t largest_free_block;
    int open_sockets;
} network_diag_sample_t;

esp_err_t network_diag_start(void);
network_diag_sample_t network_diag_snapshot(const char *phase);
void network_diag_log_delta(const char *operation,
                            const network_diag_sample_t *before,
                            const network_diag_sample_t *after);
void network_diag_fetch_begin(void);
void network_diag_fetch_end(const network_diag_sample_t *before);

#endif /* MAIN_NETWORK_DIAG_H_ */
