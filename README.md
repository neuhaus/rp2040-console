# rp2040-console

A bare-metal platform layer for terminal games on the Raspberry Pi Pico.

A program written for a terminal in the termcap era - it reads characters, it
writes escape sequences, it opens a few files by name - can be built for an
RP2040 with this engine and **no changes to its own sources**. Everything that
is missing on a microcontroller is either supplied or emulated in one place,
here, instead of being patched into the program file by file. That is the whole
design: the port is a layer, not a diff.

Hack 1.0.3 (restoHack) is the program this was developed against, and the claim
that the engine is game-agnostic is testable rather than a matter of opinion:
nothing in its sources mentions Hack - a grep for the word finds only
`restore_interrupts`. Where Hack needs something specific, it asks for it in its
own manifest.

    src/        the platform: console, terminal, files, environment, clock, startup
    include/    headers this toolchain does not have, plus the prefix header
    cmake/      the build API: rp2040_console_game()
    tools/      mkvolume.sh, which builds the FAT volume and its UF2

## Why bare metal

No OS to take away SRAM from the app - plus it's harder to cheat!

## Plugging a game in

    cmake_minimum_required(VERSION 3.13)
    include(/path/to/rp2040-console/pico_sdk_import.cmake)
    project(mygame C CXX ASM)
    set(CMAKE_C_STANDARD 11)
    include(/path/to/rp2040-console/cmake/rp2040-console.cmake)

    rp2040_console_game(
            NAME        mygame
            ROOT        ${CMAKE_CURRENT_SOURCE_DIR}
            SOURCES     ${SRCS}
            INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
            ENV         TERM=xterm HOMEDIR=/
            DATA_FILES  help data
            EMPTY_FILES scores
            DATA_DIRS   save
            )

That is a complete build: `make mygame` produces the firmware, `make volume`
produces the data volume, and both are flashed in either order. The arguments:

| Argument | Meaning |
| --- | --- |
| `NAME` | target and output name |
| `ROOT` | where `DATA_FILES` are found (default: the directory of the calling CMakeLists) |
| `SOURCES`, `GENERATED` | the program's sources, and anything generated that they need |
| `INCLUDE_DIRS` | the program's own include directories |
| `ENV` | `NAME=value` pairs the program sees through `getenv()` |
| `DATA_FILES` | files from `ROOT` to put on the volume |
| `EMPTY_FILES` | zero-length files the program expects an installation to have provided |
| `DATA_DIRS` | directories to create on the volume |
| `ARGV0` | what `argv[0]` holds (default: `NAME`) |
| `HEAP_END` | top of the heap; see the memory map below |
| `COLS`, `ROWS` | what the terminal reports its size as (default 80x24) |
| `LEGACY_C` | silence the diagnostics that pre-ANSI sources provoke |
| `LEGACY_TIME32` | checked 32-bit time API for program sources |
| `SOFT_REBOOT` | restart a single-threaded C program while retaining USB/platform state |
| `SYSTEM_HEADERS` | native declarations to preload before program renames |
| `FLASH_SIZE` | physical flash capacity shared by linker and data volume |
| `STACK_RESERVE` | minimum space above `HEAP_END`, including SDK scratch RAM |
| `DEFINES` | any extra preprocessor definitions the program wants |
| `RENAMES` | `name=replacement`, for a name in the program that clashes with something else in the link |

Cache variables: `RP2040_CONSOLE_FS_OFFSET` (default `0x180000`),
`RP2040_CONSOLE_FS_SIZE` (default 512KB), `RP2040_CONSOLE_COLS`,
`RP2040_CONSOLE_ROWS`, and the options `RP2040_CONSOLE_IO_TRACE`,
`RP2040_CONSOLE_UART_STDIN` and `RP2040_CONSOLE_CLOCK_PROMPT`.

### Compatibility options

`LEGACY_TIME32` is an opt-in manifest flag for programs written around signed
32-bit Unix seconds (including old `long` time variables). The program's
`time_t`, `time`, `localtime`, `gmtime`, their `_r` variants, `ctime`, `mktime`
and `difftime` use checked adapters. Native libc, SDK and platform code keep
their original ABI; `struct stat`, `struct timeval` and other pre-included
system structs retain native timestamp fields. Values outside 1901..2038
return `-1` with `errno = EOVERFLOW`; `time(out)` leaves `out` unchanged on
range failure. `rp2040_native_time_t` names the original type if needed.
This flag does not convert every OS time API or an application's binary files.

