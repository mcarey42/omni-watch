# The Mining Ship app (rd wave 1 Task 4): watch_ui with its theme descriptor (rd_theme.c: the faces, Digital in every
# unbuilt face's place until its task lands), the prefs service (rd_prefs_svc.c: the Settings rows, the boot decision,
# the carousel offer, the lift face), the chime's plan for face_kit's chime service (rd_chime.c, Task 8), the
# time-asleep record in RTC memory and its hooks (rd_asleep.c, Task 9) and the `rd` console command (rd_console.c,
# console builds only). No Omni code.
idf_component_register(SRCS "app_main.c" "rd_theme.c" "rd_prefs_svc.c" "rd_chime.c" "rd_asleep.c" "rd_console.c"
                       INCLUDE_DIRS "."
                       REQUIRES watch_ui omni_core store rd_core face_kit
                       PRIV_REQUIRES lvgl__lvgl espressif__esp_mmap_assets waveshare__esp32_s3_touch_amoled_1_75c)

# The "assets" partition (partitions.csv): an esp_mmap_assets image of firmware/apps/rd/assets/ (Task 4: the three
# brass backgrounds, byte-equal copies of the Omni's; Task 7 on: tools/rd_assets.sh's committed outputs). rd has no
# private art overlay (spec section 13): every rd build packs the committed set as it is.
#   - The packer writes mmap_generate_assets.h (file count, checksum) into ${CMAKE_BINARY_DIR}, so each build dir
#     compiles its own image's checksum (G3).
get_filename_component(rd_assets_src "${CMAKE_CURRENT_SOURCE_DIR}/../assets" REALPATH)
# A stale header beside this file (esp_mmap_assets' default location) would shadow the build dir's: a quoted include
# finds the source file's own directory first. Refuse it; the build-time copy refuses it again, and rd_theme.c requires
# the copy's RD_MMAP_PER_BUILD_DIR.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mmap_generate_assets.h")
    message(FATAL_ERROR "A stale ${CMAKE_CURRENT_SOURCE_DIR}/mmap_generate_assets.h exists. It would shadow this build "
                        "dir's assets header and compile another image's checksum into the app (G3). Delete it.")
endif()
spiffs_create_partition_assets(assets "${rd_assets_src}" FLASH_IN_PROJECT MMAP_FILE_SUPPORT_FORMAT ".bin,.pcm"
                               MMAP_FILE_ALIGNMENT 64 IMPORT_INC_PATH "${CMAKE_BINARY_DIR}")
# The packer writes the header as a side effect ninja does not know about, so a source that includes it straight from
# there is not recompiled when the image changes. rd_theme.c includes a copy declared as a build output instead, made
# after each new image by rd_stage.cmake (PR31).
set(rd_mmap_dir "${CMAKE_BINARY_DIR}/rd_mmap")
add_custom_command(OUTPUT "${rd_mmap_dir}/mmap_generate_assets.h"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${rd_mmap_dir}"
                   COMMAND ${CMAKE_COMMAND} "-DIN=${CMAKE_BINARY_DIR}/mmap_generate_assets.h"
                           "-DOUT=${rd_mmap_dir}/mmap_generate_assets.h"
                           "-DSTALE=${CMAKE_CURRENT_SOURCE_DIR}/mmap_generate_assets.h"
                           -P "${CMAKE_CURRENT_SOURCE_DIR}/../rd_stage.cmake"
                   DEPENDS "${CMAKE_BINARY_DIR}/mmap_build/assets/assets/assets.bin"
                           "${CMAKE_CURRENT_SOURCE_DIR}/../rd_stage.cmake"
                   COMMENT "The rd assets image's header (file count, checksum) for this build dir"
                   VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE "${rd_mmap_dir}/mmap_generate_assets.h")
target_include_directories(${COMPONENT_LIB} PRIVATE "${rd_mmap_dir}")
add_dependencies(${COMPONENT_LIB} assets_assets_bin)
