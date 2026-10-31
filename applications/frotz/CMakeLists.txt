cmake_minimum_required(VERSION 3.13)
get_filename_component(RP2040_CONSOLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include(${RP2040_CONSOLE_DIR}/pico_sdk_import.cmake)
project(frotz_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
include(${RP2040_CONSOLE_DIR}/cmake/rp2040-console.cmake)

# The Frotz source tree to compile. build.py fetches one into the build
# directory and passes this in; a direct cmake invocation falls back to the
# sibling ../frotz. Override with -DFROTZ_DIR=...
set(FROTZ_DIR "${RP2040_CONSOLE_DIR}/../frotz" CACHE PATH "Upstream Frotz source checkout")
if(NOT IS_DIRECTORY "${FROTZ_DIR}/src/common")
    message(FATAL_ERROR "No Frotz checkout at ${FROTZ_DIR} (set -DFROTZ_DIR=...)")
endif()

# A native Frotz build leaves defs.h and hash.h in src/common. A quoted include
# finds the including file's own directory before any -I, so those would shadow
# the platform's generated headers and quietly change the feature set.
foreach(_stale defs.h hash.h)
    if(EXISTS "${FROTZ_DIR}/src/common/${_stale}")
        message(FATAL_ERROR
            "${FROTZ_DIR}/src/common/${_stale} exists - a native Frotz build left it, "
            "and it would shadow the generated header. Remove it or use a fresh checkout.")
    endif()
endforeach()

# Top of the heap and the stack reserve above it. The defaults leave the
# platform's usual 24 KB of stack and scratch RAM below the top of SRAM. Raise
# the heap for a large story, but check the build's memory line afterwards: the
# stack is what pays for it.
set(FROTZ_HEAP_END "0x2003c000" CACHE STRING "Top of the heap")
set(FROTZ_STACK_RESERVE "24576" CACHE STRING "Minimum space above the heap")

# --- the story file --------------------------------------------------------
#
# There is no command line on this device, so the name of the game is build
# data: it goes onto the volume and is handed to frotz as argv[1]. Nothing is
# bundled with the recipe, because a story file is someone else's copyright and
# the recipe cannot grant a right to it.
set(FROTZ_STORY "" CACHE FILEPATH "Z-code story file to play (empty builds the interpreter only)")
set(FROTZ_EXTRA_ARGS "" CACHE STRING
    "Extra frotz arguments, space separated; e.g. \"-u 4\" or \"-f ansi\"")
separate_arguments(FROTZ_EXTRA_ARGS_LIST UNIX_COMMAND "${FROTZ_EXTRA_ARGS}")

if(FROTZ_STORY)
    if(NOT EXISTS "${FROTZ_STORY}")
        message(FATAL_ERROR "FROTZ_STORY not found: ${FROTZ_STORY}")
    endif()
    get_filename_component(_story_dir "${FROTZ_STORY}" DIRECTORY)
    get_filename_component(FROTZ_STORY_NAME "${FROTZ_STORY}" NAME)
    set(FROTZ_ROOT "${_story_dir}")
    set(FROTZ_DATA_FILES "${FROTZ_STORY_NAME}")
    set(FROTZ_ARGS ${FROTZ_EXTRA_ARGS_LIST} "${FROTZ_STORY_NAME}")

    # Frotz loads the whole story into RAM and keeps a full copy of dynamic
    # memory plus a 1.5x diff buffer for undo, so the size of the game decides
    # whether it fits. Read the header fields the same way frotz does and say
    # what the picture looks like, rather than leaving it to a runtime "Out of
    # memory" after flashing.
    file(READ "${FROTZ_STORY}" _story_hex OFFSET 0 LIMIT 64 HEX)
    string(SUBSTRING "${_story_hex}" 0 2 _ver_hex)
    math(EXPR STORY_VERSION "0x${_ver_hex}")
    string(SUBSTRING "${_story_hex}" 28 4 _dyn_hex)
    math(EXPR STORY_DYNAMIC "0x${_dyn_hex}")
    string(SUBSTRING "${_story_hex}" 52 4 _fsz_hex)
    math(EXPR STORY_FILESIZE_FIELD "0x${_fsz_hex}")
    if(STORY_FILESIZE_FIELD EQUAL 0)
        file(SIZE "${FROTZ_STORY}" STORY_SIZE)
    else()
        math(EXPR STORY_SIZE "${STORY_FILESIZE_FIELD} * 2")
        if(STORY_VERSION GREATER_EQUAL 4)
            math(EXPR STORY_SIZE "${STORY_SIZE} * 2")
        endif()
        if(STORY_VERSION GREATER_EQUAL 6)
            math(EXPR STORY_SIZE "${STORY_SIZE} * 2")
        endif()
    endif()
    # story + prev_zmp + undo_diff + screen cells (80x24 cell_t) + change map.
    math(EXPR FROTZ_WORKING_SET
        "${STORY_SIZE} + ${STORY_DYNAMIC} + (${STORY_DYNAMIC} * 3) / 2 + 2 + (80 * 24 * 12) + (80 * 24)")
    message(STATUS
        "Frotz story: v${STORY_VERSION}, ${STORY_SIZE} B, dynamic ${STORY_DYNAMIC} B; "
        "fixed working set about ${FROTZ_WORKING_SET} B")

    # A rough heap budget: HEAP_END minus the base of SRAM minus room for the
    # interpreter's static data and the undo blocks that accumulate during play.
    math(EXPR _frotz_heap "${FROTZ_HEAP_END} - 0x20000000 - 40000")
    if(FROTZ_WORKING_SET GREATER _frotz_heap)
        message(WARNING
            "The story needs about ${FROTZ_WORKING_SET} B and there is only about "
            "${_frotz_heap} B of heap at the default layout. Pick a smaller game, or "
            "raise -DFROTZ_HEAP_END and lower -DFROTZ_STACK_RESERVE, or trim undo with "
            "-DFROTZ_EXTRA_ARGS=\"-u 4\".")
    endif()
else()
    set(FROTZ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
    set(FROTZ_DATA_FILES "")
    set(FROTZ_ARGS ${FROTZ_EXTRA_ARGS_LIST})
    message(WARNING
        "FROTZ_STORY is unset: building the interpreter only. Set "
        "-DFROTZ_STORY=/path/to/game.z3 (or .z5 ...) to include a game on the volume.")
endif()

# --- generated version headers ---------------------------------------------
#
# Frotz's build writes two headers - defs.h (feature and size settings) and
# hash.h (version strings) - into src/common before compiling. Writing into the
# checkout would leave it modified, so they are generated here instead, in the
# build tree, where the quoted includes from frotz.h fall back to and find them.
file(STRINGS "${FROTZ_DIR}/Makefile" _ver REGEX "^VERSION[ \t]*=")
list(GET _ver 0 _ver)
string(REGEX REPLACE "^VERSION[ \t]*=[ \t]*" "" FROTZ_VERSION "${_ver}")
string(STRIP "${FROTZ_VERSION}" FROTZ_VERSION)

file(STRINGS "${FROTZ_DIR}/Makefile" _notes REGEX "^RELEASE_NOTES[ \t]*=")
list(GET _notes 0 _notes)
string(REGEX REPLACE "^RELEASE_NOTES[ \t]*=[ \t]*" "" FROTZ_RELEASE_NOTES "${_notes}")
string(STRIP "${FROTZ_RELEASE_NOTES}" FROTZ_RELEASE_NOTES)

execute_process(COMMAND git -C "${FROTZ_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE FROTZ_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET RESULT_VARIABLE _git)
if(NOT _git EQUAL 0)
    set(FROTZ_GIT_HASH "unknown")
endif()
string(SUBSTRING "${FROTZ_GIT_HASH}" 0 8 FROTZ_GIT_HASH_SHORT)
execute_process(COMMAND git -C "${FROTZ_DIR}" show -s --format=%ci
        OUTPUT_VARIABLE FROTZ_GIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
if(NOT FROTZ_GIT_DATE)
    set(FROTZ_GIT_DATE "unknown")
endif()

set(_defs "#ifndef COMMON_DEFINES_H\n#define COMMON_DEFINES_H\n")
string(APPEND _defs "#define VERSION \"${FROTZ_VERSION}\"\n")
string(APPEND _defs "#define RELEASE_NOTES ${FROTZ_RELEASE_NOTES}\n")
string(APPEND _defs "#define UNIX\n")
# Blorb would drag in double-precision arithmetic, which this platform builds
# without; plain story files need none of it.
string(APPEND _defs "#define NO_BLORB\n")
string(APPEND _defs "#define MAX_UNDO_SLOTS 500\n")
string(APPEND _defs "#define MAX_FILE_NAME 80\n")
string(APPEND _defs "#define TEXT_BUFFER_SIZE 512\n")
string(APPEND _defs "#define INPUT_BUFFER_SIZE 200\n")
string(APPEND _defs "#define STACK_SIZE 1024\n")
# The host terminal is assumed UTF-8, which is what the dumb interface emits
# Latin-1 characters as when this is set. Without it they go out as raw bytes.
string(APPEND _defs "#define USE_UTF8\n")
string(APPEND _defs "#endif /* COMMON_DEFINES_H */\n")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/defs.h" "${_defs}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/hash.h"
        "#ifndef VERSION\n#define VERSION \"${FROTZ_VERSION}\"\n#endif\n"
        "#ifndef RELEASE_NOTES\n#define RELEASE_NOTES ${FROTZ_RELEASE_NOTES}\n#endif\n"
        "#define GIT_HASH \"${FROTZ_GIT_HASH}\"\n"
        "#define GIT_HASH_SHORT \"${FROTZ_GIT_HASH_SHORT}\"\n"
        "#define GIT_DATE \"${FROTZ_GIT_DATE}\"\n")

# --- sources ---------------------------------------------------------------
#
# The core, the dumb terminal front end, and nothing else. The curses front end
# needs ncurses, which this platform does not have; the dumb front end is the
# one written for a terminal that only knows how to print and read lines.
file(GLOB FROTZ_CORE CONFIGURE_DEPENDS "${FROTZ_DIR}/src/common/*.c")
file(GLOB FROTZ_UI   CONFIGURE_DEPENDS "${FROTZ_DIR}/src/dumb/*.c")

rp2040_console_game(
    NAME        frotz
    ROOT        ${FROTZ_ROOT}
    ARGV0       frotz
    ARGS        ${FROTZ_ARGS}
    SOURCES     ${FROTZ_CORE} ${FROTZ_UI}
    INCLUDE_DIRS
                ${FROTZ_DIR}/src/common
                ${FROTZ_DIR}/src/dumb
    DATA_FILES  ${FROTZ_DATA_FILES}
    ENV         TERM=xterm
    HEAP_END    ${FROTZ_HEAP_END}
    STACK_RESERVE ${FROTZ_STACK_RESERVE}
)

# ARM char is unsigned by default; Frotz assumes a signed char in places.
target_compile_options(frotz PRIVATE -fsigned-char)
