#ifndef MAIN_WIFI_H_
#define MAIN_WIFI_H_

esp_err_t init_wifi ( void );
void initialize_sntp ( void );
void wifi_init_softap ( void );
esp_err_t wifi_init_sta ( void );
bool wifi_is_connected ( void );

#endif /* MAIN_WIFI_H_ */