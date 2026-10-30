cmake_minimum_required(VERSION 3.13)
get_filename_component(RP2040_CONSOLE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include(${RP2040_CONSOLE_DIR}/pico_sdk_import.cmake)
project(console_rogue C CXX ASM)
set(CMAKE_C_STANDARD 11)
include(${RP2040_CONSOLE_DIR}/cmake/rp2040-console.cmake)

# Rogue 9.8.6 is cloned at a pinned revision on the first configure; its
# sources are compiled as they are. Set ROGUE_SRC to build a local checkout
# (the directory holding rogue.h) instead.
set(ROGUE_GIT_REPOSITORY "https://github.com/mpwillson/rogue.git" CACHE STRING
    "Rogue 9.8.6 git repository")
set(ROGUE_GIT_TAG "c0386a6ed2800f7769a76c4821a5397f165b8b0e" CACHE STRING
    "Rogue revision to build")
set(ROGUE_SRC "" CACHE PATH "Local Rogue source directory (empty: clone)")
if(ROGUE_SRC)
    get_filename_component(ROGUE_SRC "${ROGUE_SRC}" ABSOLUTE)
else()
    include(FetchContent)
    FetchContent_Declare(rogue
        GIT_REPOSITORY "${ROGUE_GIT_REPOSITORY}"
        GIT_TAG        "${ROGUE_GIT_TAG}"
        GIT_SUBMODULES ""
        GIT_PROGRESS   ON
        # No CMakeLists.txt there, so rogue's own (native/browser) build is
        # not added: only the sources are wanted.
        SOURCE_SUBDIR  rp2040-sources-only)
    FetchContent_MakeAvailable(rogue)
    set(ROGUE_SRC "${rogue_SOURCE_DIR}")
endif()
if(NOT EXISTS "${ROGUE_SRC}/rogue.h")
    message(FATAL_ERROR "No rogue.h in ${ROGUE_SRC}")
endif()

set(ROGUE_SOURCES
    curses.c hit.c init.c instruct.c inventory.c level.c machdep.c
    main.c message.c monster.c move.c object.c pack.c play.c random.c
    ring.c room.c save.c score.c spec_hit.c throw.c trap.c use.c zap.c)
list(TRANSFORM ROGUE_SOURCES PREPEND "${ROGUE_SRC}/")

# Volume contents are staged in the build tree: rogue.instr ships with CRLF
# line ends, which the help screen would print, and the termcap entry lives
# with this recipe.
set(ROGUE_STAGE "${CMAKE_CURRENT_BINARY_DIR}/volume-root")
file(MAKE_DIRECTORY "${ROGUE_STAGE}")
file(READ "${ROGUE_SRC}/rogue.instr" _instr)
string(REPLACE "\r\n" "\n" _instr "${_instr}")
file(WRITE "${ROGUE_STAGE}/rogue.instr" "${_instr}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/rogue.termcap"
               "${ROGUE_STAGE}/rogue.termcap" COPYONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${ROGUE_SRC}/rogue.instr")

# ROGUE_DIAG builds the resource-validation firmware used by
# tests/ascension.py; it adds wizard-mode helper commands through
# port/diag.c and must not be shipped.
option(ROGUE_DIAG "Build the resource-validation diagnostic firmware" OFF)
set(_diag_sources)
set(_diag_defines)
if(ROGUE_DIAG)
    set(_diag_sources "${CMAKE_CURRENT_SOURCE_DIR}/port/diag.c")
    set(_diag_defines ROGUE_DIAG=1)
endif()

# Restart in place after the game exits instead of rebooting the chip, so the
# USB serial connection survives; see INTERNALS.md "Optional soft reboot".
option(ROGUE_SOFT_REBOOT "Restart the game without a hardware reboot" OFF)
set(_soft_reboot)
if(ROGUE_SOFT_REBOOT)
    set(_soft_reboot SOFT_REBOOT)
endif()

# More ROGUEOPTS entries, comma-separated (e.g. "name=Sven,nojump"); they
# follow file=rogue.save, which restoring depends on.
set(ROGUE_EXTRA_OPTIONS "" CACHE STRING "Additional ROGUEOPTS entries")
set(_rogueopts "file=rogue.save")
if(ROGUE_EXTRA_OPTIONS)
    string(APPEND _rogueopts ",${ROGUE_EXTRA_OPTIONS}")
endif()

rp2040_console_game(
    NAME         rogue
    ROOT         ${ROGUE_STAGE}
    SOURCES      ${ROGUE_SOURCES} ${_diag_sources}
    INCLUDE_DIRS ${ROGUE_SRC}
    LEGACY_C
    # Damage reduction and ^A use double arithmetic.
    FLOATING_POINT
    ${_soft_reboot}
    # Rogue's own termcap-driven curses (RCURSES), reading the entry from the
    # volume. LOGNAME gives getpwuid() the entry md_gln() dereferences.
    DEFINES      RCURSES ${_diag_defines}
    # ROGUEOPTS makes rogue.save the save prompt's default; RESTORE_ARG
    # hands that file to Rogue as argv[1] - its only way of restoring - when
    # it exists at boot.
    ENV          TERM=xterm TERMCAP=/rogue.termcap LOGNAME=rogue HOME=/
                 ROGUEOPTS=${_rogueopts}
    RESTORE_ARG  rogue.save
    DATA_FILES   rogue.instr rogue.termcap
    EMPTY_FILES  rogue.scores
    VOLUME_NAME  ROGUE
    # Rogue needs little heap; give the stack room for its screen-sized
    # local buffers (help, scores, save) with a wide margin.
    HEAP_END     0x2003a000
    STACK_RESERVE 32768
)
if(ROGUE_DIAG)
    # port/diag.c intercepts every key the game reads; see that file.
    target_link_options(rogue PRIVATE -Wl,--wrap=rgetchar)
endif()
