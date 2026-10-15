idf_component_register(SRCS "main.cpp" "Display.cpp" "ClockFace.cpp" "NixieFace.cpp" "TouchKey.cpp" "WebApi.cpp" "LogBuffer.cpp" "Temperature.cpp" "Sun.cpp" "VfdFace.cpp"
                       INCLUDE_DIRS "."
                       EMBED_FILES "nixie_bg.bin" "nixie_glow.bin" "nixie_text.bin" "vfd_bg.bin" "vfd_glow.bin"
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
