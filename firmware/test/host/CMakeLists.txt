cmake_minimum_required(VERSION 3.16)
project(omni_host_tests C)

# The host suite builds at -O2 by default (the slow rd face harness drops from minutes to about a minute). RelWithDebInfo
# without NDEBUG: iris.c and the tests rely on assert, which -DNDEBUG would remove. A caller's own CMAKE_BUILD_TYPE or
# sanitizer CMAKE_C_FLAGS still applies.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "build type" FORCE)
endif()
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g" CACHE STRING "no NDEBUG: asserts stay" FORCE)

if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH not set: source ~/esp/v5.5.6/esp-idf/export.sh (Unity is taken from ESP-IDF)")
endif()

set(UNITY_DIR $ENV{IDF_PATH}/components/unity/unity/src)
set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../components/omni_core)

add_compile_options(-std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L)

# Unity itself is third-party and not warning-clean under -Wextra -Werror.
add_library(unity STATIC ${UNITY_DIR}/unity.c)
target_include_directories(unity PUBLIC ${UNITY_DIR})
target_compile_options(unity PRIVATE -w)

file(GLOB CORE_SRCS CONFIGURE_DEPENDS ${CORE_DIR}/*.c)
add_library(omni_core STATIC ${CORE_SRCS})
target_include_directories(omni_core PUBLIC ${CORE_DIR}/include)
target_link_libraries(omni_core PUBLIC m)
# At -O2 under the sanitizers, GCC cannot see omni_fmt_dur's clamp (m <= 99999) through the int casts and warns that
# "%dh %02dm" may truncate into 12 bytes (-Wformat-truncation). The firmware source is right and its device builds are
# clean; the warning is the host build's alone, so it is silenced here, not fixed in firmware.
target_compile_options(omni_core PRIVATE -Wno-format-truncation)

# face_kit (rd wave 1 Task 1): the faces' shared pure code, built from the same top-level files as the device component
# (firmware/facekit/face_kit; its ui/ subdirectory is LVGL code, never built here), linked by dw_core.
set(FK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../facekit/face_kit)
file(GLOB FK_SRCS CONFIGURE_DEPENDS ${FK_DIR}/*.c)
list(FILTER FK_SRCS EXCLUDE REGEX "/[.][^/]*$")   # hidden files are no sources (as dw_core's below)
add_library(face_kit STATIC ${FK_SRCS})
target_include_directories(face_kit PUBLIC ${FK_DIR}/include)
target_compile_options(face_kit PRIVATE -ffp-contract=off)
target_link_libraries(face_kit PUBLIC omni_core)
# FK_TEST_HOOKS (rd wave 1 Task 9, PR18): the host library's glyph recording in fk_atlas_text* (fk/compose.h), which a
# theme's harness reads for its text radius and atlas count; no device build defines it (a theme's name inside face_kit
# would break its neutrality, so the switch is face_kit's own)
target_compile_definitions(face_kit PUBLIC FK_TEST_HOOKS=1)

# dw_core (dw wave 1, Task 3): the dw theme's pure logic, built from the same files as the device
# component (firmware/apps/dw/components/dw_core) and linked into every test_*.
set(DW_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/dw/components/dw_core)
file(GLOB DW_CORE_SRCS CONFIGURE_DEPENDS ${DW_CORE_DIR}/*.c)
# hidden files are no sources (wave 2 Task 6, the Task 5 review's M1): an editor's lock link (.#x.c), macOS's ._x.c;
# matched on the file's name, not its path (a hidden directory above the tree must not empty the list)
list(FILTER DW_CORE_SRCS EXCLUDE REGEX "/[.][^/]*$")
add_library(dw_core STATIC ${DW_CORE_SRCS})
target_include_directories(dw_core PUBLIC ${DW_CORE_DIR}/include)
target_compile_options(dw_core PRIVATE -ffp-contract=off)
target_link_libraries(dw_core PUBLIC face_kit omni_core)
# DW_TEST_HOOKS (dw wave 2 Task 2): the host build's test hooks in dw_core (circles.c's numeral-draw counter), which
# the firmware never defines
target_compile_definitions(dw_core PUBLIC DW_TEST_HOOKS=1)
# rd_core (rd wave 1 Task 4): the Mining Ship theme's pure logic, built from the same files as the device component
# (firmware/apps/rd/components/rd_core), with no fused multiply-add contraction as there; linked only into test_rd_*.
set(RD_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/rd/components/rd_core)
file(GLOB RD_CORE_SRCS CONFIGURE_DEPENDS ${RD_CORE_DIR}/*.c)
list(FILTER RD_CORE_SRCS EXCLUDE REGEX "/[.][^/]*$")   # hidden files are no sources (as dw_core's)
add_library(rd_core STATIC ${RD_CORE_SRCS})
target_include_directories(rd_core PUBLIC ${RD_CORE_DIR}/include)
target_compile_options(rd_core PRIVATE -ffp-contract=off)
target_link_libraries(rd_core PUBLIC face_kit omni_core)
# RD_TEST_HOOKS (rd wave 2 Task 12): the host build's test hooks in rd_core (curry.c's rd_cu_steam_ref, through which
# test_rd_faces draws wave 1's procedural steam, rd_cu_steam_oracle.c, in the face's place), which the firmware never
# defines
target_compile_definitions(rd_core PUBLIC RD_TEST_HOOKS=1)

enable_testing()
file(GLOB TEST_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)
foreach(src ${TEST_SRCS})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE unity omni_core dw_core)
    add_test(NAME ${name} COMMAND ${name})
endforeach()

target_compile_definitions(test_power PRIVATE TRACE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/traces")
# test_pmap_file compares the committed dial map asset (R80) with the map built here
target_compile_definitions(test_pmap_file PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
target_compile_definitions(test_omni_dial PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
# test_kb reads the committed keyboard layout asset (tools/kb_assets.py layout; the shared source is watch_ui/assets)
target_compile_definitions(test_kb PRIVATE KB_LAY_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../components/watch_ui/assets/kb_lay.bin")
# test_fontasset (rim keyboard Task 8) builds watch_ui's fontasset.c against stub_lvgl/lvgl.h (LVGL 9.5's font structs)
# and reads the committed OFA1 fonts
target_sources(test_fontasset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/watch_ui/fontasset.c)
target_include_directories(test_fontasset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub_lvgl
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../components/watch_ui/include)
target_compile_definitions(test_fontasset PRIVATE FONTASSET_HOST=1
                           KB_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../components/watch_ui/assets")
# test_tmr_lut_file compares the committed timer ring LUT asset (R139) with the LUT built here
target_compile_definitions(test_tmr_lut_file PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
# test_lid composites the shipped lid bitmap; test_lid_mockup is the same tests' timeline half on the mockup's
# timeline (OMNI_FLIP_MOCKUP, the owner's A/B at Task 14): its own lid.c and omni_model.c, built with the switch
target_compile_definitions(test_lid PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
# test_iris reads the shipped grain (omni_blade.bin)
target_compile_definitions(test_iris PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
# test_brassbg checks the committed backgrounds (bg_*.bin, R112) against the device's CRCs
target_compile_definitions(test_brassbg PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
add_executable(test_lid_mockup ${CMAKE_CURRENT_SOURCE_DIR}/test_lid.c ${CORE_DIR}/lid.c ${CORE_DIR}/omni_model.c)
target_compile_definitions(test_lid_mockup PRIVATE OMNI_FLIP_MOCKUP=1 ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../apps/omni/assets")
target_link_libraries(test_lid_mockup PRIVATE unity omni_core dw_core)
add_test(NAME test_lid_mockup COMMAND test_lid_mockup)
# test_dw_doors_mockup (dw wave 1 Task 5, A7): the same doors tests on Designer A's 2.3 s timeline (DW_DOORS_MOCKUP,
# the owner's A/B): its own doors.c, built with the switch, which resolves before dw_core's copy in the static lib
add_executable(test_dw_doors_mockup ${CMAKE_CURRENT_SOURCE_DIR}/test_dw_doors.c ${DW_CORE_DIR}/doors.c)
target_compile_definitions(test_dw_doors_mockup PRIVATE DW_DOORS_MOCKUP=1)
target_compile_options(test_dw_doors_mockup PRIVATE -ffp-contract=off)
target_link_libraries(test_dw_doors_mockup PRIVATE unity omni_core dw_core face_kit)
add_test(NAME test_dw_doors_mockup COMMAND test_dw_doors_mockup)
# test_dw_doors_libm (wave 2 Task 11): doors.c and face_kit's cols.c (rd wave 1 Task 1, PR2: the column renderer's
# trig moved there) with sinf and cosf off (doors_libm_perturb.c, resolving before dw_core's and face_kit's copies):
# the kept leaves' still geometry must not move (newlib and glibc may differ by an ulp)
add_executable(test_dw_doors_libm ${CMAKE_CURRENT_SOURCE_DIR}/test_dw_doors.c ${CMAKE_CURRENT_SOURCE_DIR}/doors_libm_perturb.c)
target_compile_definitions(test_dw_doors_libm PRIVATE DW_DOORS_LIBM_PERTURB=1)
target_include_directories(test_dw_doors_libm PRIVATE ${DW_CORE_DIR} ${FK_DIR})
target_compile_options(test_dw_doors_libm PRIVATE -ffp-contract=off)
target_link_libraries(test_dw_doors_libm PRIVATE unity omni_core dw_core face_kit)
add_test(NAME test_dw_doors_libm COMMAND test_dw_doors_libm)
# The dw faces (dw wave 1 Task 11): the face harness over the committed generic assets (DW_ASSETS_DIR), its
# still-frame CRCs against dw/verify_crc.h, and the host tool dw_shot (a frame as PPM, or --crc: the CRC header).
# DW_ART_OVERLAY (a cache variable: the private art overlay's clone, never named in this repo) runs the harness a
# second time over the staged private set (the overlay's assets/ over the generic ones) against its
# verify_crc_overlay.h, then dw/verify_crc.h (R147 PR3, wave 2 Task 4: the overlay's table supplements the generic one
# with rows for the faces whose bind reads a swap asset, and only those), and the doors' closed and band-split cases
# over both sets' real dw_doors_m.bin. Without it, the overlay variants report skipped (77), never passed (A7).
set(DW_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/dw/assets)
set(DW_ART_OVERLAY "" CACHE PATH "the private art overlay's clone (optional): the overlay variants of the dw tests")
add_library(dw_assets_host STATIC ${CMAKE_CURRENT_SOURCE_DIR}/dw_assets_host.c)
# fk_test_util (dw wave 2 Task 2, R147 PR10): the face harness's frame, instant, lit scan, orbit and tap-edge helpers
add_library(fk_test_util STATIC ${CMAKE_CURRENT_SOURCE_DIR}/fk_test_util.c)
target_link_libraries(fk_test_util PUBLIC unity dw_core)
target_compile_definitions(test_dw_faces PRIVATE DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_faces PRIVATE dw_assets_host fk_test_util)
add_executable(test_dw_faces_overlay ${CMAKE_CURRENT_SOURCE_DIR}/test_dw_faces.c)
target_compile_definitions(test_dw_faces_overlay PRIVATE DW_FACES_OVERLAY=1 DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_faces_overlay PRIVATE unity omni_core dw_core dw_assets_host fk_test_util)
add_test(NAME test_dw_faces_overlay COMMAND test_dw_faces_overlay)
# test_dw_faces_mockup (wave 2 Task 11, R147 PR11): #3's doors seams on Designer A's 2.3 s timeline (DW_DOORS_MOCKUP):
# its own doors.c and doorway.c, built with the switch, resolve before dw_core's copies in the static lib
add_executable(test_dw_faces_mockup ${CMAKE_CURRENT_SOURCE_DIR}/test_dw_faces.c ${DW_CORE_DIR}/doors.c
               ${DW_CORE_DIR}/doorway.c)
target_compile_definitions(test_dw_faces_mockup PRIVATE DW_DOORS_MOCKUP=1 DW_FACES_MOCKUP=1
                           DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_compile_options(test_dw_faces_mockup PRIVATE -ffp-contract=off)
target_link_libraries(test_dw_faces_mockup PRIVATE unity omni_core dw_core dw_assets_host fk_test_util)
add_test(NAME test_dw_faces_mockup COMMAND test_dw_faces_mockup)
# The full-day glance sweep (test_face_glance_day_<key>) samples every DW_GLANCE_SWEEP_STEP minutes plus every :59;
# the sanitizer configuration sets the step here (never left to the caller's environment), the plain one runs every
# minute
if(CMAKE_C_FLAGS MATCHES "-fsanitize=[^ ]*address")
    set_tests_properties(test_dw_faces test_dw_faces_overlay PROPERTIES ENVIRONMENT "DW_GLANCE_SWEEP_STEP=5")
else()
    set_tests_properties(test_dw_faces test_dw_faces_overlay PROPERTIES ENVIRONMENT "DW_GLANCE_SWEEP_STEP=1")
endif()
# test_dw_vortex (wave 2 Task 13): the Vortex tunnel over the committed generated table and texture (tools/dw_vortex.py)
target_compile_definitions(test_dw_vortex PRIVATE DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_vortex PRIVATE dw_assets_host)
# test_dw_chime: the envelope bound to the committed chime
target_compile_definitions(test_dw_chime PRIVATE DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_chime PRIVATE dw_assets_host)
# test_dw_reveal (rd wave 1 Task 2): the doors as face_kit's reveal (DW_REVEAL_DOORS), its golden over both committed
# door images (the fixture's (OUT, dw_doors_c) rows go through room's asset), the faces' reveal_ops and the chime plan
target_compile_definitions(test_dw_reveal PRIVATE DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_reveal PRIVATE dw_assets_host)
add_executable(test_dw_doors_real ${CMAKE_CURRENT_SOURCE_DIR}/test_dw_doors.c)
target_compile_definitions(test_dw_doors_real PRIVATE DW_DOORS_REAL=1 DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_doors_real PRIVATE unity omni_core dw_core dw_assets_host)
add_test(NAME test_dw_doors_real COMMAND test_dw_doors_real)
add_executable(test_dw_doors_real_overlay ${CMAKE_CURRENT_SOURCE_DIR}/test_dw_doors.c)
target_compile_definitions(test_dw_doors_real_overlay PRIVATE DW_DOORS_REAL=1 DW_DOORS_REAL_OVERLAY=1
                           DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(test_dw_doors_real_overlay PRIVATE unity omni_core dw_core dw_assets_host)
add_test(NAME test_dw_doors_real_overlay COMMAND test_dw_doors_real_overlay)
if(DW_ART_OVERLAY)
    get_filename_component(dw_art "${DW_ART_OVERLAY}" REALPATH)
    if(NOT EXISTS "${dw_art}/overlay.json")
        message(FATAL_ERROR "DW_ART_OVERLAY ${dw_art} has no overlay.json")
    endif()
    target_compile_definitions(test_dw_doors_real_overlay PRIVATE DW_ART_DIR="${dw_art}")
    if(EXISTS "${dw_art}/verify_crc_overlay.h")   # until it exists, the faces' overlay run fails, never passes
        target_compile_definitions(test_dw_faces_overlay PRIVATE DW_ART_DIR="${dw_art}")
        target_include_directories(test_dw_faces_overlay PRIVATE "${dw_art}")
    else()
        message(WARNING "DW_ART_OVERLAY has no verify_crc_overlay.h yet: test_dw_faces_overlay fails until dw_shot "
                        "--crc writes it")
        target_compile_definitions(test_dw_faces_overlay PRIVATE DW_ART_DIR="${dw_art}" DW_NO_OVERLAY_CRC=1)
    endif()
endif()
set_tests_properties(test_dw_faces_overlay test_dw_doors_real_overlay PROPERTIES SKIP_RETURN_CODE 77)
add_executable(dw_shot ${CMAKE_CURRENT_SOURCE_DIR}/dw_shot.c)
target_compile_definitions(dw_shot PRIVATE DW_ASSETS_DIR="${DW_ASSETS_DIR}")
target_link_libraries(dw_shot PRIVATE omni_core dw_core dw_assets_host)
# test_records compares the shared/local records with the golden bytes in fixtures/*.hex
target_compile_definitions(test_records PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# The in-memory kv (kv_mem.c: fault injection, the line format), shared by test_migrate, migrate_host and Task 6's
# test_boot. test_migrate reads the golden fixture's value files in nvs/.
add_library(kv_mem STATIC ${CMAKE_CURRENT_SOURCE_DIR}/kv_mem.c)
target_link_libraries(kv_mem PUBLIC omni_core)
target_link_libraries(test_migrate PRIVATE kv_mem)
target_compile_definitions(test_migrate PRIVATE NVS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/nvs")
# test_boot: the boot and save policy (omni/boot.h, Stage 4b Task 6) over the same kv and fixture values
target_link_libraries(test_boot PRIVATE kv_mem)
target_compile_definitions(test_boot PRIVATE NVS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/nvs"
                           FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# test_dw_prefs: the dw extension over the same boot policy and kv (the stub's record, read-only and no-write boots)
target_link_libraries(test_dw_prefs PRIVATE kv_mem)
# test_rd_prefs: rd's extension and face table, and the first rd boot over a synthetic image (nvs/'s values through the
# Omni's and dw's boots; PR6) against fixtures/nvs_rd_first_boot.txt
target_link_libraries(test_rd_prefs PRIVATE rd_core kv_mem)
target_compile_definitions(test_rd_prefs PRIVATE NVS_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/nvs"
                           FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# rd_assets_host (rd wave 1 Task 7): the committed rd assets (firmware/apps/rd/assets) read from disk, dw_assets_host's
# shape without an overlay (rd has none). test_rd_atlas2: the two-plane atlases (PR29) over the committed rd_f_vt28.bin.
set(RD_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/rd/assets)
add_library(rd_assets_host STATIC ${CMAKE_CURRENT_SOURCE_DIR}/rd_assets_host.c)
target_include_directories(rd_assets_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_rd_atlas2 PRIVATE rd_core rd_assets_host)
target_compile_definitions(test_rd_atlas2 PRIVATE RD_ASSETS_DIR="${RD_ASSETS_DIR}")
# rd wave 1 Task 9: the face harness (test_rd_faces: every RD_FACES row on the lessons, the per-face void floors, the
# still CRCs against rd/verify_crc.h) over the committed rd assets; the time-asleep rules (test_rd_asleep); the host
# tool rd_shot (a frame as PPM, --crc writes rd/verify_crc.h, --glow-lut rd_core's glow_lut.h, --void PNG measures a
# mockup shot by the harness's disc rule: zlib reads the PNG); and test_rd_core_no_libm (lesson 6: no libm symbol in
# nm -u of rd_core's objects, chime_env.o excepted, RW8).
# The harness is slow, so it is three binaries from one source (RD_FACES_PART): test_rd_faces (3: everything but the
# dirty_* and glance_day rows), test_rd_faces_dirty (1) and test_rd_faces_glance (2), which ctest -j runs in parallel.
add_executable(test_rd_faces_dirty ${CMAKE_CURRENT_SOURCE_DIR}/test_rd_faces.c)
add_executable(test_rd_faces_glance ${CMAKE_CURRENT_SOURCE_DIR}/test_rd_faces.c)
set(RD_FACES_PARTS test_rd_faces:3 test_rd_faces_dirty:1 test_rd_faces_glance:2)
foreach(pt ${RD_FACES_PARTS})
    string(REPLACE ":" ";" pt ${pt})
    list(GET pt 0 tgt)
    list(GET pt 1 part)
    if(NOT tgt STREQUAL "test_rd_faces")
        target_link_libraries(${tgt} PRIVATE unity omni_core dw_core)
        add_test(NAME ${tgt} COMMAND ${tgt})
    endif()
    target_link_libraries(${tgt} PRIVATE rd_core rd_assets_host fk_test_util)
    # rd wave 2 Task 12 (R165/I5): the host-only oracle of Curry Clock's wave 1 steam (BASE's puffs_of and steam_draw)
    target_sources(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rd_cu_steam_oracle.c)
    target_compile_definitions(${tgt} PRIVATE RD_FACES_PART=${part} RD_ASSETS_DIR="${RD_ASSETS_DIR}"
                               RD_LAYERS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/export/rd_layers.json")
endforeach()
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/rd_cu_steam_oracle.c PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
target_link_libraries(test_rd_asleep PRIVATE rd_core)
find_package(ZLIB REQUIRED)
# test_rd_release (rd wave 1 Task 10): Stasis Release, RD_REVEAL_STASIS from rd_core alone (PR12), driven as fk_view
# drives it over the Stasis Clock and the committed rd assets; the lid's ramp checked against the layer table's row
target_link_libraries(test_rd_release PRIVATE rd_core rd_assets_host)
target_compile_definitions(test_rd_release PRIVATE RD_ASSETS_DIR="${RD_ASSETS_DIR}"
                           RD_LAYERS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/export/rd_layers.json")
# test_rd_airlock (rd wave 2 Task 4): the airlock, RD_REVEAL_AIRLOCK from rd_core alone, driven as fk_view drives it over
# Bay Mouth and the committed rd assets; the doors' ramp checked against the layer table's row (libm: the test's own
# double-precision projection only)
target_link_libraries(test_rd_airlock PRIVATE rd_core rd_assets_host m)
target_compile_definitions(test_rd_airlock PRIVATE RD_ASSETS_DIR="${RD_ASSETS_DIR}"
                           RD_LAYERS_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/export/rd_layers.json")
# test_rd_pass (rd wave 2 Task 5): the hull pass, RD_REVEAL_PASS from rd_core alone, driven as fk_view drives it over
# Drift Plate (which draws the pass itself, R166) and the committed rd assets (libm: the test's own rounding only)
target_link_libraries(test_rd_pass PRIVATE rd_core rd_assets_host m)
target_compile_definitions(test_rd_pass PRIVATE RD_ASSETS_DIR="${RD_ASSETS_DIR}")
add_executable(rd_shot ${CMAKE_CURRENT_SOURCE_DIR}/rd_shot.c)
target_compile_definitions(rd_shot PRIVATE RD_ASSETS_DIR="${RD_ASSETS_DIR}")
target_link_libraries(rd_shot PRIVATE rd_core omni_core rd_assets_host ZLIB::ZLIB)
add_test(NAME test_rd_core_no_libm
         COMMAND ${CMAKE_COMMAND} "-DNM=${CMAKE_NM}" "-DOBJS=$<JOIN:$<TARGET_OBJECTS:rd_core>,|>"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/rd_no_libm.cmake)
# rd wave 1 Task 14: Bunk Clock's object names no settings, store or NVS symbol (lesson 8: the count from the clock, the
# taps and the pause in RAM)
add_test(NAME test_rd_bk_no_nvs
         COMMAND ${CMAKE_COMMAND} "-DNM=${CMAKE_NM}" "-DOBJS=$<JOIN:$<TARGET_OBJECTS:rd_core>,|>"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/rd_bk_no_nvs.cmake)
# test_rd_cu_no_nvs (Task 15): the same on curry.c (its chillies are fk_face's RAM variant: never NVS)
add_test(NAME test_rd_cu_no_nvs
         COMMAND ${CMAKE_COMMAND} "-DNM=${CMAKE_NM}" -DSRC=curry "-DOBJS=$<JOIN:$<TARGET_OBJECTS:rd_core>,|>"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/rd_bk_no_nvs.cmake)
# the full-day glance sweep (test_rd_face_glance_day_<key>): every RD_GLANCE_SWEEP_STEP minutes plus every :59; the
# sanitizer configuration sets 5 here (never the caller's environment), the plain one every minute
if(CMAKE_C_FLAGS MATCHES "-fsanitize=[^ ]*address")
    set_tests_properties(test_rd_faces test_rd_faces_dirty test_rd_faces_glance PROPERTIES ENVIRONMENT "RD_GLANCE_SWEEP_STEP=5")
else()
    set_tests_properties(test_rd_faces test_rd_faces_dirty test_rd_faces_glance PROPERTIES ENVIRONMENT "RD_GLANCE_SWEEP_STEP=1")
endif()
# test_rd_chime (rd wave 1 Task 8): rd's chime picture (rd_chime_env) and plan (rd_chime_plan); the scheduler is
# test_fk_chime's
target_link_libraries(test_rd_chime PRIVATE rd_core)
# test_rd_face_ids: the rd face ids, keys and default carousel pinned (a theme's test links its own core only; the dw
# ids are test_dw_face_ids)
target_link_libraries(test_rd_face_ids PRIVATE rd_core)
# migrate_host: the migration over migrate_host's line format, a host tool (not a test_*). test_nvs_image runs it over
# golden images built with nvs_partition_gen and parsed with nvs_parser (tools/nvs_fixture.py).
add_executable(migrate_host ${CMAKE_CURRENT_SOURCE_DIR}/migrate_host.c)
target_link_libraries(migrate_host PRIVATE kv_mem)
# `python` is the ESP-IDF environment's (export.sh): it carries esp_idf_nvs_partition_gen.
add_test(NAME test_nvs_image COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/nvs_fixture.py
         check --host $<TARGET_FILE:migrate_host>)
# test_nvs_image writes and rmtree's its work dir beside migrate_host: never alongside another test
# without .git (a download, not a clone) it skips (77): it compares `git status` before and after a run
# (final review m6)
set_tests_properties(test_nvs_image PROPERTIES RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
# The safe-flash tools (Stage 4b Task 3): flash_keep_nvs.py, nvs_diff.py, theme.sh, bootline.py, all against
# tools/fake_esptool.py (no serial port is opened). test_nvs_diff_rules builds its images with migrate_host.
add_test(NAME test_flash_tools COMMAND python -m unittest tools/test_flash_keep_nvs.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_flash_tools PROPERTIES ENVIRONMENT "MIGRATE_HOST=$<TARGET_FILE:migrate_host>;PYTHONDONTWRITEBYTECODE=1")
# The watch-port file's rules (tools/watchport.py: tools/watch.local, --port, --watch), on temp files and scratch
# repos only: no serial port is opened, and OMNI_WATCH_LOCAL always points at a temp file.
add_test(NAME test_watchport COMMAND python -m unittest tools/test_watchport.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_watchport PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
# The theme apps' drift and identity checks (Stage 4b Task 7, tools/check_apps.py). static needs no build;
# built needs both apps' default builds (idf.py -C firmware/apps/<omni|dw> build) and skips (77) without them.
add_test(NAME check_apps_static COMMAND python tools/check_apps.py static
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
# The dw theme's synthesised sounds (dw wave 1 Task 8, tools/gen_sfx_dw.py): lengths, peaks, band, determinism.
add_test(NAME test_gen_sfx_dw COMMAND python -m unittest tools/test_gen_sfx_dw.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_gen_sfx_dw PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
# The Mining Ship chime (rd wave 1 Task 8, tools/gen_sfx_rd.py): both voicings deterministic, 76 800 B, the peak, the
# audible voicing's band (PR9), the recipe's low thrum, the events' onsets on the stems (PR34), the committed bytes.
add_test(NAME test_gen_sfx_rd COMMAND python -m unittest tools/test_gen_sfx_rd.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_gen_sfx_rd PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" TIMEOUT 600)
add_test(NAME check_apps_built COMMAND python tools/check_apps.py built
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(check_apps_static check_apps_built PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
# static reads `git ls-files`: without .git (a download, not a clone) it skips (77; final review m6)
set_tests_properties(check_apps_static PROPERTIES SKIP_RETURN_CODE 77)
# rd wave 1 Task 5: the rd list's Python and JS twins agree on one corpus (tools/rd_terms.py, tools/export/rd_terms.mjs);
# without node it reports skipped (77), never passed (PR20). check_apps_static runs it too and goes on without node.
add_test(NAME check_apps_terms COMMAND python tools/check_apps.py terms
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(check_apps_terms PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
# Public release Task 5: the term lists live only in private modules (tools/dw_terms.py, tools/rd_terms.py), found
# through tools/terms.py; in a public tree (a PUBLIC_TREE marker) the term scans skip (check_apps terms exits 77), a
# stray marker never hides a module, and a private checkout missing one still fails. test_check_apps_public runs the
# copied tools in scratch repos (a public one, and a private one missing its lists), never in this checkout.
add_test(NAME test_terms COMMAND python -m unittest tools/test_terms.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_test(NAME test_check_apps_public COMMAND python -m unittest tools/test_check_apps_public.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_terms test_check_apps_public PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
set_tests_properties(test_check_apps_public PROPERTIES TIMEOUT 1800)
# Public release Task 14: the export (tools/make_public.sh, on scratch repos), the release guard
# (tools/release_check.py, invented words only) and the release list's parity with the Mining Ship list
# (tools/test_release_parity.py: skipped (77) without tools/rd_terms.py or a terms file; the terms file is
# $RELEASE_TERMS, else the overlay clone's release/terms.txt when DW_ART_OVERLAY names one).
add_test(NAME test_release_check COMMAND python -m unittest tools/test_release_check.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_test(NAME test_make_public COMMAND python -m unittest tools/test_make_public.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_test(NAME test_release_parity COMMAND python tools/test_release_parity.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(release_terms "$ENV{RELEASE_TERMS}")
if(NOT release_terms AND DW_ART_OVERLAY AND EXISTS "${DW_ART_OVERLAY}/release/terms.txt")
    set(release_terms "${DW_ART_OVERLAY}/release/terms.txt")
endif()
set_tests_properties(test_release_check test_make_public PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
set_tests_properties(test_release_parity PROPERTIES SKIP_RETURN_CODE 77
                     ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;RELEASE_TERMS=${release_terms}")
# The committed assets and generated fonts against tools/identity.sha256 (Stage 5 Task 5 m1; tools/assets.sh step 7)
add_test(NAME assets_identity COMMAND bash tools/assets.sh --identity
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(check_apps_built PROPERTIES SKIP_RETURN_CODE 77)
# The dw asset pipeline (dw wave 1 Task 9): dw_pack.py's self-test (since rd wave 1 Task 6 only dw's own rules:
# the gen rows' DAT tables and the drawn-text guard; the formats' tests are test_facepack's; standard library only).
# With node, tools/node_modules and Chrome, also dw_export.mjs's self-test (the drawn-text guard, the canvas tags) and
# dw_assets.sh --check against scratch copies (the re-render check, the namesake rule, the publishability guard;
# skipped without a previous full tools/dw_assets.sh run).
# The shared asset formats (rd wave 1 Task 6, tools/facepack.py): the PNG codec, the LVGL bins written and read back,
# the strips, the atlas builder and cell table, the name rule, the manifest, assets_meta.h, the budget, the round trip.
add_test(NAME test_facepack COMMAND python tools/facepack.py selftest
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_facepack PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
# The keyboard layout asset's generator (Task 5, tools/kb_assets.py): the sets, the read-back, the 95-character cover, the
# refusals, and the committed bytes (standard library only)
add_test(NAME test_kb_assets COMMAND python tools/kb_assets.py selftest
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_kb_assets PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
add_test(NAME test_dw_pack COMMAND python tools/dw_pack.py selftest
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
# The private build's overlay steps (public release Task 9, firmware/apps/dw/main/dw_stage.cmake): the display names
# (MODE=names), the thumbnails and the chime as swap files, on scratch overlays only (cmake alone, so unconditional)
add_test(NAME test_dw_stage COMMAND python -m unittest tools/test_dw_stage.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_dw_stage PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
# The Vortex generator's self-test (wave 2 Task 13, tools/dw_vortex.py): two runs byte-identical, the texture tileable,
# the table's header, padding and mask (standard library only)
add_test(NAME test_dw_vortex_gen COMMAND python tools/dw_vortex.py selftest
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_dw_vortex_gen PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
find_program(DW_NODE node)
set(DW_CHROME "$ENV{CHROME}")
if(NOT DW_CHROME)
    set(DW_CHROME /usr/bin/google-chrome)
endif()
if(DW_NODE AND EXISTS "${DW_CHROME}" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/node_modules/playwright-core")
    add_test(NAME test_dw_export COMMAND ${DW_NODE} tools/dw_export.mjs --selftest
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
    add_test(NAME test_dw_assets COMMAND python -m unittest tools/test_dw_assets.py
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
    set_tests_properties(test_dw_assets PROPERTIES TIMEOUT 600 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()
# The Mining Ship asset pipeline (rd wave 1 Task 7): rd_pack.py's self-test (standard library only), and
# tools/test_rd_assets.py: the table, the drawn-text guard, the committed atlases, the budget lines, build_atlas's
# advance override, the R159 scan rules and tools/rd_assets.sh --guard on scratch repos; its export cases skip without
# node, tools/node_modules, Chrome and tools/fonts/.
add_test(NAME test_rd_pack COMMAND python tools/rd_pack.py selftest
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_test(NAME test_rd_assets COMMAND python -m unittest tools/test_rd_assets.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set_tests_properties(test_rd_pack test_rd_assets PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
set_tests_properties(test_rd_assets PROPERTIES TIMEOUT 1800)
set_tests_properties(test_dw_pack PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

# the recogniser's host-only oracle (4ef03a7's gesture.c, renamed), linked into test_gesture alone
target_sources(test_gesture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gesture_oracle.c)
