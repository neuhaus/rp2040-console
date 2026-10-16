# NetHack 3.6.7 port internals

For build, configuration and flashing instructions, see [README.md](README.md).

The build pins NetHack revision
`ed600d9f0f3c37677418f0150f59363ca641f3dc` (3.6.7).

## Memory and save format

The changes close quest decoders between messages, load quest indexes earlier
on restore, bound deflate dictionaries to 4 KiB, pack overview flags, use
16-bit display and remembered-map glyphs, six-byte map squares and byte-sized room references, allocate immutable
special-level operands in arenas, pool 64-byte allocations, and reserve 20 KiB
for the stack. DECgraphics remains available. Selective LTO covers NetHack sources; SDK,
platform wrappers and third-party codecs use ordinary objects. `--jobs` bounds
compilation workers; the final LTO link uses one partition and GCC
interprocedural pointer analysis (`-fipa-pta`) to reduce firmware size.
`--no-lto` disables these options.

Saves use packed overview flags and 13-byte room records; pointers and room
padding are omitted. This format deliberately rejects previous saves and bones
(version incarnation `0x03060782`). Start a new game when switching to this
port. Object and monster link pointers are serialized as zero or presence markers,
then reconstructed by the existing restore code. Live records are unchanged.
Map squares use the existing run-length encoder before zero-run encoding and
Tamp, with separate 1 KiB encoder and decoder windows. Older saves and bones
are incompatible with the six-byte map layout.

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
wizard mode. Inactive level files continue to back the running game. After loading a cached
level, its stale disk copy is removed; the active level lives in SRAM and is
written when leaving or saving. The optional memory log retains only its latest
summary, bounding its flash use. There is one
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
globals reset. Soft reboot disables LTO so the console can tag data/BSS object sections for
reset, and uses the 520 KiB volume. It retains a tracking allocation for each live
allocation, so it has higher SRAM costs than the measured hardware-reboot build.
Its physical USB lifecycle still needs validation. Full-dungeon storage
measurements apply to the default hardware-reboot build.

`--sram-diagnostics` enables the deterministic wizard workload and serial
measurements; leave it off for ordinary play. For a separately installed
picotool, set `PICOTOOL` to its executable.

## Terminal colours and resume naming

NetHack uses its upstream `TERMINFO` colour path. The console supplies fixed
ANSI colour definitions and the `Co`, `AF`, `AB`, `md`, `me`, `mr` and `le`
capabilities. Its bounded `tparm()` adapter expands only the advertised
one-integer colour capabilities; no curses screen manager or terminfo database
is needed. With the default `use_darkgray`, black glyphs—including `dark_room`
floors—use bold black rather than inverse video. Disabling `use_darkgray` uses
the upstream blue fallback. The terminal's palette controls the actual shades.

When no character name is configured, startup probes the single checkpoint and
uses a temporary name only if it exists. Restore supplies the saved hero's
name. If restore fails, the temporary name is cleared before normal character
selection, so a new game still asks for a name. `USER` and `LOGNAME` remain
unset; no dummy name leaks into ordinary new games.

## Regression checks

The build checks all archive entries with zlib and the target inflater, compares
host/target data layouts, validates the FAT image, and checks firmware/SRAM
bounds. For ARM/newlib compact-save and allocator ownership tests, including
all 120 special-level operand programs and 100 simulated soft sessions, run
with QEMU and a native C compiler installed:

```sh
python3 applications/nethack-3.6.7/tests/test.py --build-dir build/nethack-3.6.7
```

The startup regression checks actual upstream colour generation, cleanup and
checkpoint name selection. The command-binding regression compiles the real `bind_key()` and
`commands_init()` functions both with and without the release LTO options. It
checks all signed and unsigned byte keys, binding/unbinding, unchanged adjacent
entries and guards around the keyboard table. Explicit byte normalization in
`bind_key()` avoids negative meta-key indexes observed with ARM GCC 15.3.1 LTO;
the original code corrupts a worm-list pointer at startup and faults on the
first level save or restore.

