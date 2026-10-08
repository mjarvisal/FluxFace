#ifndef MAIN_CONFIG_H_
#define MAIN_CONFIG_H_

#include "stdbool.h"
#include "esp_err.h"

#define MAX_LIMITS 7

enum countries {
    ALBANIA,
    ANDORRA,
    ARMENIA,
    AUSTRIA,
    AZERBAIJAN,
    BELARUS,
    BELGIUM,
    BOSNIA_AND_HERZEGOVINA,
    BULGARIA,
    CROATIA,
    CYPRUS,
    CZECH_REPUBLIC,
    DENMARK,
    ESTONIA,
    FINLAND,
    FRANCE,
    GEORGIA,
    GERMANY,
    GREECE,
    HUNGARY,
    ICELAND,
    IRELAND,
    ITALY,
    KAZAKHSTAN,
    KOSOVO,
    LATVIA,
    LIECHTENSTEIN,
    LITHUANIA,
    LUXEMBOURG,
    MALTA,
    MOLDOVA,
    MONACO,
    MONTENEGRO,
    NETHERLANDS,
    NORTH_MACEDONIA,
    NORWAY,
    POLAND,
    PORTUGAL,
    ROMANIA,
    RUSSIA,
    SAN_MARINO,
    SERBIA,
    SLOVAKIA,
    SLOVENIA,
    SPAIN,
    SWEDEN,
    SWITZERLAND,
    TURKEY,
    UKRAINE,
    UNITED_KINGDOM,
    VATICAN_CITY
};

typedef struct timezones_t {
    enum countries country_enum;
    char country_string[64];
    char TZ[64];
}timezones_t;
typedef struct LED_color_t {
    char RGB[16];
    int red;
    int green;
    int blue;
} LED_color_t;

typedef struct LEDs_t {
    LED_color_t LED_color;
    double LED_limit;
    int LED_brightness;
} LEDS_t;

typedef struct config_t {
    struct timezones_t timezones_info;
    LEDS_t Limits[MAX_LIMITS + 1]; // MAX configurable limits 7 + Overlimit color
    int Limits_number;
    char WIFISSID[32];
    char WIFIPassword[64];
    bool CurrentPulsingEnabled;
    int CurrentPulsingAmount;
    double CurrentPulsingSpeed;
    int DayBrightness;
    bool NightModeEnabled;
    int NightBrightness;
    char NightStart[8];
    char NightEnd[8];
    int PartyMode;
} config_t;

esp_err_t CONFIG_read_config_file( void );
config_t* CONFIG_config_file( void );
#endif /* MAIN_CONFIG_H_ */