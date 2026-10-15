# Resume checkpoint — 15 October 2026

Code checkpoint: `bd51aaa` on `main`. The normal application recipe remains
unchanged; all experimental changes are opt-in through this artifact package.

## Implemented

- Combined SRAM reductions: lazy quest decoders, restore ordering, 4 KiB
  dictionaries, compact glyphs/rooms/overviews, operand arenas, 64-byte slabs,
  and 20 KiB stack reserve.
- One checkpoint shared by all character names, as requested. `save/0game`
  stores the current level and global/hero/inventory state. Other levels remain
  in root `0game.<ledger>` files. No dungeon-wide copy on save or restore.
- The companion records sidecar sizes and CRC32 values. Restore checks these
  before reading the current level/global state. A second current-level read
  preserves the original restore ordering while quest indexes load with level
  entities released. No temporary current-level file is created during restore.
- Completed companion publication uses `0game.tmp` then rename. Successful
  restore consumes the companion even in wizard mode. Startup preserves cached
  levels while a committed companion exists; otherwise it clears orphaned levels
  and the unfinished temp file. Game death/ordinary cleanup still removes levels.
- New save/bones incarnation `0x03060781`; previous formats are not compatible.
- The private generated source tree is cleared before reapplying the patch, so
  builds are repeatable even when the patch adds new source files.

This does not promise power-loss recovery on FAT. The checks detect damage and
avoid offering an unfinished temp as a completed save. There is no retained
previous checkpoint after a successful resume.

## Verified

The artifact script built from the clean pinned NetHack source with Pico SDK
2.3.1 and ARM GCC 15.3.1/newlib 4.6. The diagnostic build is 1,549,716 B firmware,
137,044 B static SRAM, 112,812 B heap and 20,480 B stack reserve. Archive, data
layout, FAT and firmware bounds checks passed.

The packaged tests passed all 120 special-level programs, compact nested-room
records and 100 pooled soft-session ownership cycles. Actual checkpoint code
passed native address/undefined-behavior sanitizers for the CRC32 reference
vector, healthy manifests, damaged/missing/truncated/duplicate records, cache
preservation, interrupted-temp cleanup and completed-file publication. The
actual codec adapters passed 2,240 sanitizer cases with zero failures.

The earlier format's 48-overview game saved to 156,130 B but failed restoring
with zero free flash. Its SRAM allocation failure count was zero. Those hardware
measurements are historical evidence in RESULTS.md and MEASUREMENTS.json, not
verification of the new checkpoint implementation.

## Next validation, before calling the storage fix complete

1. Build and flash the **latest** diagnostic firmware and matching fresh data.
   Start a new game; do not reuse a format-0x80 fixture.
2. Run `#sramtrial` for fixed-seed Ludios. Create 30 peaceful soldiers with the
   wizard genesis command (`30`, Ctrl-G, `peaceful soldier`), then `#srampack`.
   Capture `#stats`, heap/stack/guard measurements and exact entity counts.
3. Run `#sramtour`. It visits depths 1–48 directly, returns to Ludios and emits
   `MEASURE tour_done`. Keep servicing `--More--` prompts. Confirm overview count
   and retained inventory; it should exercise 49 records including Ludios.
4. Save, capture the volume, list `save/0game` and all `0game.<ledger>` files,
   and check the FAT. There must be no completed `0game.tmp` left over.
5. Reboot and restore. Compare counts/inventory/overviews, flash use, heap peaks,
   stack and guard. Verify that restore does not create a second dungeon copy
   and that the companion is consumed. No keep-save prompt should appear.
6. Revisit multiple existing levels, save/restore again, and test ordinary end
   cleanup/new-game orphan cleanup. Extend to the remaining branches/endgame
   before making an ascension claim. Rebuild ordinary/soft variants as needed;
   the new checkpoint's physical soft-reboot lifecycle has not been tested.

## Local workspace continuation

Repository: `/Users/user/Documents/rp2040-nethack-3.6.7/rp2040-console`.
Local evaluation evidence and build helpers are in the parent `evaluation/`
folder, outside Git. Useful paths:

- Latest generated build: `evaluation/build/sidecar-trial/game/hardware/`.
- Current build log: `evaluation/build/sidecar-build.log`.
- Current tests: `evaluation/build/sidecar-final-tests.log` and
  `evaluation/build/sidecar-final-codec.log`.
- Native checkpoint harness: `sidecar-trial/tests/checkpoint/` under the build
  folder. It uses the actual manifest, cache startup and publication code.
- Historical volumes: `artifact-history-volume.uf2` (preserved before failed
  restore), `artifact-failed-restore-volume.uf2`, `artifact-pack-volume.uf2`.
  These are **old format**, for diagnosis only.
- Serial helpers: `evaluation/serial_until.py`, `serial_measure.py`,
  `serial_save_fixture.py`, `serial_workload.py`. Use one USB reader at a time.
  The older restore helper waits for a keep-save prompt; update it to stop on
  `MEASURE restore_done` for this new format.

At the checkpoint, the Pico was last left in BOOTSEL with an earlier development
image and fresh format-0x81 data. The final build described above was **not**
flashed or hardware-tested. Reload both latest images before continuing.

Local toolchain setup used:

```sh
export PATH=/Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin:/opt/homebrew/sbin:/opt/homebrew/bin:$PATH
export PICO_SDK_PATH=/Users/user/Documents/rp2040-nethack-3.6.7/pico-sdk
export PICOTOOL=/Users/user/Documents/fuzix-hack/toolchains/picotool-local/picotool/2.3.1/bin/picotool
export CMAKE_PREFIX_PATH=/Users/user/Documents/fuzix-hack/toolchains/picotool-local/picotool/2.3.1
python3 artifacts/nethack-3.6.7/build.py --qemu-generators \
  --source ../NetHack --sram-diagnostics --jobs 6
```

USB operations require the desktop's elevated tool permission. No new packages
are currently needed. Avoid rerunning the exploratory source-transform scripts;
they are not all idempotent. The committed artifact patch and build script are
the reproducible source of truth.
