# Hardware validation — 16 October 2026

**The RP2040 completed the maximum-branch wizard stress scenario through all
81 playable levels, saved and restored on every endgame plane, offered the
Amulet of Yendor and exited successfully as a Demigod.**

This is a storage and SRAM validation using forced wizard travel, not a naturally
played combat ascension. Ordinary builds do not enable the diagnostic commands
or force maximum branch lengths. All game content is retained except tribute.

## Build and regression checks

NetHack is pinned to `ed600d9f0f3c37677418f0150f59363ca641f3dc`, with save
incarnation `0x03060782`. Builds used Pico SDK 2.3.1, ARM GCC 15.3.1/newlib 4.6,
Ninja and QEMU-generated ILP32 data. Selective LTO covers NetHack sources;
platform code and codecs remain ordinary objects.

| Current build | Firmware | Static SRAM | Heap budget | Stack reserve |
|---|---:|---:|---:|---:|
| Ordinary release | 1,545,876 B | 133,440 B | 116,416 B | 20,480 B |
| Maximum-branch diagnostic (15 October) | 1,547,796 B | 133,492 B | 116,364 B | 20,480 B |

Both builds pass archive decoding, host/target layout, FAT, UF2 and flash/SRAM
bounds checks. The ordinary image was also tested through checkpoint restore
and actual staircase commands on the Pico, as detailed below.

Regression checks passed for:

- All 138 archive entries with zlib and the actual target inflater; the data
  archive is 301,799 B. Build-only Zopfli improves compression while keeping the
  existing 1–4 KiB decoder windows.
- All operands and strings in 120 special-level programs, compact room records
  and 100 simulated pooled-allocation sessions.
- 2,240 save/restore codec cases under native address/undefined-behavior
  sanitizers, plus actual checkpoint publication, corruption/missing/truncated
  level rejection, duplicate manifests and cross-process owner/ledger checks.
- Six-byte map squares across all glyph values, raw/RLE map round trips and
  run boundaries, and 32 object/monster pointer-presence combinations.
- 78 repeated role/alignment cases for writable quest traits while keeping
  monster identities and the base table immutable.
- The console regression suite and shared `getcwd()`/`fpathconf()` API checks.
- Actual command initialization and all signed/unsigned byte bindings, including
  unbinding and memory guards, under ARM/newlib both with and without LTO.
  This test fails against the previous release with LTO and passes with the fix.

## Ordinary staircase regression

The 15 October ordinary release could freeze on the first `>` from dungeon
level 1. ARM GCC 15.3.1 with the release LTO options failed to narrow a legacy
`uchar` argument in `bind_key()`: the negative meta-key `M('n')` indexed before
`Cmd.commands`, overwriting a worm-list pointer during command initialization.
The first level save then faulted in `save_worm()`. Restoring a checkpoint could
fault while clearing the same list. Diagnostic builds had a different memory
layout and did not expose this failure during forced wizard travel.

The port now receives the promoted key as `int` and explicitly normalizes it to
an unsigned byte before indexing. On the connected Pico, a fresh normal
character with a test-only staircase-positioning wrapper completed the actual
`>` command without the earlier worm-list corruption. The ordinary firmware,
without that wrapper or fault instrumentation, restored a level-1 checkpoint,
descended to level 2, returned up to the cached first level, saved again, then
restored after a hardware reboot and descended to level 2 again.
The fix adds 48 bytes of flash and no static SRAM; the save format is unchanged.

The full-dungeon resource measurements below were taken on 15 October with the
separate diagnostic build. They do not replace ordinary-image functional tests.

## Accumulated dungeon and checkpoint

The fixed-seed (`0x5eed1234`) wizard workload generated Ludios, added 30 equipped
peaceful soldiers and a bag containing 150 individual rings, then visited the
maximum valid branch roster in the same game:

| Branch | Playable levels |
|---|---:|
| Dungeons of Doom | 29 |
| Gehennom | 24 |
| Gnomish Mines | 9 |
| Quest | 6 |
| Sokoban | 4 |
| Fort Ludios | 1 |
| Vlad's Tower | 3 |
| Endgame planes | 5 |
| Total | 81 |

The planes branch includes a sixth ledger entry for the dummy surface level;
that entry is not an additional playable plane.

The first pre-endgame checkpoint retained 76 overview records and 75 inactive
cached levels. Restore matched 175 monsters (including one migrating), 1,032
objects and 162 contained objects. Its companion was 20,415 B, leaving 44,032 B
free on flash. All 76 levels were then revisited, with terrain/traps revealed
and actual floor objects remembered before leaving each level.

A second, fully mapped checkpoint saved and restored with matching counts:
175 monsters, 1,029 objects, 162 contained objects and 76 overviews. Its companion
was **20,326 B**, leaving **11,264 B free** in the 536 KiB FAT volume. Both saved
volumes passed `fsck.fat -n`. Restore retains the cached dungeon in place and
consumes the companion, avoiding a second dungeon copy.

Across generation, travel and checkpoints, allocated heap peaked at
**110,540 B**. After the first tour, the arena held 1,912 B free (largest block
1,772 B), with another 5,084 B available for growth. The fully mapped save left
3,940 B available for arena growth. These are separate allocator measures;
their sum does not guarantee a single allocation of that size.

The highest painted stack use was **15,292 B of 20,480 B**, including the travel
probes. Every sampled guard remained intact and every sampled failed-allocation
count was zero. The observed 5,188 B stack margin does not justify reducing the
reserve without broader gameplay coverage.

## Endgame and ascension

The same accumulated game entered Earth carrying the real Amulet. Normal
endgame entry deletes unreachable level files, freeing flash, while retaining
its overview history. Every plane was saved, rebooted and restored before
continuing; all four counts below matched exactly before and after each resume.

| Plane | Objects | Monsters | Contained | Overviews | Flash free after save |
|---|---:|---:|---:|---:|---:|
| Earth | 198 | 64 | 150 | 77 | 220,672 B |
| Air | 171 | 52 | 150 | 78 | 218,112 B |
| Fire | 211 | 64 | 150 | 79 | 215,552 B |
| Water | 166 | 62 | 150 | 80 | 212,992 B |
| Astral | 541 | 93 | 152 | 81 | 206,848 B |

All five saved FAT volumes were clean. The Astral checkpoint also restored on
the final firmware containing the shared filesystem API changes. A diagnostic
command positioned the hero at a real co-aligned high altar; the normal
`#offer` command sacrificed the real Amulet and reported:

```text
You ascend to the status of Demigod...
Goodbye wizard the Demigod...
```

The game returned to the new-game prompt. Final exit recorded 74,628 B peak
allocated heap, 6,896 B stack use since the last reboot, zero allocation failures
and an intact guard. The post-ascension FAT volume was clean, had no remaining
checkpoint companion or cached levels, and had 230,912 B free. The Pico was
then returned to the ordinary release image.

## Limits

These results demonstrate the measured full-dungeon, mapped-level and endgame
workload on a standard 2 MiB RP2040. Natural combat paths, every role/seed and
arbitrary populations of monsters or objects remain outside this validation.
The painted stack guard detects damage only when sampled. Physical soft reboot
remains unverified; the measurements above use the default hardware restart.
