idf_component_register(SRCS "ssd1306.c" "ssd1306_font.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wshadow)
