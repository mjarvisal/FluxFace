#include <stdio.h>
#include <string.h>
#include "config.h"

const timezones_t timezones[] = {
    {ALBANIA, "Albania", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {ANDORRA, "Andorra", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {ARMENIA, "Armenia", "AMT-4"},
    {AUSTRIA, "Austria", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {AZERBAIJAN, "Azerbaijan", "AZT-4"},
    {BELARUS, "Belarus", "MSK-3"},
    {BELGIUM, "Belgium", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {BOSNIA_AND_HERZEGOVINA, "Bosnia and Herzegovina", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {BULGARIA, "Bulgaria", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {CROATIA, "Croatia", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {CYPRUS, "Cyprus", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {CZECH_REPUBLIC, "Czech Republic", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {DENMARK, "Denmark", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {ESTONIA, "Estonia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {FINLAND, "Finland", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {FRANCE, "France", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {GEORGIA, "Georgia", "GET-4"},
    {GERMANY, "Germany", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {GREECE, "Greece", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {HUNGARY, "Hungary", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {ICELAND, "Iceland", "GMT0"},
    {IRELAND, "Ireland", "GMT0IST,M3.5.0/1,M10.5.0"},
    {ITALY, "Italy", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {KAZAKHSTAN, "Kazakhstan", "ALMT-6"},
    {KOSOVO, "Kosovo", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {LATVIA, "Latvia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {LIECHTENSTEIN, "Liechtenstein", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {LITHUANIA, "Lithuania", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {LUXEMBOURG, "Luxembourg", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {MALTA, "Malta", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {MOLDOVA, "Moldova", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {MONACO, "Monaco", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {MONTENEGRO, "Montenegro", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {NETHERLANDS, "Netherlands", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {NORTH_MACEDONIA, "North Macedonia", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {NORWAY, "Norway", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {POLAND, "Poland", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {PORTUGAL, "Portugal", "WET0WEST,M3.5.0/1,M10.5.0"},
    {ROMANIA, "Romania", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {RUSSIA, "Russia", "MSK-3"},
    {SAN_MARINO, "San Marino", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {SERBIA, "Serbia", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {SLOVAKIA, "Slovakia", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {SLOVENIA, "Slovenia", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {SPAIN, "Spain", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {SWEDEN, "Sweden", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {SWITZERLAND, "Switzerland", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {TURKEY, "Turkey", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {UKRAINE, "Ukraine", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {UNITED_KINGDOM, "United Kingdom", "GMT0BST,M3.5.0/1,M10.5.0"},
    {VATICAN_CITY, "Vatican City", "CET-1CEST,M3.5.0/2,M10.5.0/3"}
};

enum countries get_country_enum_from_string(const char* country_string) {
    for (size_t i = 0; i < sizeof(timezones) / sizeof(timezones_t); i++) {
        if (strcmp(timezones[i].country_string, country_string) == 0) {
            return timezones[i].country_enum;
        }
    }
    return -1; // Not found
}

const char* get_country_string_from_enum(enum countries country_enum) {
    for (size_t i = 0; i < sizeof(timezones) / sizeof(timezones_t); i++) {
        if (timezones[i].country_enum == country_enum) {
            return timezones[i].country_string;
        }
    }
    return "Unknown"; // Not found
}

const char* get_timezone_string_from_enum(enum countries country_enum) {
    for (size_t i = 0; i < sizeof(timezones) / sizeof(timezones_t); i++) {
        if (timezones[i].country_enum == country_enum) {
            return timezones[i].TZ;
        }
    }
    return "UTC"; // Default if not found
}