```cmake
rp2040_console_game(
    NAME mygame SOURCES ${SRCS}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    LEGACY_TIME32
    RENAMES panic=mygame_panic
    SYSTEM_HEADERS hardware/watchdog.h
    FLASH_SIZE 2097152
    FS_OFFSET 0x100000 FS_SIZE 1048576
    HEAP_END 0x2003c000 STACK_RESERVE 24576
)
```

The prefix is applied to program C/C++ sources in both `SOURCES` and
`GENERATED`, never to SDK, engine or FatFs sources. It reads libc and
`pico/stdlib.h` declarations before applying `RENAMES`. Use `SYSTEM_HEADERS`
for additional native headers that declare a conflicting name or use native
`time_t` in their ABI. Renames remain explicit: a program's `panic` need not
have the SDK's signature or meaning. Options are scoped to the target even
when the same source is compiled into multiple programs.

Quoted program headers use the program's `INCLUDE_DIRS` first (`-iquote`);
angle-bracket system includes continue to prefer platform compatibility
headers. This lets a program keep its own `"termcap.h"` while the platform
uses `<termcap.h>`. Program include directories do not leak into SDK/engine
translation units. Each target has its own generated environment/rename
headers and `<name>-volume` target; `volume` builds all declared volumes.

`cfgetispeed`, `cfgetospeed`, `cfsetispeed`, `cfsetospeed` and `cfsetspeed`
report/store the small `B*` codes, not numeric bits per second. USB CDC has no
physical baudrate. Baud bits do not overlap the character-size flags. Input
speed zero follows output speed; unsupported speed codes return `EINVAL`.

The single-user surface reports uid/gid zero, but invents no login or password
database: `getlogin`, `getpwuid` and `getpwnam` return NULL with `ENOENT`.
`chmod`/`fchmod` validate existence/descriptors without changing Unix permission
bits, which FatFs cannot represent. `umask` remembers and returns its previous
API value; it cannot enforce FAT permissions. There are still no subprocesses,
job control or OS accounts.

### Build and data checks

The build now fails if firmware overlaps the FAT region, the volume exceeds
`FLASH_SIZE`, or static RAM, heap and stack bounds collide. `STACK_RESERVE`
requires at least that many bytes above the heap, including SDK scratch RAM;
it does not measure peak stack use. The build prints actual firmware, static
RAM, heap and reserve sizes. `FLASH_SIZE` defaults to the SDK/board selection
(or 2 MB); the target's linker flash region and volume manifest use the same
capacity. The FAT region must have positive, 4 KB-aligned offset and size.
Normal builds keep `PICO_COPY_TO_RAM=0` (XIP).

Host data generators can be registered without accidentally using the ARM
compiler:

```cmake
rp2040_console_host_tool(
    NAME makedata HOST_CC /path/to/native-compiler-wrapper
    SOURCES tools/makedata.c
    INCLUDE_DIRS include
    COMPILE_OPTIONS -std=gnu89
    DEPENDS include/data-format.h
    # RUNNER /path/to/qemu-i386   # only when required by the host binary
)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated.c
    COMMAND ${makedata_COMMAND} ${CMAKE_CURRENT_BINARY_DIR}/generated.c
    DEPENDS ${makedata_DEPENDS} VERBATIM)
```

The helper exposes a build target `makedata`, command list `makedata_COMMAND`,
and dependency list `makedata_DEPENDS` (target plus binary file). Use that list
so a changed generator reruns its consumers; add relevant headers/data inputs. Choose
a host compiler/wrapper and runner matching the data format. A 64-bit host
`long` is not interchangeable with a 32-bit target `long`.
`tools/check-layout.py` compares `layout_*[sizeof(type)]` and
`offset_*[offsetof(type, field)+1]` probe symbols from separately compiled host
and target objects. Empty probes or differences fail. This checks sizes and
field offsets; endianness, bitfield encoding and semantics still need attention.

