# RP2040-Zero validation — 30 October 2026

**The connected RP2040-Zero completed all 82 playable levels in the stated wizard
workload, passed every checkpoint, then ascended through the ordinary release
firmware using the normal `#offer` command.** Raw serial logs and FAT snapshots are in
`build/ascension-results/` in this workspace.

## Failure reproduced in the original port

The maximum-branch wizard character visited all 77 non-endgame levels while
carrying a bag with 150 individual rings. The combined save was 554,004 bytes,
leaving 105,472 bytes free in the 1 MiB FAT volume.

Restore returned to the game with matching active object/monster counts, but
wrote the cached dungeon alongside the still-open combined save. The available
flash ran out. The old zero-run writer ignored short writes: the restored volume
contained **56 empty level files**. Deleting the consumed save subsequently made
flash appear available again. A successful welcome-back message and a clean FAT
are insufficient evidence of a usable restored dungeon.

The baseline snapshots and listings remain in `build/ascension-results/baseline/`.

A second failure appeared at the first Earth checkpoint: the Unix `uptodate()`
heuristic discarded a valid save because its session-clock timestamp preceded
the FAT timestamp of the empty `nethack` placeholder. That placeholder is not
the executable. The port now uses the explicit storage-format header, rather
than comparing these unrelated dates. A regression compiles the actual patched
`uptodate()` function and tests a valid save against a future placeholder date,
along with old-format rejection.

## Storage fix and software checks

The port retains upstream map RLE and zero-run encoding, then compresses its byte
stream with bounded Tamp frames. Each frame carries input/payload lengths and a
CRC32; an explicit final marker catches physical truncation. Compression falls
back to raw storage for incompressible frames. The encoder and decoder have
separate fixed buffers, so saving can read a cached level while writing the
combined save. Every physical codec write is checked.

The new eight-byte format header rejects old saves and bones. Start a new game
and flash both images. The logical game structures and all game content remain
unchanged. FAT is still not transactional or safe against interrupted power.

Native tests compile the actual patched zero-run save/restore adapters and the
same embedded Tamp implementation under AddressSanitizer and
UndefinedBehaviorSanitizer. They pass **8,496 round trips**, repeated reset/rewind,
interleaved cached-level copying, short-write rejection and 11 truncation or
corruption cases. The console regression suite also passes. Its ELF-object
fixtures used ARM GCC/binutils on macOS; executable console tests ran natively.

Builds use Pico SDK 2.3.1, ARM GCC 15.3.1/newlib, QEMU ILP32 data generators and
Ninja. All 77 host/target layout probes, FAT, UF2, XIP and flash/SRAM bounds checks
pass. Ordinary and soft-reboot images both build; physical soft reboot is outside
this validation. The SDK was cloned into this workspace.

| Build | Firmware | Static SRAM | Heap budget | Stack/scratch reserve |
|---|---:|---:|---:|---:|
| Ordinary | 841,700 B | 142,864 B | 102,896 B | 24,576 B |
| Maximum-branch diagnostic | 844,276 B | 142,912 B | 102,848 B | 24,576 B |

## Hardware workload

The RP2040-Zero reports RP2040 B2 and 2 MiB flash. Firmware and fresh data images
were written and verified over USB. The opt-in diagnostic build uses seed
`0x5eed1234`, a Valkyrie and maximum valid branch lengths:

| Branch | Playable levels |
|---|---:|
| Dungeons of Doom | 29 |
| Gehennom | 29 |
| Gnomish Mines | 9 |
| Quest | 6 |
| Fort Ludios | 1 |
| Vlad's Tower | 3 |
| Elemental Planes | 5 |
| Total | 82 |

The character carries 150 individual rings in a Bag of Holding in addition to
normal inventory and generated dungeon populations. The first tour generates
all 77 non-endgame levels (the initial diagnostic image was 844,252 B; the
full-map diagnostic helper added 88 B of code and no static SRAM; the timestamp
fix subsequently removed 64 B). A second tour revisits every level, maps terrain and
traps and remembers floor objects. Each checkpoint compares current/migrating
object and monster counts, carried object counts and the cached-level roster.
After restoring, **every cached file** is read to its validated end marker and
checked for matching owner/ledger and frame checksums. Saved FAT snapshots pass
`fsck.fat -n`.

