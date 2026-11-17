idf_component_register(SRCS "board.c" "axp2101.c" "imu.c" "input.c" "power.c"
                       INCLUDE_DIRS "include"
                       REQUIRES omni_core freertos esp_lcd lvgl__lvgl
                       PRIV_REQUIRES espressif__esp_lvgl_adapter esp_driver_gpio esp_driver_i2c esp_timer esp_pm esp_wifi)

# power.c captures the BSP's private CO5300 panel/IO handles (for display off / sleep in-out).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_lcd_new_panel_co5300")
# board.c sets the BSP's LVGL draw buffers to 16 lines (each flush is staged in internal DMA RAM allocated once).
# board_init checks that the wraps ran.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_lv_adapter_register_display")
# board.c pins the LVGL task (CONFIG_OMNI_LVGL_CORE: core 0, R108).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_lv_adapter_init")
