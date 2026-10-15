# NetHack 3.6.7 port internals

For build, configuration and flashing instructions, see [README.md](README.md).

The build pins NetHack revision
`ed600d9f0f3c37677418f0150f59363ca641f3dc` (3.6.7).

## Memory and save format

The changes close quest decoders between messages, load quest indexes earlier
on restore, bound deflate dictionaries to 4 KiB, pack overview flags, use
16-bit internal display glyphs and byte-sized room references, allocate immutable
special-level operands in arenas, pool 64-byte allocations, and reserve 20 KiB
for the stack. DECgraphics remains available. LTO is not enabled.

Saves use packed overview flags and 13-byte room records; pointers and room
padding are omitted. This format deliberately rejects previous saves and bones
(version incarnation `0x03060781`). Start a new game when switching to this
port. Compressed savings depend on the level; the old zero-filled pointer
fields already compressed well.

## One saved game

The game uses its existing compressed level files as the saved dungeon. The
single companion, `save/0game`, contains the current level, hero, inventory and
global state; root files `0game.<ledger>` contain the other visited levels.
Restore verifies their sizes and CRC32 checksums and reads them in place. It
never reconstructs the entire dungeon alongside a combined save.

Saving writes `0game.tmp` first and publishes the completed companion by rename.
An uncommitted temporary file is not offered for restore. Startup clears orphaned
level files when no committed checkpoint exists. After resuming, the checkpoint
companion is deleted; save again to create a new one. This also applies in
wizard mode. The level files continue to back the running game. There is one
saved game, shared by all character names. This implementation choice simplifies
checkpoint ownership.

Use `S` to save. A resumed checkpoint is no longer available until the next
save; save again before disconnecting. Back up or transfer the **whole data
volume**, not just `save/0game`. This format
rejects older saves/bones. FAT writes remain vulnerable to interrupted power;
temporary-file publication and checksums do not provide filesystem transactions.

## Build isolation and restart mode

`build.sh` configures the host generator mode and invokes `port/build.py`.
The Python backend handles the build recipe, binary data packing and image
validation. It archives the pinned upstream Git commit into an owned `source/`
tree, applies `port/config.patch` and copies the port adapters into that tree.
The supplied NetHack checkout remains untouched. Each build regenerates this
source tree; customize the application files rather than generated outputs.

`--soft-reboot` selects `soft/` instead of `hardware/`. Pool state is
platform-owned and slots are released by the session tracker before program
globals reset. Soft reboot retains a tracking allocation for each live
allocation, so it has higher SRAM costs than the measured hardware-reboot build.
Its physical USB lifecycle still needs validation.

`--sram-diagnostics` enables the deterministic wizard workload and serial
measurements; leave it off for ordinary play. For a separately installed
picotool, set `PICOTOOL` to its executable.

## Regression checks

The build checks all archive entries with zlib and the target inflater, compares
host/target data layouts, validates the FAT image, and checks firmware/SRAM
bounds. For ARM/newlib compact-save and allocator ownership tests, including
all 120 special-level operand programs and 100 simulated soft sessions, run
with QEMU and a native C compiler installed:

```sh
python3 applications/nethack-3.6.7/tests/test.py --build-dir build/nethack-3.6.7
```

For repeatable Pico measurements, build a separate image with
`--sram-diagnostics`. It enables a fixed-seed wizard character, serial allocation
and stack/guard readings, `#sramtrial` (Ludios), `#sramstack` (travel probes), and
`#srampack` (150 carried test objects), `#sramtour` (depths 1–48, then Ludios),
and `#stats`. This option changes gameplay setup and is intended for testing only.

The measurements establish tested headroom, not safety for every possible
monster population or a completed ascension. See `RESULTS.md` for the
build's validation and limits. NetHack and the console retain their respective
licenses; retain NetHack's license and port patches when distributing modified code.

## Cached-level ownership

NetHack embeds an owner ID and ledger number in every level file. The first
current-level read from a verified companion restores the original game owner
ID and updates the small `.0` lock. Subsequent level writes retain that owner
across process restarts. Ordinary cached-level reads still check both owner and
ledger; they do not require rewriting the dungeon after resume. Regression tests
exercise repeated cross-process resumes and reject wrong owners and ledgers.

## Remaining validation limits

The wizard workloads measure storage and allocation behavior. They do not prove
that a complete ascension, every branch or arbitrary monster populations fit.
The stack guard detects damage only when sampled. Physical soft reboot remains
unverified. See [RESULTS.md](RESULTS.md) for measured workload sizes and headroom.


## Customizing the port

Change the source patch in `port/config.patch`; edit platform adapters, codec
configuration and the manifest under `port/` directly. Shared feature switches
that add or remove entries in `objects[]` or `mons[]` belong in `port/compat.h`,
which is force-included for both host data generators and ARM compilation.
Manifest-only definitions reach the target alone and can break generated table
indexes. The build compares host and target layouts and archive contents.

Read-only data is archived into `nhdata` with raw-deflate entries and per-entry
dictionaries of 1–4 KiB. The symbols file supports DECgraphics. The inherited
port omits the optional `tribute` text file; this is not an all-content ascension
validation. Writable levels and checkpoints use zero-run encoding followed by
Tamp compression. Vendored Tamp 2.5.0 is used under MIT; uzlib uses the zlib
license. See [Tamp provenance](port/tamp/UPSTREAM.md) for the pinned commit
and upstream license selection.

Firmware ends below flash offset `0x17E000`; the 520 KiB FAT12 data volume fills
the remainder of the 2 MiB chip. Raising the volume start reduces space for
levels. `HEAP_END=0x2003d000` reserves 20 KiB above the heap for the stack.
Measured headroom and remaining limits are in [RESULTS.md](RESULTS.md).


The save/restore codec adapters have a separate native sanitizer regression:

```sh
python3 tests/nethack_codec.py --source /path/to/NetHack --sanitize
```
