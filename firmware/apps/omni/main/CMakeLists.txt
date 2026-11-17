# The Omni's own code (plan PD1): its faces, the dial and globe views, their console commands, the Omni fonts and its
# theme descriptor (omni_theme.c). Everything shared is the watch_ui component.
file(GLOB OMNI_FONTS "ui/fonts/*.c")
idf_component_register(SRCS "app_main.c" "omni_theme.c"
                            "ui/face_globe_live.c" "ui/face_globe_spin.c" "ui/face_globe_analog.c"
                            "ui/globe_view.c" "ui/globe_face.c" "ui/globe_pie.c" "ui/globe_pie.S"
                            "ui/dial_view.c" "ui/dial_console.c" "ui/face_omni.c"
                            "ui/face_timer.c" "ui/timer_svc.c" "ui/timer_glance.c" "ui/face_almanac.c" "ui/face_fade.c"
                            "ui/face_world.c" "ui/alm_cache.c"
                            ${OMNI_FONTS}
                       INCLUDE_DIRS "."
                       REQUIRES watch_ui omni_core board store
                       PRIV_REQUIRES lvgl__lvgl espressif__esp_mmap_assets waveshare__esp32_s3_touch_amoled_1_75c)
# lv_font_conv output includes "lvgl/lvgl.h" unless this is set
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# The "assets" partition (0x410000 + 0xBF0000 = 12 224 KB, R69, partitions.csv; it must end by 16 MB to be mappable): firmware/apps/omni/assets/ as an esp_mmap_assets image, written by
# `idf.py flash` (FLASH_IN_PROJECT; `idf.py app-flash` skips it). The packer also writes mmap_generate_assets.h
# (file count, checksum, enum) next to this file, so main compiles after it.
spiffs_create_partition_assets(assets ${CMAKE_CURRENT_SOURCE_DIR}/../assets FLASH_IN_PROJECT MMAP_FILE_SUPPORT_FORMAT ".bin,.tex,.json,.pcm" MMAP_FILE_ALIGNMENT 64)
add_dependencies(${COMPONENT_LIB} assets_assets_bin)
# The almanac face works once a minute (its two renderers are omni_core's, at the project's -O2): built for size (R131).
set_source_files_properties(ui/face_almanac.c PROPERTIES COMPILE_OPTIONS "-Os")
