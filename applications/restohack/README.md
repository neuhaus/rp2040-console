# restoHack: checkout to UF2

restoHack - a restoration of Hack 1.0.3 - is the reference program, so it is
the fullest worked example. Hack reads its data files, keeps a scoreboard and
lock files, saves games and bones into a `save/` directory, seeds its maze from
entropy, and drives the screen through termcap, so a build of it exercises every
surface the engine offers, and its sources go in untouched.

Put the two checkouts side by side:

```
src/
  rp2040-console/
  restoHack/
```

Two headers have to be generated before Hack compiles: `config.h`, which
restoHack makes from `config.h.in`, and `hack.onames.h`, which `makedefs` makes
from `def.objects.h`. Because the build is a cross build, `makedefs` cannot be
compiled for the Pico and run there, so it is compiled for the host instead -
that, plus the manifest, is the entire glue. Add these two files to restoHack as
`restoHack/pico-bare/`.

`restoHack/pico-bare/CMakeLists.txt`:

```cmake
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
```

`restoHack/pico-bare/gen-onames.cmake`:

```cmake
# makedefs is a host utility, but this is a cross build: run it under `cmake -P`
# with a compiler chosen for the build machine, not the ARM target.
execute_process(COMMAND ${HOST_C_COMPILER} -O1 -o ${MAKEDEFS_BIN} ${MAKEDEFS_SRC}
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "could not compile host makedefs (${rc})")
endif()
execute_process(COMMAND ${MAKEDEFS_BIN} ${DEF_OBJECTS}
                OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "makedefs ${DEF_OBJECTS} failed (${rc})")
endif()
```

Then configure and build:

```sh
cd restoHack/pico-bare
cmake -S . -B build \
      -DGAME_CONSOLE=../../rp2040-console \
      -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hack      # -> build/hack.uf2       (the program)
cmake --build build --target volume    # -> build/hack-volume.uf2 (the FAT volume)
```

That produces `hack.uf2` and `hack-volume.uf2`, built from unmodified Hack
sources. Flash both and watch the port as described above; the board asks for
the date once at boot (the engine's clock prompt - press enter to accept the
build date), then Hack drops you into the dungeon with `data`, `help`, `hh` and
`rumors` already on the volume and a `save/` directory waiting.

Two things are worth knowing:

- **The default layout targets a 2MB Pico.** The volume sits at offset `0x180000`
  (1.5MB) and is 512KB, ending exactly at the 2MB mark. On a larger board the
  defaults work as they are; to fit a smaller part, move the volume down with
  `-DRP2040_CONSOLE_FS_OFFSET` and `-DRP2040_CONSOLE_FS_SIZE` (the program itself
  is under 400KB, so the room is there - it is the default offset that assumes 2MB).
- **Nothing in these files names Hack except the manifest.** The engine learns
  Hack's data files, environment and clashing names from the `rp2040_console_game`
  call and nothing else, which is the point. To build some other game, write your
  own call and drop the two generated-header steps it does not need.
