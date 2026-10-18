cmake_minimum_required(VERSION 3.13)

# Where rp2040-console lives: its bundled pico_sdk_import.cmake (before
# project()) and its cmake module (after) are both found here. PICO_SDK_PATH is
# read from the environment or the cache by that import.
if(NOT GAME_CONSOLE AND DEFINED ENV{GAME_CONSOLE})
    set(GAME_CONSOLE "$ENV{GAME_CONSOLE}")
endif()
get_filename_component(GAME_CONSOLE "${GAME_CONSOLE}" ABSOLUTE)

include(${GAME_CONSOLE}/pico_sdk_import.cmake)          # installs the ARM toolchain
project(hack C CXX ASM)
set(CMAKE_C_STANDARD 11)
include(${GAME_CONSOLE}/cmake/rp2040-console.cmake)     # calls pico_sdk_init()

set(HACK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(HACK_SRC  "${HACK_ROOT}/src")
set(GEN       "${CMAKE_CURRENT_BINARY_DIR}/generated")

# config.h: none of restoHack's feature checks apply here except getentropy(),
# which the console really does provide (from the RP2040 ring oscillator).
set(HAVE_GETENTROPY ON)
configure_file("${HACK_ROOT}/config.h.in" "${GEN}/config.h")

# hack.onames.h, from makedefs - built with the host compiler, see below.
if(NOT HOST_C_COMPILER)
    find_program(HOST_C_COMPILER cc)
endif()
set(ONAMES "${GEN}/hack.onames.h")
add_custom_command(
    OUTPUT  ${ONAMES}
    COMMAND ${CMAKE_COMMAND}
            -DHOST_C_COMPILER=${HOST_C_COMPILER}
            -DMAKEDEFS_SRC=${HACK_SRC}/makedefs.c
            -DDEF_OBJECTS=${HACK_SRC}/def.objects.h
            -DMAKEDEFS_BIN=${CMAKE_CURRENT_BINARY_DIR}/makedefs-host
            -DOUT_FILE=${ONAMES}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/gen-onames.cmake
    DEPENDS ${HACK_SRC}/makedefs.c ${HACK_SRC}/def.objects.h
            ${CMAKE_CURRENT_SOURCE_DIR}/gen-onames.cmake
    COMMENT "Generating hack.onames.h with makedefs")

# The game's own sources: all of restoHack/src except makedefs. Termcap front
# end included, so there is no curses path compiled in.
file(GLOB HACK_SOURCES "${HACK_SRC}/*.c")
list(REMOVE_ITEM HACK_SOURCES "${HACK_SRC}/makedefs.c")

rp2040_console_game(
    NAME         hack
    ROOT         ${HACK_ROOT}
    SOURCES      ${HACK_SOURCES}
    GENERATED    ${ONAMES}
    INCLUDE_DIRS ${HACK_SRC} ${GEN}
    LEGACY_C

    # The volume is Hack's whole filesystem, mounted at "/", so HACKDIR is the
    # root rather than the /usr/games/hack of a Unix install.
    ENV          TERM=xterm HACKDIR=/

    DATA_FILES   data help hh rumors         # read-only game data
    EMPTY_FILES  record perm hack            # opened but never created by Hack
    DATA_DIRS    save                        # where save files and bones go
    VOLUME_NAME  HACKFS

    # Hack's one-argument itoa() and its own panic() collide with names in the
    # link; the prefix header renames them out of the way.
    RENAMES      itoa=hackitoa panic=hack_panic
)
