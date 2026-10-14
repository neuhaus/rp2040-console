# NetHack 3.6.7 on a 2 MB Pico

> [!WARNING]
> While it runs already, you will run out of flash memory during an ascension run! :boom: Work in progress.

Source: [NetHack/NetHack](https://github.com/NetHack/NetHack), tag
`NetHack-3.6.7_Released`, commit
`ed600d9f0f3c37677418f0150f59363ca641f3dc`. The recipe applies configuration
changes in a private generated source tree and adds the CMake manifest,
compatibility header, platform adapters, the Tamp and uzlib codecs and a
replacement data librarian. Upstream game C files and the supplied Git checkout
remain unchanged. Keep the upstream `dat/license` with distributions; it is
included in the FAT image.

## Why the data is compressed

NetHack 3.6.7's program is larger than 3.1.3's, and so is its data. Uncompressed,
the firmware plus the data files do not fit on the Pico's 2 MB of flash:
dropping every optional file still leaves more data than the chip has room for.
So the read-only data is compressed, which is what makes the port fit:

- Every file NetHack reads through its data librarian (`dungeon`, the level
  descriptions, `quest.dat`, the encyclopedia `data`, `symbols`, rumors,
  oracles, the help screens, `bogusmon`/`engrave`/`epitaph`, `options`) is
  compressed with raw deflate, decoded on the board by
  [uzlib](https://github.com/micropython/micropython/tree/master/lib/uzlib)
  (a few KB of code and a 4 KB window per open file).  Deflate beats the Tamp
  codec used for the level and save files by 15-25% on this data: it adds
  Huffman coding on top of LZ77, which Tamp lacks.
- The files are collected into a single archive, `nhdata`, rather than left
  separate. A FAT volume rounds each file up to a whole cluster; in one file
  they cost one file's worth of slack.
- The window is a fixed 4 KB, the same as the level and save codec. Every
  entry is compressed with it, so the decoder allocates one 4 KB dictionary
  per open file. A wider window would compress a little better (`data` is
  10 KB smaller at 8 KB) but holds more RAM while a data file is open.
- The recipe defines `DLB` and replaces `src/dlb.c`, so NetHack's existing
  `dlb_fopen`/`dlb_fread`/`dlb_fseek` calls land on a streaming inflate.
  Seeking is emulated by decoding and discarding; a backward seek restarts the
  entry. NetHack's access patterns are almost entirely forward scans, and the
  files are small enough that this is cheaper than the flash it saves.

`build.py` packs the archive with the host's zlib and then decodes it back with
the same uzlib the board runs (`port/host/nhdata_check.c`), so a decoder
mismatch fails the build rather than the game.

The 138 files packed into `nhdata` total **939,508 bytes (about 918 KB)**
uncompressed. Raw deflate brings them to about **305 KB** (a ratio near 0.33),
plus a 3,876-byte header and directory. Only the compressed form fits:
uncompressed as separate files the same data would not fit on the volume
alongside the firmware, and even a single uncompressed archive would be larger
than what is left over after the firmware.

`tribute` is the one data file left out: its compressed form is the difference
between fitting and not, and it is the least of what the library carries (the
passages shown when reading a novel, and the death quotes). The game handles
its absence the way it handles any missing optional data file.

## Saving levels

NetHack keeps one file per level the player has left and re-reads it on return.
On a volume this size that is the binding constraint on how deep a game can go,
so level files and save files are compressed too. They use Tamp rather than the
data library's deflate: a level file is written on the board while playing, and
Tamp's compressor is far smaller than a deflate encoder. The port keeps
upstream's `ZEROCOMP` plumbing -- its option, its `SFI1_ZEROCOMP` savefile flag
and its `bufon`/`bufoff` raw-front-matter protocol -- and swaps only the codec
for Tamp, so `src/save.c` and `src/restore.c` change only where they do the
byte I/O.

Level files drop from about 20-30 KB to about 1.4-8 KB (roughly 8x), so about
**55 levels** fit in the free space instead of about four. Without this the
volume filled after a few levels, the level write failed as a short write, and
NetHack panicked with an empty reason line (the panic save could not be written
either, so `NH_abort()` ran before the reason was printed).

Save/restore has been exercised on hardware across a power cycle, and a game has
descended past dungeon level 10 with no volume trouble. When the game ends it
prints how many bytes are free on the volume, so the fit of a save can be judged
without a host.

## Build

Install the [common prerequisites](../README.md) plus the 32-bit host compiler
and parser generators. On an x86-64 Debian/Ubuntu host:

```sh
sudo apt install gcc-multilib libc6-dev-i386 bison flex patch
git clone https://github.com/NetHack/NetHack.git nethack
git -C nethack checkout NetHack-3.6.7_Released
python3 applications/nethack-3.6.7/build.py --source ./nethack
```

Run the last command from the console repository root with `PICO_SDK_PATH` set.
Without `--source`, the script clones upstream into its build directory. It always
archives the pinned commit, regardless of the checkout's current branch or edits.
`--build-dir /absolute/path` selects a different private output directory;
`--jobs 4` controls ARM build parallelism. A nonempty unrelated output directory
is rejected. The recipe owns and regenerates its `source/` tree; do not edit it.
Edit this recipe's `port/` files instead.

The default host command is `gcc -m32`. The datagenerators must use 32-bit `long`
and compatible structure layouts; a native 64-bit build is insufficient. On other
hosts, supply a suitable compiler/wrapper and runner, for example:

```sh
export HOST_CC=i686-linux-gnu-gcc
export HOST_CFLAGS=''
export HOST_RUNNER='qemu-i386 -L /path/to/i386/sysroot'
python3 applications/nethack-3.6.7/build.py --source ./nethack
```

`HOST_CC`, `HOST_CFLAGS` and `HOST_RUNNER` are shell-style argument lists, parsed
without shell execution. `HOST_LD_LIBRARY_PATH` optionally sets library lookup
for executed host generators. The ARM compiler remains `arm-none-eabi-gcc`.

Two compression libraries are vendored, both fetched with NetHack and needing no
network access of their own: Tamp for the level and save files (`port/tamp/`,
Apache 2.0, `port/tamp/LICENSE.txt`) and uzlib for the data library
(`port/uzlib/`, zlib licence). `build.py` packs `nhdata` with the host's zlib,
then builds and runs `port/host/nhdata_check.c`, which decodes every entry with
the same uzlib the board runs and compares it with its source, so a bad archive
fails the build rather than the board.

### Startup options

The image can carry a `NETHACKOPTIONS` value, for settings that belong to a
build rather than to the source. Pass them with `--nethack-options`:

```sh
python3 applications/nethack-3.6.7/build.py \
    --nethack-options 'symset:DECgraphics,number_pad:-1'
```

The value is comma-separated, exactly as NetHack's `NETHACKOPTIONS` is. The
image already turns `color` on: upstream leaves colour off in a Unix tty build,
because it expects terminals of every stripe, while this board always drives a
colour-capable ANSI emulator. Anything passed here is appended after `color`,
so a build can still ask for `!color`. Two are often asked for:

- `symset:DECgraphics` selects the DEC line-drawing symbol set from the
  `symbols` file. This is the set behind the old `DECgraphics` option; in 3.6.7
  that option is config-file-only and no longer appears in the `O` menu, where
  the equivalent is the `symset` compound option.
- `number_pad:-1` is NetHack's "German keyboard" setting: vi-keys with the `y`
  and `z` directions swapped, for a QWERTZ layout.

These two are personal choices, so the recipe ships neither and a default build
leaves them at upstream's values. `build.py` always passes the option through
(empty when the flag is absent), so a later build without the flag is clean
again rather than keeping the previous value.

### Startup banner and saved games

Before the game takes the screen, the image prints a one-line banner and lists
the saved games in `save/`, each with its size and the date the session clock
gave it:

```
RP2040 Console ready.
Saved games in save/:
  1000wizard               390368 bytes  2026-10-13 18:22
```

The banner is not a literal in the port. It is baked into the image as the
`BANNER` environment variable, set at build time from `--banner` or `$BANNER`
(the default is the line above); `--banner ''` drops it. The game prints its
own NetHack version banner, so this line deliberately does not repeat it. The
listing needs a "last saved" date, which FatFs cannot supply -- it is built
timestamp-off and stamps every file it writes 2022 -- so the engine remembers
the session time when it closes a file and reads it back for the listing; a
file it never wrote is shown without a date. Nothing clears the screen here, so
the lines stay in the terminal's scrollback once the game draws over them.

## Outputs and checks

Run the save codec regression from the console repository root with a local
NetHack checkout and a native C compiler:

```sh
python3 tests/nethack_codec.py --source /path/to/NetHack --sanitize
```

Omit `--sanitize` if the compiler does not support sanitizers.

Default outputs are under `build/nethack-3.6.7/hardware/`:

- `nethack.uf2`: XIP firmware below flash offset `0x17D000` (about 1.49 MB).
- `nethack-volume.uf2`: 524 KB FAT12 volume at `0x17D000`, ending at 2 MB.
- `nethack.elf`, `nethack.bin` and `nethack-volume.img`: inspection artifacts.

The volume holds `nhdata` (the compressed library, which carries every data
file including `symbols`), empty `record`,
`perm`, `logfile` and `xlogfile` (NetHack locks and appends to all four, and
`lock_file()` opens the file itself), and a `save` directory. About 330 KB of
the 524 KB is used, leaving about **194 KB** for the levels a game has visited.
The volume is FAT12 with 512-byte clusters, so the many small level files cost
almost no slack.

The split between firmware and volume is set by `FS_OFFSET`/`FS_SIZE` in the
manifest. The volume starts at `0x17D000`, just above the firmware (about 5 KB
to spare); the volume's free space is what a long game has for level files, so
raise `FS_OFFSET` only as far as the savegame budget allows.

The build generates all game data, checks host/ARM sizes and offsets, validates
firmware/heap/stack bounds, checks FAT with fsck and verifies UF2 addresses and
family IDs. `HEAP_END=0x2003c000` leaves 24 KB above the heap for stack/SDK
scratch; actual stack peaks are unmeasured. NetHack's own static data is large
(about 148 KB), so the heap budget (about 92 KB) is smaller than on 3.1.3;
peak heap use is unmeasured. The two biggest read-only tables, `mons[]` (~21 KB)
and `extcmdlist[]` (~3 KB), are declared `const` by the patch so the toolchain
places them in flash (XIP) rather than SRAM; only tables the game actually
writes (`objects[]`, `artilist[]`, options) stay in RAM. The manifest uses
`LEGACY_C`, `LEGACY_TIME32` and `RENAMES panic=nh_panic`.

The configuration enables `NO_FILE_LINKS` with `LOCKDIR="/"`, so NetHack
creates lock files exclusively instead of requesting hard links unsupported by
FAT. The image check rejects a compiled lock implementation that calls `link()`.
`NETWORK`, `TERMINFO` and `POSIX_JOB_CONTROL` are off, `COMPRESS`/`INSURANCE`
are off, `HACKDIR` is `/`, and `SYSCF` is off, so the game needs no `sysconf`
file. `sys_random_seed()` uses the platform's `getentropy()` instead of
`/dev/urandom`. No `USER` or `LOGNAME` is set in the environment, so the game
asks for a character name rather than defaulting to one.

### Host and target must preprocess alike

The data tables NetHack indexes (`objects[]`, `mons[]`) are compiled from the
same sources by the program and by the host generators. `makedefs` writes the
name-to-index headers (`onames.h`, `pm.h`) from a host build, so any feature
toggle that adds or removes a table entry has to be seen by *both*, or the
indices disagree and the program indexes the wrong entry. Concretely, `NOMAIL`
removes the mail scroll in `objects.c` and the mail daemon in `monst.c`; if the
host generator keeps them, every later index shifts by one and the game aborts
with `init-prob error for class 13`.

Because of that, the toggles shared with the host live in `port/compat.h`,
which the recipe force-includes into the program and its host generators alike,
rather than in the manifest's `DEFINES` (which reaches only the program). Add a
new toggle that changes a data table in `compat.h`, not in `CMakeLists.txt`, and
check it the same way: compile `objects.c` for the host and for ARM and diff the
per-index class and probability columns.

For optional application restart while preserving the USB runtime:

```sh
python3 applications/nethack-3.6.7/build.py --source ./nethack --soft-reboot
```

Outputs then go to `build/nethack-3.6.7/soft/` with the same filenames and flash
layout. Hardware reboot is the default and is what the recipe ships: soft reboot
adds about 1 KB of static RAM and, heavier over a long game, a bookkeeping
allocation for every `malloc` plus a fixed exit-handler table. Read the
[supported single-threaded C profile](../../INTERNALS.md#optional-soft-reboot)
before enabling soft reboot.

This recipe builds ARM images and runs the host data-library round-trip check.
It has been run on a Pico: it boots, creates a character, plays, and
save/restore across a power cycle works -- a game has reached dungeon level 10
with the level/save volume still comfortable. Peak heap use over a long game
remains unmeasured; the heap budget is the tightest part of the port.
