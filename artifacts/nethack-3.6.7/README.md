# NetHack 3.6.7 SRAM trial

**Experimental: the new single-checkpoint format builds and passes regression
tests, but its many-level Pico save/restore test is still pending.**

An opt-in combined patch for a standard RP2040 Pico with 2 MiB flash and no
SD card. `combined-sram.patch` patches the console recipe and its upstream
NetHack patch. `build.py` applies it in a private snapshot of the committed
console checkout; it leaves the checkout and supplied NetHack sources untouched.
The package targets console revision `e87f816` and NetHack revision
`ed600d9f0f3c37677418f0150f59363ca641f3dc` (3.6.7).

The changes close quest decoders between messages, load quest indexes earlier
on restore, bound deflate dictionaries to 4 KiB, pack overview flags, use
16-bit internal display glyphs and byte-sized room references, allocate immutable
special-level operands in arenas, pool 64-byte allocations, and reserve 20 KiB
for the stack. DECgraphics remains available. LTO is not enabled.

Saves use packed overview flags and 13-byte room records; pointers and room
padding are omitted. This format deliberately rejects previous saves and bones
(version incarnation `0x03060781`). Start a new game when switching to this
package. Compressed savings depend on the level; the old zero-filled pointer
fields already compressed well.

## One saved game

The game uses its existing compressed level files as the saved dungeon. The
single companion, `save/0game`, contains the current level, hero, inventory and
global state; root files `0game.<ledger>` contain the other visited levels.
Restore verifies their sizes and CRC32 checksums and reads them in place. It
never reconstructs the entire dungeon alongside a combined save.

Saving writes `0game.tmp` first and publishes the completed companion by rename.
An uncommitted temporary file is not offered for restore. Startup clears orphaned
level files when no committed checkpoint exists. A successful restore consumes
the companion, including in wizard mode; the level files then back the running
game. There is one saved game, shared by all character names.

Back up or transfer the **whole data volume**, not just `save/0game`. This format
rejects older saves/bones. FAT writes remain vulnerable to interrupted power;
temporary-file publication and checksums do not provide filesystem transactions.

## Build

Use the ordinary console prerequisites: Pico SDK 2.3.1 with TinyUSB, ARM GNU
Toolchain/newlib, CMake, Ninja, Python 3, Git, patch, Bison, Flex, dosfstools and
mtools. Put their commands on `PATH` and set `PICO_SDK_PATH`. If picotool is
installed separately, set `PICOTOOL` to its executable.

On x86-64 Linux with the 32-bit host compiler dependencies installed:

```sh
python3 artifacts/nethack-3.6.7/build.py --source /path/to/NetHack --jobs 4
```

On Apple Silicon or another host without a native ILP32 compiler, install QEMU
with `qemu-system-arm` and use the included ARM/newlib generator runner:

```sh
python3 artifacts/nethack-3.6.7/build.py --qemu-generators \
  --source /path/to/NetHack --jobs 4
```

Omit `--source` to download the pinned NetHack checkout. Other recipe arguments
such as `--nethack-options 'symset:DECgraphics'` are forwarded. `--build-dir`
sets the package output directory, defaulting to `build/nethack367-sram`.

Flash both `game/hardware/nethack.uf2` and
`game/hardware/nethack-volume.uf2` below that directory, following the console's
BOOTSEL instructions. Flashing the volume replaces its saves and scores.

`--soft-reboot` selects `game/soft` instead. Pool state is platform-owned and
slots are released by the session tracker before program globals reset.
Soft reboot retains a tracking allocation for each live allocation, so it has
higher SRAM costs than the measured hardware-reboot build.

## Regression checks

The build checks all archive entries with zlib and the target inflater, compares
host/target data layouts, validates the FAT image, and checks firmware/SRAM
bounds. For ARM/newlib compact-save and allocator ownership tests, including
all 120 special-level operand programs and 100 simulated soft sessions, run with QEMU and a native C compiler installed:

```sh
python3 artifacts/nethack-3.6.7/test.py --build-dir build/nethack367-sram
```

For repeatable Pico measurements, build a separate image with
`--sram-diagnostics`. It enables a fixed-seed wizard character, serial allocation
and stack/guard readings, `#sramtrial` (Ludios), `#sramstack` (travel probes), and
`#srampack` (150 carried test objects), `#sramtour` (depths 1–48, then Ludios),
and `#stats`. This option changes gameplay setup and is intended for testing only.

The measurements establish tested headroom, not safety for every possible
monster population or a completed ascension. See `RESULTS.md` for the packaged
build's validation and limits. NetHack and the console retain their respective
licenses; retain NetHack's license and this patch when distributing modified code.

[RESUME.md](RESUME.md) records the current checkpoint and exact next validation steps.
