# face_kit: the faces' shared pure code (rd wave 1 Task 1), moved out of the first theme's core so a second theme
# builds on the same compositor, shapes, column renderer, reveal state machine, chime scheduler, offer rules and face
# interface (include/fk/*.h), plus the row renderer and the text helpers (rows.c, text.c: rd wave 1 Task 3). Only the
# themes that use it list it (EXTRA_COMPONENT_DIRS ../../facekit); the Omni never compiles it. Pure as omni_core is: its
# top level builds under the host's cc -std=c99 -Wall -Wextra -Werror, with no esp_*, freertos or LVGL, and includes
# nothing from a theme. The host tests build the same files (firmware/test/host/CMakeLists.txt, a glob of this
# directory's top level).
set(FK_SRCS "compose.c" "annulus.c" "clock.c" "shapes.c" "face.c" "cols.c" "reveal_sm.c" "chime.c" "offer.c"
            "rows.c" "text.c" "ui/fk_view.c" "ui/fk_face.c" "ui/fk_chime_svc.c")
# Every top-level source: no fused multiply-add contraction, so the device builds the bytes the host tests pin. Listed
# on its own, and the two must name the same files: a source missing here would build with contraction and drift from
# the host's frames. (ui/*.c, from Task 2, are LVGL code in FK_SRCS only, compiled with contraction like an app's main;
# the checks below compare the top-level subset.)
set(FK_NO_CONTRACT compose.c annulus.c clock.c shapes.c face.c cols.c reveal_sm.c chime.c offer.c rows.c text.c)
set(_srcs ${FK_SRCS})
list(FILTER _srcs EXCLUDE REGEX "/")   # the top level only
set(_nc ${FK_NO_CONTRACT})
list(SORT _srcs)
list(SORT _nc)
if(NOT _srcs STREQUAL _nc)
    message(FATAL_ERROR "face_kit: SRCS (${_srcs}) and the -ffp-contract=off list (${_nc}) differ")
endif()
file(GLOB _on_disk RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*.c)
list(FILTER _on_disk EXCLUDE REGEX "^[.]")   # hidden files are no sources (an editor's lock link, macOS's ._x.c)
list(SORT _on_disk)
if(NOT _srcs STREQUAL _on_disk)
    message(FATAL_ERROR "face_kit: SRCS (${_srcs}) is not every top-level .c in the component (${_on_disk})")
endif()
# ui/ (rd wave 1 Task 2): the app side every theme shares, on the shell (watch_ui) and the store: the face canvas and
# the reveal driver on the direct path (fk_view.c), the face runtime (fk_face.c) and the chime service
# (fk_chime_svc.c). Its headers (ui/) are the themes' apps'; the host tests never build ui/.
idf_component_register(SRCS ${FK_SRCS}
                       INCLUDE_DIRS "include" "ui"
                       REQUIRES omni_core watch_ui store
                       PRIV_REQUIRES lvgl__lvgl espressif__esp_mmap_assets)
set_source_files_properties(${FK_NO_CONTRACT} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
