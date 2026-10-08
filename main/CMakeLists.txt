idf_component_register(SRCS "system.c" "countries.c" "sntp.c" "json.c" "init.c" "debug.c" "debug_log.c" "led.c" "main.c" "wifi.c" "rest_server.c" "prices.c" "config.c" "ota.c" "mutex.c"
                       PRIV_REQUIRES esp_wifi esp_http_server json nvs_flash lwip esp_http_client esp-tls app_update esp_https_ota
                       INCLUDE_DIRS ".")

set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../www")
if(EXISTS ${WEB_SRC_DIR})
    littlefs_create_partition_image(www ${WEB_SRC_DIR} FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "'${WEB_SRC_DIR}' doesn't exist. Please run 'pnpm build' under '${WEB_SRC_DIR}'")
endif()