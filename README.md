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

An RP2040 has 264KB of SRAM and 2MB of flash mapped at `0x10000000`. Text
executes in place through the XIP cache, so only writable data costs RAM. A
game from 1985 that is one large contiguous image - a quarter megabyte of text
and constant tables, plus a habit of static arrays - will not fit a user
process on a small unix with kernel/user split and a per-process cap, however
that split is tuned, and will fit comfortably here: the tables stay in flash and
the program gets essentially all 264KB.

The cost is that nothing is there when you arrive. No terminal driver, no
filesystem, no process table, no clock, no libc that has ever heard of a device
it cannot write to. This engine is what fills in those gaps.

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
| `DEFINES` | any extra preprocessor definitions the program wants |
| `RENAMES` | `name=replacement`, for a name in the program that clashes with something else in the link |

Cache variables: `RP2040_CONSOLE_FS_OFFSET` (default `0x180000`),
`RP2040_CONSOLE_FS_SIZE` (default 512KB), `RP2040_CONSOLE_COLS`,
`RP2040_CONSOLE_ROWS`, and the options `RP2040_CONSOLE_IO_TRACE`,
`RP2040_CONSOLE_UART_STDIN` and `RP2040_CONSOLE_CLOCK_PROMPT`.

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
