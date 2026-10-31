# Score-log cleanup — 31 October 2026

**With the score logs added to the cleanup, the full run passed again: all 90
checks of `tests/ascension.py`, every FAT image clean under `fsck.fat -n`, and
the ordinary release firmware ascended.** Bones files are deleted level by
level, and when that is not enough `xlogfile` and then `logfile` are emptied
(see INTERNALS.md). Release firmware 1,546,636 B, diagnostic firmware
1,548,268 B (20 B below the volume).

| Measure | Result |
|---|---:|
| planted before the world stage | 7 bones files (12,085 B), `logfile` 966 B, `xlogfile` 3,296 B |
| reclaimed, in order | 7 bones files, then `xlogfile`, then `logfile` |
| free space at each reclaim | 41,472–48,640 B, always below the 48 KiB reserve |
| space freed | 18,944 B (14,336 B bones, 3,584 B `xlogfile`, 1,024 B `logfile`) |
| free after the deep checkpoint | **11,264 B** |
| free without the cleanup | about −7.5 KB: the dungeon would not fit |
| restore (#stats) | 200 objects, 3 monsters, 76 overviews, 153 contained: identical |
| Astral checkpoint | 526 objects, 83 monsters, 81 overviews, 153 contained: identical |
| logged after emptying | ascension: `logfile` 80 B, `xlogfile` 320 B; quit game +70 / +303 B |
| peak heap / stack | 104,440 B / 16,132 of 20,480 B; 0 failed allocations, guard intact |

The emptied logs stay on the volume with size 0, and the next finished games
were logged normally. `record` stayed empty: every game in the run was played
in wizard or explore mode, or quit without points.

Faults found on the way:

- A first version **deleted** the logs. That run reached the ascension, but the
  score check then found that a deleted log is never recreated: with `fcntl`
  locking, `lock_file()` opens the log first and gives up ("Cannot open file
  logfile"). The logs are now emptied instead.
- The harness answered the wizard-mode "Die?" with `y` after the bones stage,
  and a level-1 hero died on arrival on the Plane of Air. It now answers `n`
  except while planting bones.
- One rerun stopped in the Gnomish Mines because the host computer went to sleep
  and dropped the USB connection. The final run kept the host awake
  (`caffeinate`).

# Hardware validation — 30 October 2026

**On a volume that already held seven bones files, the maximum-branch dungeon
was filled, saved and restored. The ordinary release firmware then climbed out
of level 1 with the Amulet into the Planes and ascended.** All 94 checks of
`tests/ascension.py` passed, and every FAT image passed `fsck.fat -n`. Bones
files were deleted only as free space fell below the 48 KiB reserve. Without
that, the fully mapped dungeon would not have fitted on the volume.

This is still a wizard-driven storage and memory test, not a naturally played
game. What is new compared with 16 October:

- the level-1 climb, a normal checkpoint restore of the full dungeon with real
  stair travel, and the final `#offer` all ran on the **ordinary** firmware;
- the planes were left through their magic portals by the ordinary trap code
  (`domagicportal()`), not by direct level changes;
- the volume carried bones files and the score logs of earlier games.

## Builds

Pico SDK 2.3.1, Arm GNU Toolchain 15.3.Rel2, QEMU-generated data.

| Build | Firmware | Static SRAM | Heap budget | Stack reserve |
|---|---:|---:|---:|---:|
| Ordinary release | 1,546,620 B | 133,520 B | 116,336 B | 20,480 B |
| Maximum-branch diagnostic | 1,548,116 B | 133,544 B | 116,312 B | 20,480 B |

The bones reserve adds about 200 bytes to the ordinary firmware. The flash
layout, save format and data volume are unchanged: both images build the same
`nhdata`, so one volume moves between them. The application regression suite
(`tests/test.py`) passes; its checkpoint test now also checks that startup
removes an unfinished bones file.

## Bones and the full dungeon

Ten wizard characters died on levels 3–12 of the Dungeons of Doom. Seven left
bones files (levels 3–7, the Oracle and the big room; 12,084 B in all). The
other three levels allow none. A new fixed-seed game carried a bag of 150 rings,
visited all 76 non-endgame levels of the maximum-branch roster (75 cached),
revisited and mapped them, and declined all seven "Get bones?" prompts, so
the files stayed on flash.

| Measure | Result |
|---|---:|
| bones files deleted (oldest first) | 7 of 7, 12,084 B |
| free space when each was deleted | 41,472–48,640 B |
| free after the mapped revisit | 26,112 B |
| free after the deep checkpoint | **6,656 B** |
| companion / cached levels | 12,127 B / 75 files, 187,956 B |
| free without the deletions | about −5.4 KB: the dungeon would not fit |
| restore (#stats) | 200 objects, 3 monsters, 76 overviews, 153 contained: identical |

The free space at the checkpoint is lower than the 11,264 B of 16 October.
Most of the difference is the 4,262 B of `logfile` and `xlogfile` left by the
seven earlier deaths (see Limits).

## Ordinary firmware

The full-dungeon checkpoint was restored on the release firmware. Wizard mode
is refused there ("Only user wizard may access debug (wizard) mode"), so the
game continues in explore mode. Travel (`_` with `<`/`>`) plus the ordinary
staircase commands went from level 2 down to cached level 3, then up to 2 and
1. On level 1, `<` without the Amulet asked "Beware, there will be no return!"
and was declined. The game was saved again, with 7,168 B free.

After `#sramamulet` on the diagnostic firmware, the ordinary firmware restored
the hero on the level-1 up staircase and climbed with the real Amulet into
Earth. Entering the Planes deleted the 75 cached levels (216,064 B free).

## Planes and ascension

Earth, Air, Fire and Water were each saved, imaged, restored on the diagnostic
firmware and compared, then left through their magic portals (`#sramportal`):

| Plane | Objects | Monsters | Contained | Overviews | Free after save |
|---|---:|---:|---:|---:|---:|
| Earth | 209 | 63 | 151 | 77 | 216,064 B |
| Air | 174 | 52 | 151 | 78 | 213,504 B |
| Fire | 216 | 63 | 151 | 79 | 210,944 B |
| Water | 169 | 61 | 151 | 80 | 207,872 B |
| Astral (on the altar) | 526 | 83 | 153 | 81 | 202,240 B |

On Astral, `#sramaltar` placed the hero on the high altar to Thoth (neutral),
and the game was saved (15,075 B companion). The **ordinary firmware**
restored it and `#offer`ed the Amulet: "You ascend to the status of
Demigod...". Afterwards the volume held no checkpoint and no level files, and
had 226,304 B free.

## Memory (diagnostic firmware, every sample)

| Measure | Result | Budget |
|---|---:|---:|
| peak allocated heap | 104,440 B | 116,312 B |
| least arena growth left | 11,784 B | — |
| peak stack | 16,132 B | 20,480 B |
| failed allocations | 0 | — |
| stack guard | intact in every sample | — |

## Score files

A normal game that is quit adds 70 B to `record` (capped at 100 entries),
70 B to `logfile` and 303 B to `xlogfile`. A death adds about 610 B to the two
logs. As of 31 October the reserve empties `xlogfile` and `logfile` when bones
files alone do not free enough space; `record` is kept.

## Faults found while building the test

- **Torn `.clock` update (harness).** The script once sent a key while the
  board waited at "Press any key". `_exit()` then rewrote `.clock` while the
  script forced a USB reboot. FAT copy 1 kept the new cluster, while FAT copy 2
  and the directory entry did not. The checkpoint was reloaded from the clean
  image and the stage was repeated. The harness now never sends keys at that
  prompt. A power cut at the same moment would do the same, as documented.
- **Imitation Amulet.** Wizard-mode wishes for "Amulet of Yendor" may grant
  the cheap plastic imitation, because `rnd_otyp_by_namedesc()` matches both
  objects by description. That is upstream behaviour. The climb then escaped
  the dungeon, as it should without the Amulet (`u.uhave.amulet=0` on the
  branch staircase). The test uses `#sramamulet` instead.

Raw serial logs, `results.json` and every FAT image are in
`build/nethack-ascension/`; the two incidents' logs are in its `incident-*`
folders.

## Limits

- **Score logs grew without limit** (about 370–610 B per finished game) and
  would have used up this margin after roughly 18–30 games; fixed on
  31 October by emptying them under the reserve.
- The volume can still fill: the reserve frees bones files and the score logs
  only. A full volume ends the game in a panic.
- Wizard travel, one fixed seed, one role. Natural combat and arbitrary object
  hoards remain outside this validation.

# Earlier validation — 16 October 2026

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
| Ordinary release | 1,546,148 B | 133,456 B | 116,400 B | 20,480 B |
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

The subsequent terminfo and automatic-resume update passes the console and
application regression suites, including actual upstream colour generation and
checkpoint name selection. Its firmware was flashed and verified on the Pico;
the user confirmed the display looks correct. Relative to the staircase-only
fix, it adds 272 bytes of firmware and 16 bytes of static SRAM. The save format
and flash layout are unchanged.

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
