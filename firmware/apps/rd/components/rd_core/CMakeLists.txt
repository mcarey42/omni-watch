# rd_core: the Mining Ship theme's pure logic (rd wave 1 Task 4: t_rd/local's extension, the face ids, the default
# carousel, the lift rule and the registry of built faces; Task 7: the two-plane atlases; Task 8: the chime's picture
# envelope and plan, chime_env.c, the one file here allowed libm (expf, RW8); Task 9: the time-asleep rules, asleep.c,
# and Stasis Clock, stasis.c, with the stills, the glow tables and the stale marker in faces.c; Task 10: Stasis Release,
# release.c, with RD_REVEAL_STASIS and its bind (PR12); Task 11: Ship Terminal, terminal.c; Task 12: Deck Sign, deck.c;
# Task 13: Hull Plate, hull.c; Task 14: Bunk Clock, bunk.c; Task 15: Curry Clock, curry.c; wave 2 Task 3: Bay Mouth,
# bay.c; Task 4: the airlock, airlock.c, with RD_REVEAL_AIRLOCK; Task 5: Drift Plate, drift.c, and the hull pass,
# pass.c, with RD_REVEAL_PASS; Task 6: Drive Console, console.c, and the seven-segment digit, seg7.c; Task 7: Nav
# Scope, scope.c; Task 8: Vending Machine, vending.c; Task 9: the Bowl, bowl.c; Task 10: Mop Bot, mopbot.c; each face
# task adds its own file).
# Pure as omni_core is: it builds under the host's cc -std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L, with no
# esp_*, freertos or LVGL, and no allocation in a draw path. The host tests build the same files
# (firmware/test/host/CMakeLists.txt, a glob of this directory).
set(RD_CORE_SRCS "prefs.c" "faces.c" "atlas2.c" "chime_env.c" "asleep.c" "stasis.c" "release.c" "terminal.c"
                 "deck.c" "hull.c" "bunk.c" "curry.c" "bay.c" "airlock.c"
                 "drift.c" "pass.c" "console.c" "seg7.c" "scope.c" "vending.c" "bowl.c"
                 "mopbot.c")
# Every source: no fused multiply-add contraction, so the device builds the bytes the host tests pin. Listed on its own
# (each face task extends both lists), and the two must name the same files, and every .c in this directory (R147
# PR13): a source missing from the second list would build with contraction and drift from the host's frames.
set(RD_CORE_NO_CONTRACT prefs.c faces.c atlas2.c chime_env.c asleep.c stasis.c release.c terminal.c deck.c hull.c
                        bunk.c curry.c bay.c airlock.c drift.c pass.c console.c seg7.c scope.c vending.c bowl.c
                        mopbot.c)
set(_srcs ${RD_CORE_SRCS})
set(_nc ${RD_CORE_NO_CONTRACT})
list(SORT _srcs)
list(SORT _nc)
if(NOT _srcs STREQUAL _nc)
    message(FATAL_ERROR "rd_core: SRCS (${_srcs}) and the -ffp-contract=off list (${_nc}) differ")
endif()
file(GLOB _on_disk RELATIVE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/*.c)
# hidden files are no sources: an editor's lock link (.#x.c), macOS's ._x.c
list(FILTER _on_disk EXCLUDE REGEX "^[.]")
list(SORT _on_disk)
if(NOT _srcs STREQUAL _on_disk)
    message(FATAL_ERROR "rd_core: SRCS (${_srcs}) is not every .c in the component (${_on_disk})")
endif()
idf_component_register(SRCS ${RD_CORE_SRCS}
                       INCLUDE_DIRS "include"
                       REQUIRES omni_core face_kit)
set_source_files_properties(${RD_CORE_NO_CONTRACT} PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
