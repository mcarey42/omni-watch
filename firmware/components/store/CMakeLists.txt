idf_component_register(SRCS "store.c"
                       INCLUDE_DIRS "."
                       REQUIRES omni_core
                       PRIV_REQUIRES board nvs_flash esp_timer)
