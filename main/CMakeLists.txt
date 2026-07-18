idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt esp_driver_gpio nvs_flash)
