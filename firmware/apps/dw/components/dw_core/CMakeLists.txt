# dw_core: the dw theme's pure logic (preferences, face ids, the lift rule, WD6's boot decision, the numerals,
# the doors, the compositor and the polar annulus; Task 11: the clock, the face registry and stills, the hands and
# face 1; Task 13: faces 4 and 5; Task 14: faces 7 and 11; Task 15: faces 14 and 16 and the numeral key; wave 2 Task 5:
# face 2; Task 6: face 10; Task 7: face 13; Task 9: face 9; Task 10: face 6; Task 11: face 3; Task 12: face 8; Task 13: the Vortex tunnel spike, vortex.c). Pure as omni_core is: it builds under the host's cc -std=c99 -Wall -Wextra
# -Werror, with no esp_*, freertos or LVGL. rd wave 1 Task 1: the generic pure part (the compositor, the annulus, the
# clock, the shapes, the face interface, the column renderer, the reveal state machine, the chime scheduler and the
# offer rules) moved to face_kit (firmware/facekit/face_kit, fk/*.h), which dw_core requires. Every .c is in
# DW_CORE_SRCS; the host tests build the same files (firmware/test/host/CMakeLists.txt, a glob of this directory).
set(DW_CORE_SRCS "prefs.c" "circles.c" "doors.c" "chime.c"
                 "dwclock.c" "faces.c" "hands.c" "crystal.c"
                 "roundel.c" "callbox.c" "rotor.c" "coral.c" "gface.c" "gkey.c" "glearn.c"
                 "room.c" "amber.c" "fob.c" "console.c" "wall.c" "doorway.c" "broadcast.c" "vortex.c")
# Every source: no fused multiply-add contraction, so the device builds the bytes the host tests pin. Listed on its own
# (later tasks extend both lists), and the two must name the same files (R147 PR13): a source missing here would build
# with contraction and drift from the host's frames.
set(DW_CORE_NO_CONTRACT prefs.c circles.c doors.c chime.c dwclock.c faces.c hands.c crystal.c
                        roundel.c callbox.c rotor.c coral.c gface.c gkey.c glearn.c
                        room.c amber.c fob.c console.c wall.c doorway.c broadcast.c vortex.c)
set(_srcs ${DW_CORE_SRCS})
set(_nc ${DW_CORE_NO_CONTRACT})
list(SORT _srcs)
list(SORT _nc)
if(NOT _srcs STREQUAL _nc)
    message(FATAL_ERROR "dw_core: SRCS (${_srcs}) and the -ffp-contract=off list (${_nc}) differ")
endif()
file(GLOB _on_disk RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*.c)
# hidden files are no sources (wave 2 Task 6, the Task 5 review's M1): an editor's lock link (.#x.c), macOS's ._x.c
list(FILTER _on_disk EXCLUDE REGEX "^[.]")
list(SORT _on_disk)
if(NOT _srcs STREQUAL _on_disk)
    message(FATAL_ERROR "dw_core: SRCS (${_srcs}) is not every .c in the component (${_on_disk})")
endif()
idf_component_register(SRCS ${DW_CORE_SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES omni_core face_kit)
set_source_files_properties(${DW_CORE_NO_CONTRACT} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
# The owner's doors A/B (CONFIG_DW_DOORS_MOCKUP, main/Kconfig.projbuild): doors.h's timeline from Designer A's mockup.
# PUBLIC, so every component that reads doors.h's constants sees the same timeline.
if(CONFIG_DW_DOORS_MOCKUP)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC DW_DOORS_MOCKUP=1)
endif()