Volume manifests accept the original whitespace format and tab-separated
fields for paths containing spaces. Required files keep their declared target
names; parent directories are created, and missing files fail the build.
Parallel volume builds have independent staging directories. Manifest paths
cannot contain tabs, newlines, `#` or `|`; volume destinations are relative
paths without `..` components.

### Regression checks

With host C compiler, Python, dosfstools/mtools, SDK and ARM tools available:

```sh
sh tests/run.sh
cmake -S tests/integration -B build/integration -G Ninja -DPICO_SDK_PATH=...
cmake --build build/integration --parallel 4
cmake --build build/integration --target volume
```

The host checks cover 32-bit time boundaries, output-pointer overwrite guards,
terminal speed/flag state, rejected memory layouts, incompatible/empty data
probes, and renamed/nested volume files with spaces. The ARM fixture builds a
native host generator and two programs, shares a source between native and
legacy time targets, verifies native system struct ABIs, and checks `panic`
renames and quoted-header resolution in generated sources.

By default program termination still reboots. The restart-key wait polls
without blocking, so its existing timeout can expire. Optional soft reboot
is described below.
Physical USB, flash writes and long-game heap/stack peaks need board testing.

### Optional soft reboot

Enable it explicitly in the program manifest:

```cmake
rp2040_console_game(
    NAME mygame SOURCES ${SRCS}
    SOFT_REBOOT
    # All mutable program C units belong in SOURCES/GENERATED.
    HEAP_END 0x2003c000 STACK_RESERVE 24576
)
```

The default is still a hardware reboot. In soft mode, normal return from
`main()`, `exit()`, `_exit()` and `_Exit()` enter a session boundary. The
platform initializes USB, its alarms, storage and the clock once, then starts
new program sessions without deinitializing or reconnecting USB. After the
usual end-of-game key prompt, pending input is discarded and terminal modes
are restored. Session time continues and is persisted; each session gets a
new pid. A nonzero exit during the initial three-second grace period still
stops instead of looping on a startup error; a quick successful exit can
restart normally.

A compiler launcher retags only declared program `.data`/`.bss` objects. A
checked SDK linker override keeps those ranges within normal startup data/BSS
and exposes their original XIP initialization image. Before each call, only
program initialized globals and zero-initialized globals are restored. Platform
and USB globals remain live. A `setjmp` boundary unwinds exited program stack
frames; it does not reset the entire RAM or the C allocator's shared state.

Each session uses its own newlib reentrancy context and its own standard
streams. `atexit` handlers execute in reverse order, and normal exit flushes
and closes remaining program streams while the caller's stack is still alive.
`_exit`/`_Exit` skip handlers and discard buffered stdio output. Closing the
program's standard streams never tears down USB or the platform streams.
Newlib's reentrant malloc/calloc/realloc/free are wrapped to reclaim outstanding
program and libc allocations, including leaked objects, without reclaiming
platform allocations. Allocation tracking consumes additional heap space.
Process-global timezone/locale caches retain platform ownership; environment,
locale, terminal modes, umask and working directory are reset between sessions.
A strong `_sbrk` now enforces the manifest's actual `HEAP_END` in both modes.

**Supported profile:** single-threaded C terminal programs on RP2040, XIP,
GCC/newlib (validated with SDK 2.3.1, newlib 4.5.0). List every mutable program
translation unit in `SOURCES`/`GENERATED`; stateful precompiled libraries or
application units added through `target_sources` cannot be reset automatically.
C++, TLS, constructors, custom writable sections, core-1 work, and program
IRQ/timer/peripheral callbacks surviving exit are not supported. The build
rejects unsupported declared source types, common symbols and writable sections.
The launcher preserves an existing compiler launcher such as ccache.

Soft reboot is normal-termination cleanup, not recovery from memory corruption
or a crash. A libc exit originating in a precompiled library bypasses the
program-only macro and follows the hardware reboot path. A cleanup failure
falls back to hardware reboot. Physical cable/host disconnection cannot be
prevented; keeping the USB driver alive still requires a board test.

Run the lifecycle test with ARM tools and qemu-user available:

```sh
bash tests/soft/run.sh
```

