idf_component_register(SRCS "net.c" "ntp_client.c"
                       INCLUDE_DIRS "."
                       REQUIRES omni_core
                       PRIV_REQUIRES board bt nvs_flash esp_wifi esp_netif esp_event esp_timer bootloader_support lwip
                                     espressif__network_provisioning protocomm)