Unlike 3.6.7, this 3.1.3 engine retains the old dungeon on endgame entry. Plane
checkpoints therefore retain the accumulated dungeon as well as visited planes.

| Checkpoint | Verified levels | Objects | Monsters | Carried incl. contents | Save | Flash free after save |
|---|---:|---:|---:|---:|---:|---:|
| World | 77 | 167 | 8 | 159 | 188,559 B | 471,040 B |
| Mapped | 77 | 168 | 8 | 160 | 212,117 B | 447,488 B |
| Earth | 78 | 209 | 61 | 161 | 214,409 B | 445,440 B |
| Air | 79 | 165 | 50 | 161 | 215,811 B | 443,904 B |
| Fire | 80 | 185 | 60 | 161 | 218,536 B | 441,344 B |
| Water | 81 | 161 | 44 | 161 | 220,208 B | 439,296 B |
| Astral | 82 | 432 | 74 | 161 | 226,929 B | 432,640 B |

All before/after dictionaries match exactly. Object and monster counts refer to
active/current and migrating state; cached levels are verified separately.
All seven saved FAT images are clean. The fully mapped save is **212,117 B**,
leaving **447,488 B free**; the Astral save is **226,929 B**, leaving **432,640 B**.

Across the diagnostic sessions, allocated heap peaked at **85,088 B** and painted
stack use at **14,016 B of 24,576 B**. Every sampled guard remained intact and the
failed-allocation request field stayed zero. The `largest=0` instrumentation field
is unavailable for this full-newlib allocator; it is not a largest-free-block
measurement.


## Ascension and ordinary firmware

The final ordinary release (without diagnostic workload symbols) restored the
Astral checkpoint at the co-aligned high altar of Odin. Inventory showed
`j - the Amulet of Yendor`. The ordinary `#offer` command, selecting `j`, produced:

```text
You offer the Amulet of Yendor to Odin...
You ascend to the status of Demigoddess...
Goodbye wizard the Demigoddess...
```

The game explicitly identified this as a wizard-mode character and did not enter
the ordinary score list. It completed exit and returned to `Press any key for a
new game`. This was the real game sacrifice/end/cleanup path, not a diagnostic
command setting an ascended flag.

The post-ascension FAT image passes `fsck.fat -n`, contains no cached dungeon or
saved character, and has **659,456 B free**. The raw ordinary-firmware proof is in
`ordinary-astral-restore.log`, `ordinary-inventory.log`, `ordinary-ascension.log`
and `ascended.img` under the result directory.

A separate normal character, without wizard mode, was initially placed at a
real down staircase by `tests/staircase.py`. It was saved, then restored on the
unmodified final ordinary firmware. The actual `>` command reached level 2;
`<` returned to the cached first level. After saving again and a hardware reboot,
the ordinary firmware restored the character and `>` loaded the cached second
level successfully. All staircase commands ran without the positioning helper.
The corresponding `staircase-*.log` captures are in the result directory.

The board was finally returned to the ordinary release and a fresh data volume;
both images were verified after writing. Final ordinary outputs are under
`build/nethack-3.1.3/hardware/`.



## Limits

This follows the 3.6.7 resource-validation approach using forced wizard travel;
it is not a naturally played combat ascension. It uses a freshly flashed volume without extra saved characters or accumulated
bones and covers one role/seed, maximum
branch lengths, normal generated populations and the stated carried-object load.
Forced travel bypasses quest/invocation progression and portal navigation.
It does not establish capacity for arbitrary monster/object populations, every
role, every combat path or interrupted writes. No artificial 30-soldier crowd
was added in this 3.1.3 workload. Painted stack guards detect damage when sampled;
remaining heap growth and allocator free bytes do not imply one contiguous
allocation of their sum.

## Final artifact checksums

SHA-256 of the tested ordinary release:

- `nethack.bin`: `5cf76bd0a369dad86c106e062dd7b0e46b595a349dcc15111f906b502cb7ccb7`
- `nethack.uf2`: `e9c0acce1198b6d7f3d52d8c2e4f3820167da1f6a2e90c1a71fcd795e89bb9a6`
- `nethack-volume.uf2`: `8c61340aea56212da0a1d29603460cde03adb8d7d1cf4959cac2e271b5e68b1e`
