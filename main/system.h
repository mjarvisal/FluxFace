#ifndef MAIN_SYSTEM_H_
#define MAIN_SYSTEM_H_

#include "esp_wifi_types_generic.h"

typedef enum {
    SYSTEM_STATE_INIT,
    SYSTEM_STATE_RUNNING,
    SYSTEM_STATE_ERROR
} system_state_t;

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED
} wifi_state_t;

wifi_mode_t get_wifi_mode ( void );
wifi_mode_t set_wifi_mode ( wifi_mode_t new_mode );
wifi_state_t get_wifi_state ( void );
wifi_state_t set_wifi_state ( wifi_state_t new_state );
system_state_t get_system_state ( void );
void set_system_state ( system_state_t new_state );



#endif /* MAIN_SYSTEM_H_ */