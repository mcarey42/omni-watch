# The dw assets' build-time steps (cmake -P, run by main/CMakeLists.txt; dw wave 1 Task 9), by -DMODE:
#
# MODE=stage (G4, G7). -DSRC: firmware/apps/dw/assets; -DART: the overlay's clone; -DSTAGE: the staging dir (named
#   "assets").
#   1. STAGE is emptied and SRC copied into it.
#   2. ART/overlay.json (theme "dw") lists the files it replaces. Only the swap files may be listed (dw_doors_m.bin,
#      dw_b5_box.bin and, from wave 2 Task 4, dw_doors_c.bin; from public release Task 9 the thumbnails dw_th_b5.bin
#      and dw_th_d3.bin and the chime dw_chime.pcm), any non-empty subset, each once. Each must exist in ART/assets and
#      have a generic twin in SRC. A .bin must have its twin's LVGL header (magic, cf, width, height, stride) and size;
#      a .pcm an even size from 2 to 110 080 B (16 kHz s16le up to DW_CHIME_CHIME_MAX_MS, 3 440 ms; plan C2). Anything
#      else fails the build. Each is then copied over its twin.
# MODE=validate (wave 2 Task 4; main/CMakeLists.txt runs it at configure, so a bad overlay fails the configure, not
#   only the build). -DART: step 2's descriptor checks (the theme; the list, each name a swap file, none twice) and
#   the names checks below.
# MODE=names (public release Task 9). -DART, -DOUT: the descriptor's optional "names" object, the private display
#   names, into OUT (dw_names_overlay.h, which main/dw_names.h includes under DW_NAMES_OVERLAY): a do-not-edit comment,
#   then one `#define DW_NAME_<X> "<value>"` per entry in sorted key order. The keys: "theme" -> DW_NAME_THEME,
#   "face:<key>" -> DW_NAME_FACE_<KEY>, "group:<id>" -> DW_NAME_GROUP_<ID>; only name_keys below (DW_FACE_TABLE's keys
#   and the four Settings groups). "names" must be an object of at most 32 members, each value a string of 1 to 24
#   characters from 0x20-0x7E but '"' and '\', with no leading or trailing space and no "??" (a trigraph under
#   -Werror). OUT is rewritten only when its text changes. Without "names", OUT is deleted if it exists, so no header
#   from an earlier configure survives (preflight M16).
# MODE=art (review M6, run on every overlay build). -DART, -DART_TXT: the build dir's dw_art.txt reads
#   "overlay <short sha>" (plus "-dirty" when the clone has changes); rewritten only when that text changes.
# MODE=mmap (G3, review M5). -DIN: the packer's mmap_generate_assets.h; -DOUT: the copy dw_theme.c includes; -DSTALE:
#   main/mmap_generate_assets.h. Fails if STALE exists (it would shadow the copy: a quoted include finds the source
#   file's own directory first). The copy gains DW_MMAP_PER_BUILD_DIR, which dw_theme.c requires.
cmake_minimum_required(VERSION 3.19)
if(NOT DEFINED MODE)
    set(MODE stage)
endif()
function(need)
    foreach(v IN LISTS ARGN)
        if(NOT DEFINED ${v})
            message(FATAL_ERROR "dw_stage.cmake MODE=${MODE}: -D${v} is required")
        endif()
    endforeach()
endfunction()

if(MODE STREQUAL "mmap")
    need(IN OUT STALE)
    if(EXISTS "${STALE}")
        message(FATAL_ERROR "A stale ${STALE} exists. It would shadow this build dir's assets header and compile another "
                            "image's checksum into the app (G3). Delete it, then build again.")
    endif()
    file(READ "${IN}" text)
    string(APPEND text "\n/* Added by firmware/apps/dw/main/dw_stage.cmake: this is the build dir's own copy (G3). */\n"
                       "#define DW_MMAP_PER_BUILD_DIR 1\n")
    set(old "")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${OUT}" "${text}")
    else()
        file(TOUCH "${OUT}")   # newer than the image, so the step does not rerun every build
    endif()
    return()
endif()