For repeatable Pico measurements, build a separate image with
`--sram-diagnostics`. It enables a fixed-seed wizard character, serial allocation
and stack/guard readings, `#sramtrial` (Ludios), `#sramstack` (travel probes), and
`#srampack` (150 carried test objects), `#sramtour` (depths 1–48, then Ludios),
`#sramworld` (all actual non-endgame branch levels), `#sramrevisit`
(revisit every non-endgame level, map terrain/traps and remember floor objects), `#sramnextplane`
(one endgame plane), `#sramplanes` (all five), `#sramaltar` (move to the
co-aligned Astral altar), and `#stats`. Add `--sram-max-dungeon` to choose the
maximum valid length for each randomly sized branch. This option changes gameplay setup and is intended for testing only.

The maximum-branch wizard workload completed all 81 playable levels, plane
checkpoints and the normal Amulet offering. This establishes tested headroom,
not safety for every possible monster population or combat path. See
`RESULTS.md` for the build's validation and limits. NetHack and the console retain their respective
licenses; retain NetHack's license and port patches when distributing modified code.

## Cached-level ownership

NetHack embeds an owner ID and ledger number in every level file. The first
current-level read from a verified companion restores the original game owner
ID and updates the small `.0` lock. Subsequent level writes retain that owner
across process restarts. Ordinary cached-level reads still check both owner and
ledger; they do not require rewriting the dungeon after resume. Regression tests
exercise repeated cross-process resumes and reject wrong owners and ledgers.

## Remaining validation limits

The wizard workloads measure storage and allocation behavior with forced
travel and mapped levels. Natural combat paths, all roles/seeds and arbitrary
monster or object populations are outside this validation.
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
dictionaries of 1–4 KiB. The symbols file supports DECgraphics. The optional `tribute` text file is omitted, as agreed for this port. Writable maps use NetHack run-length encoding; levels and checkpoints use
zero-run encoding followed by
Tamp compression. Vendored Tamp 2.5.0 is used under MIT; uzlib uses the zlib
license. See [Tamp provenance](port/tamp/UPSTREAM.md) for the pinned commit
and upstream license selection.

With LTO, firmware ends below flash offset `0x17A000`; the 536 KiB FAT12 data volume fills
the remainder of the 2 MiB chip. Without LTO the boundary is `0x17E000` and
the volume is 520 KiB. Changing layouts requires flashing the matching data
image. The application reserves 256 root directory entries. Small FAT12 images
use one-sector track geometry so every declared sector remains usable; all
level and bones contents remain supported. Raising the volume start reduces
space for levels. `HEAP_END=0x2003d000` reserves 20 KiB above the heap for the stack.
Measured headroom and remaining limits are in [RESULTS.md](RESULTS.md).


The save/restore codec adapters have a separate native sanitizer regression:

```sh
python3 tests/nethack_codec.py --source /path/to/NetHack --sanitize
```

## Mutable quest traits

The immutable monster table stays in flash. Three RAM copies hold the quest
leader, guardian and nemesis traits initialized by `role_init`; sound, alignment
and mutable flag accesses resolve through a common accessor. Monster pointers
retain their original table identities. This costs 180 bytes of static SRAM and
restores the original role-specific behavior without writing flash. Tests cover
all 13 roles and three alignments, repeated initialization and table immutability.

## Build-time archive optimization

A native Zopfli host tool tries three optimization iterations at 1, 2 and 4 KiB
windows alongside zlib. Each archive entry takes the smallest result, preferring
a smaller dictionary on ties. This changes only the encoder on the build host;
the Pico continues to use the same raw-deflate inflater and 4 KiB maximum
dictionary. Every entry is verified with zlib and the target inflater. Set
`BUILD_CC` to select the native host compiler independently of the 32-bit data
generator compiler. The vendored host-only tool is Apache-2.0; its source,
license and pinned revision are in `port/host/zopfli/`. It is never linked into
NetHack or the firmware.
