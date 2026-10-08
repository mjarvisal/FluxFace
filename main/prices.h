#ifndef MAIN_PRICES_H_
#define MAIN_PRICES_H_

#include <time.h>

typedef struct prices_t {
    time_t timestamp;
    double price_per_kwh;
}prices_t;

typedef struct price_update_info_t {
    prices_t *prices;
    time_t *last_update;
}price_update_info_t;

void Prices_Task ( void * pvParameters );
const price_update_info_t* get_prices( void );

#endif /* MAIN_PRICES_H_ */