It executes the actual ARM/newlib session implementation via a Linux syscall
bridge under `qemu-arm` (no Pico/USB emulation): 60 sessions exercise normal
return, exit, immediate exit, nested exits from handlers, program standard
stream closure, caller-stack-backed handlers/buffers, global restoration,
realloc failure, leaked allocations/streams, signals, timezone changes, platform
state preservation, stable heap high-water and enforced heap growth/shrink bounds.
The integration fixture also links the Pico backend and separated globals.

What a game needs on the machine doing the building: CMake, `gcc-arm-none-eabi`,
Pico SDK 2.3.1 or newer (`PICO_SDK_PATH`), and `dosfstools` plus `mtools` for the
volume. There is no download step: a build that quietly fetches its own platform
layer is a build whose result depends on what upstream looked like that day.

## What a game gets

**Startup.** The engine owns `main()`. It brings up stdio, the environment, the
clock and the volume, then calls the program's own main. See *The entry point*
below for why this is done by renaming rather than by wrapping, and why an
`__attribute__((constructor))` would not have run at all.

**Console.** USB CDC, with UART0 also enabled so a wired console works when USB
does not enumerate. Input arrives with the input processing a line discipline
used to do; output goes out whichever way the program chose to reach it.

**Terminal.** `<termios.h>`, `<termcap.h>`, `<sys/ioctl.h>`, `<sgtty.h>`,
`<curses.h>`. Termios is emulated and *acted upon*; termcap is answered from a
built-in description; `ioctl()` answers `TIOCGWINSZ` and refuses the rest.

**Files.** A POSIX descriptor layer (`open`, `read`, `write`, `lseek`, `close`,
`creat`, `stat`, `fstat`, `unlink`, `link`, `access`, `chdir`, `flock`) over
FatFs, backed by a region of the program's own flash, with an SD card as a
fallback. See *Filesystem*.

**Environment.** `getenv`, `setenv`, `unsetenv` over a table the manifest
supplies. There is no `environ` here, so a program that reads its environment
would otherwise find nothing, and several of them treat that as fatal.

**Clock.** `time()` on a real time base, set at boot from the build date and
offered for correction on the console. Nothing on this board keeps time, and a
program that stamps score files with 1970 is worse than useless.

**Entropy.** `getentropy()` from the ring oscillator. This is the difference
between a game that plays a different dungeon each boot and one that replays the
same dungeon forever, because the fallback seed is usually built from `time()`
and `getpid()`, both of which are fixed here.

**Exit.** `_exit()` reboots. A game that has not freed what it allocated, and
there is no `exec` to replace it with, restarts cleanly in a second; a boot grace
period stops a program that fails immediately from rebooting in a loop.

**Diagnostics.** Startup stages printed as they are reached, and a `TRACE()`
macro for file and console traffic that is off by default. Genuine setup
failures - no volume, not a FAT volume, mount refused - are reported
unconditionally, because a game that cannot find its data files should say so.

## How the program stays unmodified

The tempting way to port is to edit the program: guard the awkward calls, rename
what clashes, stub what is missing. Every one of those edits is a conflict waiting
in the next upstream merge, and they add up to hundreds of lines. What happens
instead:

**A prefix header, force-included into the program's translation units**
(`include/rp2040-console.h`). It pre-includes the C library headers while their
names are still unshadowed, and then defines the handful of things that have to
be different at preprocessing time: `getchar`, `main`, and the two or three names
a program defines that collide with something in newlib. Because it runs before
the program includes anything, a rename defined in it cannot reach into a libc
declaration and quietly rewrite it.

**The headers this toolchain does not have.** `<termcap.h>`, `<curses.h>`,
`<sys/ioctl.h>` and the `<sys/termios.h>` that newlib's `<termios.h>` is a
one-line include of. They are found through the include path, so `#include
<termcap.h>` works the way the program expects. Nothing here shadows a header
that already works: the moment this directory competes with a real newlib header,
bugs stop being about the port and start being about which of two headers won.

**The libc calls that are missing, defined rather than stubbed.** `flock()` that
always grants, because there is one program and it is not a lie; `ioctl()` that
answers the one request with a true answer; and the process surface left absent on
purpose - newlib declares `fork()` but it resolves through to `_fork_r`, which
nothing in this toolchain defines, so a program that reaches for a subprocess stops
at link time where somebody will notice rather than running at runtime with a
child process that cannot exist.

