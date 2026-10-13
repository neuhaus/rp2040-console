cmake_minimum_required(VERSION 3.13)
if(NOT RP2040_CONSOLE_DIR)
    message(FATAL_ERROR "Set RP2040_CONSOLE_DIR to the console checkout")
endif()
include(${RP2040_CONSOLE_DIR}/pico_sdk_import.cmake)
project(nethack367 C CXX ASM)
set(CMAKE_C_STANDARD 11)
option(NETHACK_SOFT_REBOOT "Restart the game while preserving the USB console" OFF)
set(_restart_option)
if(NETHACK_SOFT_REBOOT)
    set(_restart_option SOFT_REBOOT)
endif()

# NetHack startup options for this image, handed to the game as
# NETHACKOPTIONS.  The recipe ships none of its own beyond `color` (added
# below), and choices personal to a build (a symbol set, a keyboard layout)
# are passed per build rather than committed.  build.py sets this from
# --nethack-options, e.g. "symset:DECgraphics,number_pad:-1".
set(NETHACK_OPTIONS "" CACHE STRING
        "Extra NETHACKOPTIONS for this image; appended after the default")

# One line printed before the game starts, handed to the engine as the BANNER
# environment variable.  It is build-time text, not a literal in the port, so
# a build can name itself; build.py sets it from --banner or $BANNER.
set(NETHACK_BANNER "RP2040 Console ready." CACHE STRING
        "Startup banner line; empty for none")

# NetHack's whoami() seeds the character name from $USER or $LOGNAME and then
# skips the "Who are you?" prompt, so neither is set here: the player names
# their own character.  HOME stays for NetHack's config-file lookup.
set(_game_env TERM=xterm HOME=/)
# `color` is off by default in a Unix tty build, because upstream expects to
# run on terminals of every stripe; the board always drives a colour-capable
# ANSI emulator, so turn it on.  A per-build --nethack-options value is
# appended after it and can override it, e.g. "!color".
set(_nethack_options "color")
if(NETHACK_OPTIONS)
    string(APPEND _nethack_options ",${NETHACK_OPTIONS}")
endif()
list(APPEND _game_env "NETHACKOPTIONS=${_nethack_options}")
list(APPEND _game_env "BANNER=${NETHACK_BANNER}")

include(${RP2040_CONSOLE_DIR}/cmake/rp2040-console.cmake)

# HACKCSRC is exactly every src/*.c.  src/vis_tab.c is generated there by
# `makedefs -z` before this configure runs, so the glob picks it up too.
# src/dlb.c is the recipe's Tamp-backed data librarian, copied over the
# stock one by build.py.
file(GLOB SRCS src/*.c win/tty/*.c)
list(APPEND SRCS
        sys/unix/unixmain.c
        sys/unix/unixunix.c
        sys/unix/unixres.c
        sys/share/unixtty.c
        sys/share/ioctl.c
        sys/share/pmatchregex.c)

# The data files are Tamp-compressed and collected into one archive by
# build.py, so the volume carries `nhdata` alone.  Keeping them separate
# would cost a 2 KB FAT cluster per file, and `symbols` is in the archive
# too (the port serves its stdio FILE* from the librarian).
rp2040_console_game(NAME nethack ${_restart_option}
        ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dat
        RENAMES panic=nh_panic
        SOURCES ${SRCS}
        INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/tamp
                     ${CMAKE_CURRENT_SOURCE_DIR}/uzlib
        LEGACY_C LEGACY_TIME32
        ENV ${_game_env}
        DATA_FILES nhdata
        EMPTY_FILES record perm logfile xlogfile
        DATA_DIRS save
        LISTDIR save
        FS_OFFSET 0x17E000 FS_SIZE 0x82000
        HEAP_END 0x2003c000 STACK_RESERVE 24576)
target_compile_options(nethack PRIVATE -Oz -fsigned-char
        -fno-unwind-tables -fno-asynchronous-unwind-tables)
target_link_options(nethack PRIVATE --specs=nano.specs)
set_property(SOURCE ${SRCS} APPEND PROPERTY COMPILE_OPTIONS -include ${CMAKE_CURRENT_SOURCE_DIR}/compat.h)
target_sources(nethack PRIVATE platform.c memlog.c)

# The Tamp decompressor, too small to notice in the flash budget and small
# enough in RAM to stream one data file at a time.  It is deliberately not
# listed in SOURCES: that would drag the platform's prefix header (which
# renames getchar and friends) and NetHack's compat.h into a third-party
# library.
#
# TAMP_STREAM=0 drops the convenience read/write workers, which this port
# does not use.  Extended compression is the library default.
target_sources(nethack PRIVATE tamp/common.c tamp/decompressor.c tamp/compressor.c)
set_property(SOURCE tamp/common.c tamp/decompressor.c tamp/compressor.c APPEND PROPERTY
        COMPILE_DEFINITIONS TAMP_STREAM=0)

# raw-deflate inflate for the data librarian (port/nhdlb.c).  Like Tamp it is
# a third-party source, so it too stays out of SOURCES (no compat.h, no
# platform prefix header).
target_sources(nethack PRIVATE uzlib/tinflate.c)

