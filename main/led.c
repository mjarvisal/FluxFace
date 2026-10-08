#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "main.h"
#include "led_strip.h"
#include "led.h"
#include "prices.h"
#include "config.h"
#include "debug.h"
#include "system.h"
#include "wifi.h"

#undef DEBUG_ENABLE
#undef DEBUG_PRINT

#define TASK_DELAY_MS 50

#define INIT_COLORS 255

// Set to 1 to use DMA for driving the LED strip, 0 otherwise
// Please note the RMT DMA feature is only available on chips e.g. ESP32-S3/P4
#if CONFIG_IDF_TARGET_ESP32S3
#define LED_STRIP_USE_DMA  1
#else
#define LED_STRIP_USE_DMA  0
#endif

#if LED_STRIP_USE_DMA
// Numbers of the LED in the strip
#define LED_STRIP_LED_COUNT 48
#define LED_STRIP_MEMORY_BLOCK_WORDS 1024 // this determines the DMA block size
#else
// Numbers of the LED in the strip
#define LED_STRIP_LED_COUNT 48
#define LED_STRIP_MEMORY_BLOCK_WORDS 256 // let the driver choose a proper memory block size automatically
#endif // LED_STRIP_USE_DMA

// GPIO assignment
#define LED_STRIP_GPIO_PIN  16

// 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define LED_STRIP_RMT_RES_HZ  (40 * 1000 * 1000)

#define ADDITION 0
#define REDUCTION 1

static inline int clamp_color(double v)
{
    int r = (int)round(v);
    if (r < 0) return 0;
    if (r > 255) return 255;
    return r;
}

static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

static const price_update_info_t *price_info = NULL;
static config_t *led_gen_config = NULL;
static time_t now = 0;
static led_strip_handle_t led_strip = NULL;
static bool NightModeActive = false;

led_strip_handle_t configure_led(void)
{
    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO_PIN, // The GPIO that connected to the LED strip's data line
        .max_leds = LED_STRIP_LED_COUNT,      // The number of LEDs in the strip,
        .led_model = LED_MODEL_SK6812,        // LED strip model
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW, // The color order of the strip: GRB
        .flags = {
            .invert_out = false, // don't invert the output signal
        }
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,        // different clock source can lead to different power consumption
        .resolution_hz = LED_STRIP_RMT_RES_HZ, // RMT counter clock frequency
        .mem_block_symbols = LED_STRIP_MEMORY_BLOCK_WORDS, // the memory block size used by the RMT channel
        .flags = {
            .with_dma = LED_STRIP_USE_DMA,     // Using DMA can improve performance when driving more LEDs
        }
    };

    // LED Strip object handle
    led_strip_handle_t led_strip_tmp = NULL;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip_tmp));
    ESP_LOGI(TAG, "Created LED strip object with RMT backend");
    return led_strip_tmp;
}

void LED_update_config ( void )
{
    taskENTER_CRITICAL(&my_spinlock);
    led_gen_config = CONFIG_config_file();
    taskEXIT_CRITICAL(&my_spinlock);
}

void LED_update_prices ( void )
{
    taskENTER_CRITICAL(&my_spinlock);
    price_info = get_prices();
    taskEXIT_CRITICAL(&my_spinlock);
}

void LED_reset_to_defaults ( void ) 
{
    led_strip_clear(led_strip);

    ESP_ERROR_CHECK(led_strip_set_pixel_rgbw(led_strip, 0,
        255,
        0,
        0,
        0) );

    /* Refresh the strip to send data (bounded retries) */
    esp_err_t err = ESP_FAIL;
    int retries = 0;
    const int max_retries = 5;
    while(err != ESP_OK && retries < max_retries) {
        err = led_strip_refresh(led_strip);
        retries++;
    }
}

bool prices_and_config_ready ( void )
{
    if (get_system_state() != SYSTEM_STATE_RUNNING)
    {
        return false;
    }

    if ( price_info != NULL &&\
         led_gen_config != NULL &&\
         get_wifi_mode() == WIFI_MODE_STA &&\
         get_system_state() == SYSTEM_STATE_RUNNING &&\
         (now - *price_info->last_update) > 0 &&\
         (now - *price_info->last_update) <= 172800 &&\
         now > 1735689600 ) // after Jan 1, 2025
        return true;
    else
    {
        return false;
    }
}

