idf_component_register(SRCS "main.cpp" "zh14.c" "zh10.c" "zh28.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES
                    esp_driver_gpio
                    esp_driver_i2c
                    esp_driver_i2s
                    esp_driver_spi
                    esp_lcd
                    lvgl__lvgl
                    esp_lvgl_port
                    esp_event
                    esp_timer
                    bt
                    espressif__esp_codec_dev
                    espressif__esp-sr
                    espressif__esp_audio_codec
                    m5stack__m5pm1
                    nvs_flash
                    spi_flash)
