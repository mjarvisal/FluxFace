typedef enum {
    MUTEX_TYPE_FILE,
    MUTEX_TYPE_LED_STRIP,
    MUTEX_TYPE_OTA,
    MUTEX_TYPE_COUNT
} mutex_type_t;

void mutex_init ( void );
void mutex_lock ( mutex_type_t type );
void mutex_unlock ( mutex_type_t type );