**`_write()` routing descriptors 1 and 2 to the console, and stdio unbuffered.**
The SDK makes `putchar()`, `puts()` and `printf()` work by `--wrap`ping them onto
its stdio drivers. `fputs()`, `fprintf()` and `fwrite()` are not wrapped: they
stay inside newlib and reach `_write()` with fd 1 or 2, where a check for "not an
open file" would silently discard them while reporting success. That is a whole
class of bug - output that just never appears, in a program that prints plenty of
other things - and one fix in the syscall layer removes it for every program.
stdout is unbuffered so the wrapped path and the `_write` path interleave in the
order the program wrote them.

### The entry point

`--wrap=main` does not work, and it is worth knowing why, because it looks like
the obvious mechanism and fails without a word. `--wrap` redirects a reference
only where that reference would otherwise be resolved out of an archive; a symbol
defined by an object file handed to the linker directly is used as-is. The C
library's `putchar` and `printf` qualify, which is exactly why the SDK's own
wrapping of them works. A program's `main.c` does not: it is one of the objects in
the link, so the C runtime's call to `main` reaches it untouched, `__wrap_main` is
included and never called, and the program runs with no console and no filesystem.
Nothing complains.

So the prefix header renames the program's `main` to
`rp2040_console_game_main` and the engine provides the real `main()`. It fails
loudly in the one case that matters: a program with no `main` to rename leaves an
undefined reference to `rp2040_console_game_main`.

There is no command line on this device, and the C runtime leaves whatever
initialisation put in r0 and r1 in place of argc and argv - so a program that
reads `argv[0]` is following a wild pointer. The engine passes one argument, the
program's own name, which is all a program can sensibly want, and which several
of them need to date their own save files.

Constructors are not used either, and could not be: this SDK's startup calls
`runtime_init`, `main` and `exit`, and never walks `.init_array` -
`__libc_init_array` is not in the link at all.

## Scope

Fits: a program that drives a terminal with escape sequences or termcap, reads
characters from stdin, opens flat files by name, and wants no more RAM than
roughly 200KB including its heap. Terminal size is fixed at 80x24. The host
terminal is assumed to be VT100/xterm-compatible, which covers every terminal
emulator anyone will actually attach.

Does not fit:

- **ncurses.** There is no ncurses or PDCurses for newlib on RP2040, and writing
  one is a different project. A program must be doing its own screen control, or
  be willing to use the termcap surface provided here.
- **Processes, signals, threads, `exec`, dynamic loading.** Some of this is
  provided as a "no" that programs handle correctly - `fork()` is absent so the
  caller learns at link time - and some of it cannot be.
- **Programs that need more than the heap.** See the memory map.
- **Anything expecting a real line discipline.** The console echoes and translates
  what the program's termios settings say it should, which is enough for a program
  that reads a line and then turns echo off to read keys. It is not a terminal
  emulator's worth of behaviour: no job control, no flow control, no signals from
  the break key.

## Filesystem

FatFs, over a region of the same flash the program runs in, authored on the host.

FatFs comes with the Pico SDK (it ships inside TinyUSB), so it is not a new
dependency. The volume is built on the host by `tools/mkvolume.sh` with
`mkfs.vfat` and `mtools`, checked with `fsck.vfat`, and flashed as a UF2 at a
fixed offset. The device never formats anything, so there is no formatting code
in the firmware, and the geometry is decided by a program that can see the whole
chip rather than by the board.

FAT is chosen over a flash-friendly log filesystem such as littlefs for reasons
worth stating plainly rather than pretending are obvious:

- The content is a handful of flat ASCII files that a game reads and appends to.
  Nothing needs POSIX semantics, and nothing needs to survive the power being cut
  mid-write.
- The volume is authored and checked with tools that already exist, and can be
  poked at afterwards with `mdir` - which matters more for debugging a game's data
  files than any property of a flash filesystem.
- The same image works on an SD card, readable by any operating system.

