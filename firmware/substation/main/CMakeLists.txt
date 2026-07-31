idf_component_register(SRCS "substation.c"
                       REQUIRES shared_payload project_config sx127x
                       PRIV_REQUIRES nvs_flash esp_event esp_netif esp_wifi esp_timer)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wextra -Wshadow)
