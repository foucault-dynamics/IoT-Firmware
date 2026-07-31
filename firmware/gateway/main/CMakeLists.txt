idf_component_register(SRCS "gateway.c"
                       REQUIRES shared_payload project_config sx127x ssd1306 mqtt
                       PRIV_REQUIRES nvs_flash esp_event esp_netif esp_wifi)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wshadow)
