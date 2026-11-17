# The rd assets' build-time step (cmake -P, run by main/CMakeLists.txt; PR31). rd has no art overlay, so this holds
# only dw_stage.cmake's MODE=mmap step; firmware/apps/dw/main/dw_stage.cmake is dw's own and unchanged.
#
# -DIN: the packer's mmap_generate_assets.h; -DOUT: the copy rd_theme.c includes; -DSTALE: main/mmap_generate_assets.h.
# Fails if STALE exists (it would shadow the copy: a quoted include finds the source file's own directory first). The
# copy gains RD_MMAP_PER_BUILD_DIR, which rd_theme.c requires, so each build dir compiles its own image's checksum (G3).
cmake_minimum_required(VERSION 3.19)
foreach(v IN ITEMS IN OUT STALE)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "rd_stage.cmake: -D${v} is required")
    endif()
endforeach()
if(EXISTS "${STALE}")
    message(FATAL_ERROR "A stale ${STALE} exists. It would shadow this build dir's assets header and compile another "
                        "image's checksum into the app (G3). Delete it, then build again.")
endif()
file(READ "${IN}" text)
string(APPEND text "\n/* Added by firmware/apps/rd/rd_stage.cmake: this is the build dir's own copy (G3). */\n"
                   "#define RD_MMAP_PER_BUILD_DIR 1\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
else()
    file(TOUCH "${OUT}")   # newer than the image, so the step does not rerun every build
endif()