if(MODE STREQUAL "art")
    need(ART ART_TXT)
    execute_process(COMMAND git -C "${ART}" rev-parse --short HEAD OUTPUT_VARIABLE sha
                    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE rc ERROR_QUIET)
    if(NOT rc EQUAL 0)
        set(sha "unknown")
    endif()
    execute_process(COMMAND git -C "${ART}" status --porcelain OUTPUT_VARIABLE dirty ERROR_QUIET)
    if(dirty)
        string(APPEND sha "-dirty")
    endif()
    set(old "")
    if(EXISTS "${ART_TXT}")
        file(READ "${ART_TXT}" old)
    endif()
    if(NOT old STREQUAL "overlay ${sha}\n")
        file(WRITE "${ART_TXT}" "overlay ${sha}\n")
    endif()
    return()
endif()

# The swap files (R132): the only files an overlay may replace; each has a generic twin in firmware/apps/dw/assets.
set(allowed dw_doors_m.bin dw_b5_box.bin dw_doors_c.bin dw_th_b5.bin dw_th_d3.bin dw_chime.pcm)
# The display names an overlay may set (MODE=names): the theme's, each DW_FACE_TABLE key's (dw/prefs.h) and each
# Settings group's (tools/test_dw_stage.py checks this list against the table).
set(name_keys theme
    face:crystal face:roundel face:callbox face:rotor face:coral face:circles face:digital face:learn face:room
    face:amber face:fob face:console face:wall face:doorway face:broadcast face:vortex
    group:modern group:classic group:circles group:faces)

