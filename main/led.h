#ifndef MAIN_LED_H_
#define MAIN_LED_H_

void LED_Task ( void * pvParameters );
void LED_update_config ( void );
void LED_update_prices ( void );
void LED_reset_to_defaults ( void );
void LED_Configure ( void );
void LED_set_init_colors( void );

#endif /* MAIN_LED_H_ */