**The honest cost: FAT is not power-loss safe.** A write interrupted by a reset
can lose a directory entry or leave a chain inconsistent, and FatFs has no journal
to recover from. For a score file that is an acceptable loss, and it is what a
player experiences as "my last game did not make the top ten". A reset *between*
writes is safe, which is the normal case, since a game writes on quit and on save.
If that tradeoff is wrong for a game, the seam to replace it is small: the POSIX
layer in `src/fileio.c` and the five `diskio.h` functions - littlefs would occupy
the same two places.

Writes are read-modify-write over 4KB erase blocks (`src/flash_fs.c`), with
interrupts off during the flash procedure, because executing from flash while it
is being programmed faults. The volume needs a 2MB flash chip: the program lives
below 1.5MB and the volume above it.

## Memory map

    0x10000000  program: text and constant tables, execute in place
      ...       end of program text
    0x10180000  the FAT volume (RP2040_CONSOLE_FS_OFFSET), 512KB by default
      ...
    0x20000000  SRAM: .data, .bss
      ...       heap grows up from the end of .bss to HEAP_END
      ...       free
      top       stack, growing down

`HEAP_END` is where a program's malloc budget is set, and it is the number to
adjust when a game runs out of memory: the SDK's linker script grows the heap from
the end of `.bss` to `HEAP_LIMIT` and reads nothing else, so `PICO_HEAP_SIZE` has
no effect here. The remaining space between the heap and the top of RAM belongs to
the stack, so setting `HEAP_END` at the very top leaves a program with no stack and
a failure mode that looks like random corruption.

A program's text and tables cost nothing in RAM. When measuring, watch the RAM
figures: a program that grows 40KB and still runs has not necessarily got any
nearer to the limit.

## Building, flashing, watching

    cmake -S . -B build -DPICO_SDK_PATH=... -DRP2040_CONSOLE_PATH=...
    make -C build -j                    # hack.uf2 / mygame.uf2
    make -C build volume                # mygame-volume.uf2

Both UF2s are flashed by dropping the board in BOOTSEL mode. They do not overlap
and the order does not matter. `mkvolume.sh` verifies the blocks it emits and the
volume it built, and fails rather than shipping something `fsck` disagrees with.

Rebooting drops USB, so a plain `screen` session ends when the program exits:

    while true; do screen /dev/ttyACM0; sleep 1; done

On macOS the device is `/dev/cu.usbmodem*`. `picotool` for inspecting images ends
up at `<build>/_deps/picotool/picotool` when it is not installed system-wide.

## restoHack: checkout to UF2

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

## Limits worth knowing

These are the ones that cost time to find, in rough order of how much they cost.

- **The SDK's `--wrap` is both how stdio works and a trap.** Only `putchar`,
  `puts`, `printf`, `vprintf` and `getchar` are wrapped. Anything that reaches
  `_write()` - `fputs`, `fprintf`, `fwrite` - needed the fd 1/2 routing described
  above. Expect this pattern in any new code: a libc function that links, returns
  success and silently does nothing.
- **`getchar` is claimed by the SDK**, so the input translation happens in the
  prefix header rather than by supplying a better `getchar()`.
- **There is no EOF on the console.** Nothing hangs up a modem, so a program
  waiting for a key waits forever, which is correct. `read()` on fd 0 blocks for
  the first byte and then returns what is waiting, at most a line.
- **Backspace is normalised to BS (0x08)** and `VERASE` is reported as BS, because
  hosts send DEL and programs hard-code BS and `VERASE` in different places; both
  have to agree or erase works in one prompt and not the next.
- **`termcap`'s `ospeed` is a padding index, not a baud rate.** It is reported as
  `B9600`'s BSD code. A program that indexes a table by it will read out of bounds
  if given a number like 115200.
- **`ffconf.h` uses unconditional `#define`s**, so `-D` overrides of FatFs options
  are silently no-ops. Change the header or provide your own on the include path.
- **The program is not allowed to buffer the console.** `setbuf()` and
  `setvbuf()` are refused for stdout and stderr, because with two writers - one
  through stdio, one around it - buffering reorders output and loses up to a buffer
  full of it on a reset. A program that asks for buffering gets its output no later
  than it asked, which is the only promise it can be making.
- **A stale CMake cache survives removing the call that set it.** Options like
  `pico_enable_stdio_uart()` need to be set explicitly to 0 rather than simply not
  set, if the build directory has ever seen them on.

## Licence

3-clause BSD
