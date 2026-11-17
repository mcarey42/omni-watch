# tools/ringpack: the Omni ring strips (and the RLE subset bench frames) from the exported engravings, built on the
# host from omni_core's own sources (the same omni_strip_from_rgba and omni_rle_encode the host tests cover).
#   cmake -S tools/ringpack -B tools/ringpack/build && cmake --build tools/ringpack/build   (tools/assets.sh does it)
cmake_minimum_required(VERSION 3.16)
project(ringpack C)

set(CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/omni_core)
add_compile_options(-std=c99 -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L -O2)
file(GLOB CORE_SRCS CONFIGURE_DEPENDS ${CORE_DIR}/*.c)
add_executable(ringpack ringpack.c ${CORE_SRCS})
target_include_directories(ringpack PRIVATE ${CORE_DIR}/include)
target_link_libraries(ringpack PRIVATE m)
