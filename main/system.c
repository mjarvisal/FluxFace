#include "system.h"
#include "esp_wifi_types_generic.h"

static system_state_t current_system_state = SYSTEM_STATE_INIT;
static wifi_mode_t current_wifi_mode = WIFI_MODE_NULL;
static wifi_state_t current_wifi_state = WIFI_STATE_DISCONNECTED;


// Wifi modes
wifi_mode_t get_wifi_mode ( void )
{
    return current_wifi_mode;
}

wifi_mode_t set_wifi_mode ( wifi_mode_t new_mode )
{
    current_wifi_mode = new_mode;
    return current_wifi_mode;
}

// Wifi states
wifi_state_t get_wifi_state ( void )
{
    return current_wifi_state;
}

wifi_state_t set_wifi_state ( wifi_state_t new_state )
{
    current_wifi_state = new_state;
    return current_wifi_state;
}

// System states
system_state_t get_system_state ( void )
{
    return current_system_state;
}

void set_system_state ( system_state_t new_state )
{
    current_system_state = new_state;
}