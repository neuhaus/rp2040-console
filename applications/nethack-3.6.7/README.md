# NetHack 3.6.7 on a 2 MB Pico

Source: [NetHack/NetHack](https://github.com/NetHack/NetHack), tag
`NetHack-3.6.7_Released`, commit
`ed600d9f0f3c37677418f0150f59363ca641f3dc`. The recipe applies configuration
changes in a private generated source tree and adds the CMake manifest,
compatibility header, platform adapters, Tamp decompressor and a replacement
data librarian. Upstream game C files and the supplied Git checkout remain
unchanged. Keep the upstream `dat/license` with distributions; it is included
in the FAT image.

## Why the data is compressed

NetHack 3.6.7's program is larger than 3.1.3's, and so is its data. Uncompressed,
the firmware plus the data files do not fit on the Pico's 2 MB of flash:
dropping every optional file still leaves more data than the chip has room for.
So the read-only data is compressed, which is what makes the port fit:

- Every file NetHack reads through its data librarian (`dungeon`, the level
  files, `quest.dat`, the encyclopedia `data`, rumors, oracles, the help
  screens, `bogusmon`/`engrave`/`epitaph`, `options`) is compressed with
  [Tamp](https://github.com/BrianPugh/tamp), whose decoder is about 2 KB of
  code and needs only a 1-4 KB window per open file.
- The files are collected into a single archive, `nhdata`, rather than left
  separate. A FAT volume with a 2 KB cluster size would otherwise round each of
  the small files up to a whole cluster; in one file they cost one file's worth
  of slack.
- The recipe defines `DLB` and replaces `src/dlb.c`, so NetHack's existing
  `dlb_fopen`/`dlb_fread`/`dlb_fseek` calls land on a Tamp streaming reader.
  Seeking is emulated by decoding and discarding; a backward seek restarts the
  entry. NetHack's access patterns are almost entirely forward scans, and the
  files are small enough that this is cheaper than the flash it saves.

The 137 files packed into `nhdata` total **922,752 bytes (about 901 KB)**
uncompressed. Tamp brings them to about **353 KB** (a ratio near 0.39), plus a
3,848-byte header and directory. Only the compressed form fits: uncompressed as
separate files the same data would occupy about **1,054,720 bytes (about
1.0 MB)** on the volume once each file is rounded up to a 2 KB cluster
(131,968 bytes of slack), and even a single uncompressed archive would be
larger than what is left over after the firmware.

`tribute` is the one data file left out: its compressed form is the difference
between fitting and not, and it is the least of what the library carries (the
passages shown when reading a novel, and the death quotes). The game handles
its absence the way it handles any missing optional data file.

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

The Tamp tools are built from the vendored library under `port/tamp/` (Apache
2.0, `port/tamp/LICENSE.txt`); no network access beyond fetching NetHack is
needed. `tamp_pack` builds the archive and `tamp_check` decompresses every entry
back and compares it with its source, so a bad archive fails the build rather
than the board.

### Startup options

The image can carry a `NETHACKOPTIONS` value, for settings that belong to a
build rather than to the source. Pass them with `--nethack-options`:

```sh
python3 applications/nethack-3.6.7/build.py \
    --nethack-options 'symset:DECgraphics,number_pad:-1'
```

The value is comma-separated, exactly as NetHack's `NETHACKOPTIONS` is. Two it
is often asked for:

- `symset:DECgraphics` selects the DEC line-drawing symbol set from the
  `symbols` file. This is the set behind the old `DECgraphics` option; in 3.6.7
  that option is config-file-only and no longer appears in the `O` menu, where
  the equivalent is the `symset` compound option.
- `number_pad:-1` is NetHack's "German keyboard" setting: vi-keys with the `y`
  and `z` directions swapped, for a QWERTZ layout.

These are personal choices, so the recipe ships none of them and a default
build has upstream's defaults. `build.py` always passes the option through
(empty when the flag is absent), so a later build without the flag is clean
again rather than keeping the previous value.

## Outputs and checks

Default outputs are under `build/nethack-3.6.7/hardware/`:

- `nethack.uf2`: XIP firmware below flash offset `0x184000` (about 1.55 MB).
- `nethack-volume.uf2`: 496 KB FAT volume at `0x184000`, ending at 2 MB.
- `nethack.elf`, `nethack.bin` and `nethack-volume.img`: inspection artifacts.

The volume holds `nhdata` (the compressed library), `symbols` (the one data file
NetHack opens through plain stdio rather than the librarian), empty `record`,
`perm`, `logfile` and `xlogfile` (NetHack locks and appends to all four, and
`lock_file()` opens the file itself), and a `save` directory. About 380 KB of
the 496 KB is used, leaving the rest for save files; how much a full game needs
is unmeasured.

The build generates all game data, checks host/ARM sizes and offsets, validates
firmware/heap/stack bounds, checks FAT with fsck and verifies UF2 addresses and
family IDs. `HEAP_END=0x2003c000` leaves 24 KB above the heap for stack/SDK
scratch; actual stack peaks are unmeasured. NetHack's own static data is large
(about 171 KB, most of it the mutable monster and object tables), so the heap
budget is smaller than on 3.1.3; peak heap use is unmeasured. The manifest uses
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
layout. Hardware reboot is the default. Read the
[supported single-threaded C profile](../../INTERNALS.md#optional-soft-reboot)
before enabling soft reboot.

This recipe builds ARM images and runs the host data-library round-trip check.
It has been run on a Pico: it boots, creates a character and plays. Save/restore
across power cycles and peak heap use over a long game remain unmeasured; the
heap budget is the tightest part of the port.
