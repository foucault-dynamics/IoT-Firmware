idf_component_register(SRCS "sx127x.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_spi esp_driver_gpio
                       PRIV_REQUIRES esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wshadow)
