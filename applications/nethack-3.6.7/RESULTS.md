# Hardware validation — 31 October 2026

**On a volume that already held seven bones files and the score logs of earlier
games, the maximum-branch dungeon was filled, saved and restored. The ordinary
release firmware then climbed out of level 1 with the Amulet into the Planes
and ascended.** All 90 checks of `tests/ascension.py` passed, and every FAT
image passed `fsck.fat -n`. Bones files were deleted, and then the score logs
emptied, only as free space fell below the 48 KiB reserve. Without that, the
fully mapped dungeon would not have fitted on the volume.

This is a wizard-driven storage and memory test, not a naturally played game.
The level-1 climb, a normal checkpoint restore of the full dungeon with real
stair travel, and the final `#offer` ran on the **ordinary** firmware; the
planes were left through their magic portals by the ordinary trap code
(`domagicportal()`).

## Builds

Pico SDK 2.3.1, Arm GNU Toolchain 15.3.Rel2, QEMU-generated data. NetHack is
pinned to `ed600d9f0f3c37677418f0150f59363ca641f3dc`, save incarnation
`0x03060782`.

| Build | Firmware | Static SRAM | Heap budget | Stack reserve |
|---|---:|---:|---:|---:|
| Ordinary release | 1,546,636 B | 133,512 B | 116,344 B | 20,480 B |
| Maximum-branch diagnostic | 1,548,268 B | 133,536 B | 116,320 B | 20,480 B |

The diagnostic image ends 20 B below the data volume. Both images build the
same `nhdata`, so one volume moves between them. The application regression
suite (`tests/test.py`) passes.

## Bones, score logs and the full dungeon

Ten wizard characters died on levels 3–12 of the Dungeons of Doom. Seven left
bones files (levels 3–7, the Oracle and the big room); the other three levels
allow none. A new fixed-seed game carried a bag of 150 rings, visited all 76
non-endgame levels of the maximum-branch roster (75 cached), revisited and
mapped them, and declined all seven "Get bones?" prompts, so the files stayed
on flash.

| Measure | Result |
|---|---:|
| on the volume before the world stage | 7 bones files (12,085 B), `logfile` 966 B, `xlogfile` 3,296 B |
| reclaimed, in order | 7 bones files, then `xlogfile`, then `logfile` |
| free space at each reclaim | 41,472–48,640 B |
| space freed | 18,944 B |
| free after the deep checkpoint | **11,264 B** |
| companion / cached levels | 12,088 B / 75 files, 187,973 B |
| free without the cleanup | about −7.5 KB: the dungeon would not fit |
| restore (#stats) | 200 objects, 3 monsters, 76 overviews, 153 contained: identical |

The emptied logs stay on the volume with size 0; later games were logged
normally.

## Ordinary firmware

The full-dungeon checkpoint was restored on the release firmware. Wizard mode
is refused there ("Only user wizard may access debug (wizard) mode"), so the
game continues in explore mode. Travel (`_` with `<`/`>`) plus the ordinary
staircase commands went from level 2 down to cached level 3, then up to 2 and
1. On level 1, `<` without the Amulet asked "Beware, there will be no return!"
and was declined. The game was saved again, with 11,776 B free.

After `#sramamulet` on the diagnostic firmware, the ordinary firmware restored
the hero on the level-1 up staircase and climbed with the real Amulet into
Earth. Entering the Planes deleted the 75 cached levels.

## Planes and ascension

Earth, Air, Fire and Water were each saved, imaged, restored on the diagnostic
firmware and compared, then left through their magic portals (`#sramportal`):

| Plane | Objects | Monsters | Contained | Overviews | Free after save |
|---|---:|---:|---:|---:|---:|
| Earth | 215 | 63 | 151 | 77 | 220,672 B |
| Air | 179 | 53 | 151 | 78 | 218,112 B |
| Fire | 216 | 63 | 151 | 79 | 215,040 B |
| Water | 169 | 61 | 151 | 80 | 212,480 B |
| Astral (on the altar) | 526 | 83 | 153 | 81 | 206,848 B |

On Astral, `#sramaltar` placed the hero on the high altar to Thoth (neutral),
and the game was saved (15,085 B companion). The **ordinary firmware**
restored it and `#offer`ed the Amulet: "You ascend to the status of
Demigod...". Afterwards the volume held no checkpoint and no level files, and
had 230,912 B free.

## Memory (diagnostic firmware, every sample)

| Measure | Result | Budget |
|---|---:|---:|
| peak allocated heap | 104,440 B | 116,320 B |
| least arena growth left | 11,792 B | — |
| peak stack | 16,132 B | 20,480 B |
| failed allocations | 0 | — |
| stack guard | intact in every sample | — |

## Score files

A finished game adds about 70 B to `logfile` and 300 B to `xlogfile`; `record`
is capped at 100 entries. When bones files alone do not free enough space, the
reserve empties `xlogfile` and then `logfile`; `record` is kept.

# Heavier workload — 16 October 2026

An earlier run of the same maximum-branch roster carried a heavier load: Ludios
was generated and 30 equipped peaceful soldiers were added alongside the bag of
150 rings. Its checkpoint and heap figures are the largest measured.

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

The fully mapped pre-endgame checkpoint saved and restored with matching
counts: 175 monsters, 1,029 objects, 162 contained objects and 76 overviews. Its
companion was **20,326 B**, leaving **11,264 B free** in the 536 KiB FAT
volume. Restore retains the cached
dungeon in place and consumes the companion, avoiding a second dungeon copy.

Allocated heap peaked at **110,540 B**. The fully mapped save left 3,940 B
available for arena growth. The highest painted stack use was **15,292 B of
20,480 B**. Every sampled guard remained intact and every sampled
failed-allocation count was zero.

Every plane was saved, rebooted and restored with matching counts (Astral: 541
objects, 93 monsters, 152 contained, 81 overviews; 206,848 B free), and the
`#offer` of the real Amulet ascended. Final exit recorded zero allocation
failures, an intact guard, and a clean FAT volume with 230,912 B free.

## Regression checks

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
- Actual command initialization and all signed/unsigned byte bindings, including
  unbinding and memory guards, under ARM/newlib both with and without LTO.

# Limits

These results demonstrate the measured full-dungeon, mapped-level and endgame
workloads on a standard 2 MiB RP2040: wizard travel, one fixed seed, one role.
Natural combat paths, every role/seed and arbitrary populations of monsters or
objects remain outside this validation. A full volume still ends the game in a
panic: the reserve frees only bones files and the score logs. The painted stack
guard detects damage only when sampled. Physical soft reboot remains
unverified.
