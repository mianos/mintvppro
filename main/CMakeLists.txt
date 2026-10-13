idf_component_register(SRCS "main.cpp" "Display.cpp" "ClockFace.cpp" "TouchKey.cpp" "WebApi.cpp" "LogBuffer.cpp" "Temperature.cpp" "Sun.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES
                         nvsstoragemanager
                         wifimanager
                         webserver
                         jsonwrapper
                         mqttwrapper
                         settingsbase
                         app_update
                         esp_lcd
                         esp_driver_spi
                         esp_driver_ledc
                         esp_driver_gpio
                         esp_driver_touch_sens
                         esp_wifi
                         esp_netif
                         esp_event
                         nvs_flash)
