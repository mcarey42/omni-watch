# tools/bgpack: the three brass backgrounds (R112) from omni_core's own renderer (omni/brassbg.h), built on the host.
#   cmake -S tools/bgpack -B tools/bgpack/build && cmake --build tools/bgpack/build   (tools/assets.sh does it)
# The bytes depend on the host's libm and FMA contraction (R115): the committed bg_*.bin are the source of truth and
# assets.sh only compares bgpack's output with them, unless --bg.
cmake_minimum_required(VERSION 3.16)
project(bgpack C)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/omni_core)
add_compile_options(-std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -O2)
add_executable(bgpack bgpack.c ${CORE_DIR}/brassbg.c)
target_include_directories(bgpack PRIVATE ${CORE_DIR}/include)
target_link_libraries(bgpack PRIVATE m)