uint calculate_price_index ( int led_index, const struct tm* timeinfo )
{
    int correction = ceil((12.0 - ((double)timeinfo->tm_hour + (double)timeinfo->tm_min/60.0))*4.0);

    int price_index = (led_index + correction)%48 < 0 ? (led_index + correction)%48 + 48 : (led_index + correction)%48;

    price_index = price_index >= 48 ? price_index - 48 : price_index;
    price_index = price_index < 0 ? 0 : price_index;

    return (uint)price_index;
}

uint calculate_limit_index ( int led_index, uint price_index )
{
    double price = price_info->prices[price_index].price_per_kwh;
    uint limit = MAX_LIMITS; // default to highest limit

    for ( int i=0; i<led_gen_config->Limits_number; i++ )
    {
        if ( price <= led_gen_config->Limits[i].LED_limit )
        {
            if (i <= MAX_LIMITS)
                limit = i;
            break;
        }
    }
    return limit;
}

void debug_log_price_index ( uint i, uint price_index )
{
#ifdef DEBUG_ENABLE
    const struct tm *timeinfo;
    char strftime_buf[64];
    const struct tm *timeinfo_now;
    char strftime_now_buf[64];
    
    timeinfo_now = localtime_r(&now, &timeinfo_now);
    strftime(strftime_now_buf, sizeof(strftime_now_buf), "%c", timeinfo_now);
    localtime_r(&price_info->prices[price_index].timestamp, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Led_index %d: price_index: %d time_price: %s time_now:%s price: %.3f", i, price_index, strftime_buf, strftime_now_buf, price_info->prices[price_index].price_per_kwh);
#endif
}

bool parseTime ( void )
{
    struct tm TimeNow, NightTimeStart, NightTimeEnd;
    int nowtime, starttime, endtime;

    localtime_r(&now, &TimeNow);
    if (!led_gen_config) return false;

    char temp[3] = {0};
    // Start time (expect format "HH:MM")
    strncpy(temp, &led_gen_config->NightStart[0], 2);
    temp[2] = '\0';
    NightTimeStart.tm_hour = atoi(temp);
    strncpy(temp, &led_gen_config->NightStart[3], 2);
    temp[2] = '\0';
    NightTimeStart.tm_min = atoi(temp);

    // End time
    strncpy(temp, &led_gen_config->NightEnd[0], 2);
    temp[2] = '\0';
    NightTimeEnd.tm_hour = atoi(temp);
    strncpy(temp, &led_gen_config->NightEnd[3], 2);
    temp[2] = '\0';
    NightTimeEnd.tm_min = atoi(temp);

    nowtime = TimeNow.tm_hour*60 + TimeNow.tm_min;
    starttime = NightTimeStart.tm_hour*60 + NightTimeStart.tm_min;
    endtime = NightTimeEnd.tm_hour*60 + NightTimeEnd.tm_min;
    
    if (starttime <= endtime)
    {
        return nowtime >= starttime && nowtime < endtime;
    }
    else
    {
        return nowtime >= starttime || nowtime < endtime;
    }
}

bool isNightModeActive( void )
{
    if (!led_gen_config) return false;
    if (led_gen_config->NightModeEnabled) {
        return parseTime();
    }
    return false;
}

int calculate_pulsing ( void )
{
    if (!led_gen_config) return 0;

    int max_pulsing_amount = led_gen_config->CurrentPulsingAmount;
    if (max_pulsing_amount <= 0) return 0;
    if (max_pulsing_amount > 100) max_pulsing_amount = 100;

    double speed = led_gen_config->CurrentPulsingSpeed;
    if (speed <= 0.0) return 0;
    if (speed > 5.0) speed = 5.0;

    unsigned long now_ms = (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    unsigned long period_ms = (unsigned long)(speed * 1000.0);
    if (period_ms == 0) return 0;

    unsigned long phase_ms = now_ms % period_ms;
    double phase = (double)phase_ms / (double)period_ms;
    double wave = (phase <= 0.5) ? (phase * 2.0) : ((1.0 - phase) * 2.0);
    int pulsing = (int)round(wave * max_pulsing_amount);
    return pulsing;
}

void set_led_colors(int i, uint price_index, uint limit, led_strip_handle_t led_strip_handle)
{
    double brightness = 0.5; /* default to 50% */

    int red, green, blue, white;

    if (led_gen_config != NULL)
    {
        if (NightModeActive) {
            brightness = (double)led_gen_config->NightBrightness / 100.0;
        } else {
            brightness = (double)led_gen_config->DayBrightness / 100.0;
        }
    }

    // INIT state
    if ((limit == INIT_COLORS) ||\
        (wifi_is_connected() == false) ||\
        (get_system_state() != SYSTEM_STATE_RUNNING) ||\
        (prices_and_config_ready() == false))
    {
        red = 0;
        green = 0; 
        blue = 0;
        white = 0;
        if (i == 3 || i == 44)
        {
            red = 200;
        }
    }
    else
    {
        // No price
        if (price_info->prices[price_index].timestamp == 0)
        {
            red = 50;
            green = 50;
            blue = 50;
            white = 0;
        }
        else
        {
            red = led_gen_config->Limits[limit].LED_color.red;
            green = led_gen_config->Limits[limit].LED_color.green;
            blue = led_gen_config->Limits[limit].LED_color.blue;
            white = 0;
        }
    }

    if (price_index == 0)
    {
        if (led_gen_config != NULL && led_gen_config->CurrentPulsingEnabled)
        {
            int pulsing_amount = calculate_pulsing();
            brightness = brightness * (1.0 - (double)pulsing_amount / 100.0);
            if (brightness < 0.0) brightness = 0.0;
        }
    }

    int r = clamp_color(brightness * red);
    int g = clamp_color(brightness * green);
    int b = clamp_color(brightness * blue);
    int w = clamp_color(brightness * white);

    ESP_ERROR_CHECK(led_strip_set_pixel_rgbw(led_strip_handle, i, r, g, b, w));
}

void LED_set_init_colors( void )
{
    taskENTER_CRITICAL(&my_spinlock);
    for(int i=0; i<LED_STRIP_LED_COUNT; i++)
    {
        set_led_colors(i, 1, INIT_COLORS, led_strip);
    }
    taskEXIT_CRITICAL(&my_spinlock);
}

void LED_Configure ( void )
{
    led_strip = configure_led();
}

void LED_Task ( void * pvParameters )
{
#ifdef DEBUG_ENABLE
    int delay = 0;
#endif
    LED_Configure();

    LED_update_prices();
    LED_update_config();

    while (1) {
    time(&now);

    NightModeActive = isNightModeActive();

    if (get_system_state() == SYSTEM_STATE_RUNNING)
    {
        if (prices_and_config_ready())
        {
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);

            taskENTER_CRITICAL(&my_spinlock);
            for(int i=0; i<LED_STRIP_LED_COUNT; i++)
            {
                uint price_index = calculate_price_index(i, &timeinfo);
                uint limit = calculate_limit_index(i, price_index);

                debug_log_price_index (i, price_index );

                set_led_colors(i, price_index, limit, led_strip);
            }
            taskEXIT_CRITICAL(&my_spinlock);
        }
        else // Error color
        {   
            taskENTER_CRITICAL(&my_spinlock);
            for(int i=0; i<LED_STRIP_LED_COUNT; i++)
            {
                set_led_colors(i, 1, MAX_LIMITS, led_strip);
            }
            taskEXIT_CRITICAL(&my_spinlock);
#ifdef DEBUG_ENABLE
            if (delay <= 0)
            {
                DEBUG_PRINTF("Price info ptr: %p\nconfig ptr: %p\n last update: %lld\n now: %lld\ndiff: %lld\n", price_info, led_gen_config, (price_info != NULL) ? *price_info->last_update : 0, now, (price_info != NULL) ? (now - *price_info->last_update) : 0);
                delay = 10000;
            }
#endif
        }
#ifdef DEBUG_ENABLE
        delay -= TASK_DELAY_MS;
#endif
        /* Refresh the strip to send data (bounded retries) */
        esp_err_t err = ESP_FAIL;
        int retries = 0;
        const int max_retries = 5;
        while (err != ESP_OK && retries < max_retries) {
            err = led_strip_refresh(led_strip);
            retries++;
        }
    }
    else
    {
        LED_reset_to_defaults();
    }
    vTaskDelay(pdMS_TO_TICKS(TASK_DELAY_MS));
    }
}