# The dw app (dw wave 1): watch_ui with its theme descriptor (dw_theme.c: face_kit's hooks and faces), the prefs
# service (the Settings rows, WD6's boot decision, the lift face), the numeral key screen (scr_key.c, SCR_THEME), the
# hourly chime's plan (dw_chime.c, for face_kit's chime service) and the `dw` console command (dw_console.c, console
# builds only). rd wave 1 Task 2: the face canvas and the reveal (fk_view.c), the face runtime (fk_face.c) and the
# chime service (fk_chime_svc.c) moved to face_kit (firmware/facekit/face_kit/ui), the doors behind its reveal ops.
# No Omni code: check_apps fails the build on any Omni symbol in omni_watch_dw.elf.
idf_component_register(SRCS "app_main.c" "dw_theme.c" "dw_prefs_svc.c" "dw_console.c" "scr_key.c" "dw_chime.c"
                       INCLUDE_DIRS "."
                       REQUIRES watch_ui omni_core store dw_core face_kit
                       PRIV_REQUIRES lvgl__lvgl espressif__esp_mmap_assets waveshare__esp32_s3_touch_amoled_1_75c)

# The "assets" partition (partitions.csv): an esp_mmap_assets image of firmware/apps/dw/assets/ (the generic art set,
# the three brass backgrounds, byte-equal copies of the Omni's, and the four sounds tools/gen_sfx_dw.py synthesises;
# check_apps allows no other audio; wave 2 Task 13: the Vortex table, dw_v12_lut.dat, hence .dat), or of that set with the private art overlay's files over it (dw wave 1 Task 9,
# R132, R136 item 4; wave 2 Task 4 adds the third):
#   - DW_ART_OVERLAY (a CMake cache variable only, never the environment, G2) names the overlay's clone. It is refused
#     for firmware/apps/dw/build (the generic build stays generic) and for a path inside this repository.
#   - With it, a build-time command (G4) stages the assets into ${CMAKE_BINARY_DIR}/dw_assets/assets (the directory's
#     name stays "assets", so the packer's header is still mmap_generate_assets.h and the image keeps its path), then
#     copies over only the swap files (dw_doors_m.bin, dw_b5_box.bin, dw_doors_c.bin; public release Task 9 adds
#     dw_th_b5.bin, dw_th_d3.bin and dw_chime.pcm) from the overlay's assets/, refusing any other name its overlay.json
#     lists and any cf or size mismatch, or a chime of an odd size or over 110 080 B (G7; main/dw_stage.cmake). The
#     descriptor's list is checked at configure too (dw_stage.cmake MODE=validate), so a bad name fails the configure.
#   - The overlay's optional "names" (public release Task 9) become ${CMAKE_BINARY_DIR}/dw_names/dw_names_overlay.h at
#     configure (dw_stage.cmake MODE=names) and DW_NAMES_OVERLAY, so the private build shows the overlay's theme, face
#     and group names (main/dw_names.h); a configure without them deletes that header.
#   - `dw verify` (dw_console.c) looks a still up in the overlay's verify_crc_overlay.h first (rows only for the faces
#     whose bind reads a swap asset), then in dw/verify_crc.h (R147 PR3: the overlay's table supplements the generic
#     one).
#   - Without it, firmware/apps/dw/assets/ is packed as it is.
#   - The packer writes mmap_generate_assets.h (file count, checksum) into ${CMAKE_BINARY_DIR}, so each build dir
#     compiles its own image's checksum (G3; check_apps built --dw-build compares them).
#   - ${CMAKE_BINARY_DIR}/dw_art.txt says which set was packed: "generic" or "overlay <short sha>" (check_apps built
#     --publishable needs "generic"); in an overlay build it is refreshed on every build.
get_filename_component(dw_assets_src "${CMAKE_CURRENT_SOURCE_DIR}/../assets" REALPATH)
get_filename_component(dw_repo "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." REALPATH)
get_filename_component(dw_bin "${CMAKE_BINARY_DIR}" REALPATH)
get_filename_component(dw_generic_bin "${CMAKE_SOURCE_DIR}/build" REALPATH)
if(DEFINED ENV{DW_ART_OVERLAY})
    message(FATAL_ERROR "DW_ART_OVERLAY is set in the environment: it is a CMake cache variable only "
                        "(-D DW_ART_OVERLAY=<clone> on a build-private configure). Unset it.")
