# watch_ui (plan PD1): the UI every theme firmware shares. It never includes an app header: what it needs from the
# app comes through the theme descriptor (include/watch_theme.h). Kconfig: the shared options (PD3).
file(GLOB WATCH_UI_FONTS "fonts/*.c")
idf_component_register(SRCS "watch_main.c" "debug_console.c" "sfx.c"
                            "theme.c" "theme_ring.c" "shell.c" "glance.c" "face_digital.c" "scr_provision.c" "stepart.c" "scr_netlist.c" "scr_keyboard.c"
                            "scr_picker.c" "thumbring.c" "thumbring_ops.c" "scr_faces.c" "scr_quick.c" "scr_settings.c" "drum.c" "assets.c" "fontasset.c" "disp_stats.c" "disp_direct.c"
                            ${WATCH_UI_FONTS}
                       INCLUDE_DIRS "include"
                       REQUIRES omni_core board store net console esp_driver_usb_serial_jtag esp_timer esp_pm
                                esp_partition esp_rom
                       PRIV_REQUIRES esp_app_format lvgl__lvgl espressif__qrcode espressif__esp_mmap_assets esp_driver_i2s
                                     espressif__esp_codec_dev)
# lv_font_conv output includes "lvgl/lvgl.h" unless this is set
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
# R223: the track's new Setup screens compile for size (-Os, after the build's -O2); LVGL's drawing and every other file
# keep the project's optimisation. R228: the picker and thumbring's non-drawing half too; thumbring.c (the picker's pixel
# path) stays -O2. R229: scr_settings.c and scr_provision.c too, from Task 14 on (S1's pixel-identity captures are of
# the earlier images; S1 re-checks their frame time and stack). theme_ring.c: the hold keys' ring (Task 14).
set_source_files_properties(scr_netlist.c scr_keyboard.c scr_faces.c scr_picker.c thumbring_ops.c scr_settings.c
                            scr_provision.c theme_ring.c PROPERTIES COMPILE_OPTIONS "-Os")