# The descriptor: theme "dw", and replaces a list of swap files, none twice; sets swaps. Its optional names object is
# checked too; sets name_list (the keys, sorted) and name_<key> (each value).
function(read_descriptor)
    file(READ "${ART}/overlay.json" json)
    string(JSON theme ERROR_VARIABLE err GET "${json}" theme)
    if(err OR NOT theme STREQUAL "dw")
        message(FATAL_ERROR "${ART}/overlay.json: \"theme\" must be \"dw\" (${err})")
    endif()
    string(JSON kind ERROR_VARIABLE err TYPE "${json}" replaces)
    if(err OR NOT kind STREQUAL "ARRAY")
        message(FATAL_ERROR "${ART}/overlay.json: \"replaces\" must be a list of the files it replaces (${err})")
    endif()
    string(JSON n ERROR_VARIABLE err LENGTH "${json}" replaces)
    if(err OR n LESS 1)
        message(FATAL_ERROR "${ART}/overlay.json: \"replaces\" must list the files it replaces (${err})")
    endif()
    math(EXPR last "${n} - 1")
    set(swaps "")
    foreach(i RANGE ${last})
        string(JSON name GET "${json}" replaces ${i})
        if(NOT name IN_LIST allowed)
            message(FATAL_ERROR "${ART}/overlay.json replaces ${name}: an overlay may replace only ${allowed}")
        endif()
        if(name IN_LIST swaps)
            message(FATAL_ERROR "${ART}/overlay.json lists ${name} twice")
        endif()
        list(APPEND swaps "${name}")
    endforeach()
    set(swaps "${swaps}" PARENT_SCOPE)

    set(name_list "")
    string(JSON kind ERROR_VARIABLE err TYPE "${json}" names)
    if(err)   # no names: the generic ones
        set(name_list "" PARENT_SCOPE)
        return()
    endif()
    if(NOT kind STREQUAL "OBJECT")
        message(FATAL_ERROR "${ART}/overlay.json: \"names\" must be an object of display names (it is ${kind})")
    endif()
    string(JSON n LENGTH "${json}" names)
    if(n GREATER 32)
        message(FATAL_ERROR "${ART}/overlay.json: \"names\" has ${n} members, at most 32")
    endif()
    if(n EQUAL 0)
        set(name_list "" PARENT_SCOPE)
        return()
    endif()
    math(EXPR last "${n} - 1")
    foreach(i RANGE ${last})
        string(JSON key MEMBER "${json}" names ${i})
        if(NOT key IN_LIST name_keys)
            message(FATAL_ERROR "${ART}/overlay.json names \"${key}\": not a known key (theme, face:<DW_FACE_TABLE key>, "
                                "group:modern|classic|circles|faces)")
        endif()
        string(JSON vkind TYPE "${json}" names "${key}")
        if(NOT vkind STREQUAL "STRING")
            message(FATAL_ERROR "${ART}/overlay.json names \"${key}\": the value must be a string (it is ${vkind})")
        endif()
        string(JSON value GET "${json}" names "${key}")
        string(LENGTH "${value}" len)
        string(FIND "${value}" "\"" quote)
        string(FIND "${value}" "\\" backslash)
        string(FIND "${value}" "??" trigraph)
        if(len LESS 1 OR len GREATER 24)
            message(FATAL_ERROR "${ART}/overlay.json names \"${key}\": ${len} characters; a name has 1 to 24")
        elseif(NOT value MATCHES "^[ -~]+$" OR NOT quote EQUAL -1 OR NOT backslash EQUAL -1)
            message(FATAL_ERROR "${ART}/overlay.json names \"${key}\": only the characters 0x20-0x7E, never a quote "
                                "or a backslash")
        elseif(value MATCHES "^ " OR value MATCHES " $")
            message(FATAL_ERROR "${ART}/overlay.json names \"${key}\": a leading or trailing space")
        elseif(NOT trigraph EQUAL -1)
            message(FATAL_ERROR "${ART}/overlay.json names \"${key}\": \"??\" (a trigraph under -Werror)")
        endif()
        list(APPEND name_list "${key}")
        set(name_${key} "${value}" PARENT_SCOPE)
    endforeach()
    list(SORT name_list)
    set(name_list "${name_list}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "validate")
    need(ART)
    read_descriptor()
    return()
endif()

if(MODE STREQUAL "names")
    need(ART OUT)
    read_descriptor()
    if(NOT name_list)
        file(REMOVE "${OUT}")   # no names: no header from an earlier configure survives (M16)
        return()
    endif()
    string(CONCAT text "/* dw_names_overlay.h: the private art overlay's display names (its overlay.json \"names\"), written by\n"
             " * firmware/apps/dw/main/dw_stage.cmake MODE=names at configure. Do not edit: edit the overlay. */\n")
    foreach(key IN LISTS name_list)
        if(key STREQUAL "theme")
            set(macro DW_NAME_THEME)
        else()
            string(REGEX REPLACE "^(face|group):" "" id "${key}")
            string(REGEX REPLACE ":.*$" "" kind "${key}")
            string(TOUPPER "DW_NAME_${kind}_${id}" macro)
        endif()
        string(APPEND text "#define ${macro} \"${name_${key}}\"\n")
    endforeach()
    set(old "")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${OUT}" "${text}")
    endif()
    return()
endif()

need(SRC ART STAGE)
read_descriptor()

file(REMOVE_RECURSE "${STAGE}")
file(MAKE_DIRECTORY "${STAGE}")
file(GLOB generic "${SRC}/*")
file(COPY ${generic} DESTINATION "${STAGE}")

function(lvgl_header path out)   # magic, cf, w, h, stride (bytes 0, 1, 4-9) as hex, plus the size
    file(READ "${path}" hex LIMIT 12 HEX)
    file(SIZE "${path}" size)
    string(SUBSTRING "${hex}" 0 4 a)
    string(SUBSTRING "${hex}" 8 12 b)
    set(${out} "${a}:${b}:${size}" PARENT_SCOPE)
endfunction()

foreach(name IN LISTS swaps)
    if(NOT EXISTS "${ART}/assets/${name}" OR NOT EXISTS "${SRC}/${name}")
        message(FATAL_ERROR "${name}: missing from ${ART}/assets or from ${SRC}")
    endif()
    if(name MATCHES "\\.pcm$")   # the chime: an even size up to the slot, 3 440 ms at 16 kHz s16le (plan C2)
        file(SIZE "${ART}/assets/${name}" size)
        math(EXPR odd "${size} % 2")
        if(size LESS 2 OR size GREATER 110080 OR odd)
            message(FATAL_ERROR "${name}: the overlay's is ${size} B; a swapped sound is an even 2 to 110 080 B")
        endif()
    else()
        lvgl_header("${ART}/assets/${name}" o)
        lvgl_header("${SRC}/${name}" g)
        if(NOT o STREQUAL g)
            message(FATAL_ERROR "${name}: the overlay's (magic:cf, w h stride, size ${o}) is not the generic one's (${g})")
        endif()
    endif()
    file(COPY_FILE "${ART}/assets/${name}" "${STAGE}/${name}")
endforeach()
# Fresh times on every staged file, so the image rebuilds from this staging and not from copied timestamps.
file(GLOB staged "${STAGE}/*")
file(TOUCH ${staged})

message(STATUS "dw assets staged: ${swaps} from the overlay")