endif()
set(DW_ART_OVERLAY "" CACHE PATH "The private art overlay's clone (a private build dir only); empty: the generic art")
# A stale header beside this file (esp_mmap_assets' default location before G3, e.g. from building an older commit
# here) would shadow the build dir's: a quoted include finds the source file's own directory first. Refuse it (M5).
# The build-time copy refuses it again, and dw_theme.c requires the copy's DW_MMAP_PER_BUILD_DIR.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mmap_generate_assets.h")
    message(FATAL_ERROR "A stale ${CMAKE_CURRENT_SOURCE_DIR}/mmap_generate_assets.h exists. It would shadow this build "
                        "dir's assets header and compile another image's checksum into the app (G3). Delete it.")
endif()
file(GLOB dw_asset_files CONFIGURE_DEPENDS "${dw_assets_src}/*")
set(dw_names_dir "${CMAKE_BINARY_DIR}/dw_names")
set(dw_names_h "${dw_names_dir}/dw_names_overlay.h")
set(dw_stage_outputs "")
if(DW_ART_OVERLAY)
    get_filename_component(dw_art "${DW_ART_OVERLAY}" REALPATH)
    if(dw_bin STREQUAL dw_generic_bin)
        message(FATAL_ERROR "DW_ART_OVERLAY is refused for ${dw_generic_bin}: the default build is the generic one. "
                            "Build the private art in its own dir (-B firmware/apps/dw/build-private -D SDKCONFIG=<abs>); "
                            "here, drop it with -U DW_ART_OVERLAY or delete the build dir.")
    endif()
    string(FIND "${dw_art}/" "${dw_repo}/" dw_inside)
    if(dw_inside EQUAL 0)
        message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art} is inside this repository: the overlay is never part of it")
    endif()
    execute_process(COMMAND git -C "${dw_art}" rev-parse --path-format=absolute --git-common-dir
                    OUTPUT_VARIABLE dw_art_git OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND git -C "${dw_repo}" rev-parse --path-format=absolute --git-common-dir
                    OUTPUT_VARIABLE dw_repo_git OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(dw_art_git AND dw_art_git STREQUAL dw_repo_git)
        message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art} is a checkout of this repository, not the overlay")
    endif()
    # Nor under any checkout of this repository (the main checkout, a sibling worktree) or its git common dir (M4).
    execute_process(COMMAND git -C "${dw_repo}" worktree list --porcelain OUTPUT_VARIABLE dw_wts ERROR_QUIET)
    string(REGEX MATCHALL "worktree [^\n]+" dw_wts "${dw_wts}")
    foreach(w ${dw_wts} "worktree ${dw_repo_git}")
        string(REGEX REPLACE "^worktree " "" w "${w}")
        if(w AND EXISTS "${w}")
            get_filename_component(w "${w}" REALPATH)
            string(FIND "${dw_art}/" "${w}/" dw_inside)
            if(dw_inside EQUAL 0)
                message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art} is under ${w}, a checkout of this repository")
            endif()
        endif()
    endforeach()
    if(NOT EXISTS "${dw_art}/overlay.json")
        message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art} has no overlay.json")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -DMODE=validate "-DART=${dw_art}"
                            -P "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                    RESULT_VARIABLE dw_valid ERROR_VARIABLE dw_valid_err)
    if(NOT dw_valid EQUAL 0)
        message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art}: its overlay.json is refused:\n${dw_valid_err}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dw_art}/overlay.json")
    # The overlay's display names (public release Task 9): dw_stage.cmake MODE=names writes dw_names_overlay.h from
    # overlay.json's "names", or deletes it when there are none. Whether the header exists after the run, not whether
    # this run wrote it, decides DW_NAMES_OVERLAY (preflight M16); dw_names.h then takes its names first.
    execute_process(COMMAND ${CMAKE_COMMAND} -DMODE=names "-DART=${dw_art}" "-DOUT=${dw_names_h}"
                            -P "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                    RESULT_VARIABLE dw_names_rc ERROR_VARIABLE dw_names_err)
    if(NOT dw_names_rc EQUAL 0)
        message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art}: its names are refused:\n${dw_names_err}")
    endif()
    if(EXISTS "${dw_names_h}")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE DW_NAMES_OVERLAY)
        target_include_directories(${COMPONENT_LIB} PRIVATE "${dw_names_dir}")
    endif()
    file(GLOB dw_overlay_files CONFIGURE_DEPENDS "${dw_art}/overlay.json" "${dw_art}/assets/*")
    set(dw_stage "${CMAKE_BINARY_DIR}/dw_assets/assets")
    foreach(f ${dw_asset_files})
        get_filename_component(n "${f}" NAME)
        list(APPEND dw_stage_outputs "${dw_stage}/${n}")
    endforeach()
    add_custom_command(OUTPUT ${dw_stage_outputs}
                       COMMAND ${CMAKE_COMMAND} -DMODE=stage "-DSRC=${dw_assets_src}" "-DART=${dw_art}"
                               "-DSTAGE=${dw_stage}" -P "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                       DEPENDS ${dw_asset_files} ${dw_overlay_files} "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                       COMMENT "Staging the dw assets with the art overlay's files"
                       VERBATIM)
    set(dw_pack_dir "${dw_stage}")
    # dw_art.txt names the overlay's commit: refreshed on every build (M6), rewritten only when it changes.
    add_custom_target(dw_art ALL
                      COMMAND ${CMAKE_COMMAND} -DMODE=art "-DART=${dw_art}" "-DART_TXT=${CMAKE_BINARY_DIR}/dw_art.txt"
                              -P "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                      BYPRODUCTS "${CMAKE_BINARY_DIR}/dw_art.txt"
                      COMMENT "The art overlay's commit -> dw_art.txt"
                      VERBATIM)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE DW_ART_OVERLAY_BUILD)   # `dw verify`: never the generic table alone
    if(EXISTS "${dw_art}/verify_crc_overlay.h")   # the overlay set's supplement, once it has it (G9, R147 PR3)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE DW_ART_OVERLAY_CRC)
        target_include_directories(${COMPONENT_LIB} PRIVATE "${dw_art}")
    endif()
else()
    file(REMOVE "${dw_names_h}")   # no overlay: no names header from an earlier configure survives (M16)
    file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/dw_art.txt" CONTENT "generic\n")
    set(dw_pack_dir "${dw_assets_src}")
endif()
spiffs_create_partition_assets(assets "${dw_pack_dir}" FLASH_IN_PROJECT MMAP_FILE_SUPPORT_FORMAT ".bin,.pcm,.dat"
                               MMAP_FILE_ALIGNMENT 64 IMPORT_INC_PATH "${CMAKE_BINARY_DIR}" DEPENDS ${dw_stage_outputs})
# The packer writes the header as a side effect ninja does not know about, so a source that includes it straight from
# there is not recompiled when the image changes (its old checksum stays compiled in; check_apps built --dw-build
# catches that). dw_theme.c includes a copy declared as a build output instead, made after each new image.
set(dw_mmap_dir "${CMAKE_BINARY_DIR}/dw_mmap")
add_custom_command(OUTPUT "${dw_mmap_dir}/mmap_generate_assets.h"
                   COMMAND ${CMAKE_COMMAND} -E make_directory "${dw_mmap_dir}"
                   COMMAND ${CMAKE_COMMAND} -DMODE=mmap "-DIN=${CMAKE_BINARY_DIR}/mmap_generate_assets.h"
                           "-DOUT=${dw_mmap_dir}/mmap_generate_assets.h"
                           "-DSTALE=${CMAKE_CURRENT_SOURCE_DIR}/mmap_generate_assets.h"
                           -P "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                   DEPENDS "${CMAKE_BINARY_DIR}/mmap_build/assets/assets/assets.bin"
                           "${CMAKE_CURRENT_SOURCE_DIR}/dw_stage.cmake"
                   COMMENT "The dw assets image's header (file count, checksum) for this build dir"
                   VERBATIM)
target_sources(${COMPONENT_LIB} PRIVATE "${dw_mmap_dir}/mmap_generate_assets.h")
target_include_directories(${COMPONENT_LIB} PRIVATE "${dw_mmap_dir}")
add_dependencies(${COMPONENT_LIB} assets_assets_bin)
