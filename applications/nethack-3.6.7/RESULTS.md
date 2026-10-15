# Validation status — 15 October 2026

**Format `0x03060781` passes clean builds, regression tests, a 49-record Pico
save/restore workload, all 48 cached-level revisits and a second save/restore.**

The single-checkpoint implementation retains compressed level files, uses a
current-level/global companion, verifies a size/CRC32 manifest and publishes a
completed temporary companion by rename. Missing, corrupted, truncated and
duplicate manifest entries, orphaned temporary cleanup and file publication are
covered by sanitizer tests against the actual implementation.

The current diagnostic build is **1,549,940 B firmware, 137,044 B static SRAM,
112,812 B heap and 20,480 B stack reserve**. All 138 data entries, host/target
layouts and image bounds pass. All 120 operand programs, 100 pooled sessions,
compact room tests and 2,240 codec cases pass. The following hardware run confirms that restore retains cached levels in
place and avoids the earlier dungeon duplication for this workload.

## Single-checkpoint hardware validation (15 October)

The fresh diagnostic image generated fixed-seed Ludios, added 30 equipped
peaceful soldiers and a bag with 150 separate rings, then visited every depth
1–48 and returned to Ludios. It retained **49 overview records**. Natural
simulation changed entity counts during the tour; the exact saved/restored
comparison below uses the state after returning, without intervening game turns.

| Check | Result |
|---|---:|
| Monsters before / after restore | 176 / 176 (including one migrating) |
| Objects before / after restore | 1,037 / 1,037 |
| Contained objects before / after restore | 156 / 156 |
| Overview records before / after restore | 49 / 49 |
| Companion size (`save/0game`) | 25,333 B |
| Cached levels retained | 48 |
| Flash free after save / restore | 27,136 / 52,736 B |
| Highest allocated heap through save | 108,536 B |
| Restore allocated heap peak | 107,324 B |
| Highest stack use through save | 13,128 B of 20,480 B |
| Restore stack use | 4,048 B |
| Failed SRAM allocations | 0 |

Both captured FAT images pass `fsck.fat -n`. All 48 cached level files have
identical sizes and SHA256 hashes before and after restore. The companion is
removed and only the normal `.0` lock is added; there is no second dungeon copy
and no `0game.tmp` after saving. This confirms the storage fix for this workload.

SRAM remains tight: after the tour, 2,016 B were free inside the allocator arena
and 3,444 B remained available for arena growth (largest free block 924 B).
After restore, those values were 320 B, 5,168 B and 156 B respectively. These
are separate sources of available heap, not a guarantee that any allocation of
their sum will succeed. The painted stack guard remained intact. The highest
observed stack use leaves 7,352 B in the reserve; it does not justify reducing
the reserve without broader gameplay coverage.

All 48 cached depths were revisited after resume, then a second save/restore
passed. The second companion was 25,150 B with 21,504 B of flash free after
saving. The exact comparison retained 176 monsters, 1,032 objects (156
contained) and 49 overviews. Save allocation peak was 108,664 B; restore peak
was 107,332 B. Both recorded zero allocation failures and intact guards. The
smaller flash margin includes growing level state and the appended memory log.

The first revisit test exposed cached levels retaining the previous process ID.
The corrected build restores that original game owner from the verified
companion and updates only the `.0` lock. The full cached-level tour and second
cycle above used this correction; no ownership warnings occurred. Regression
tests also reject incorrect owners and ledger numbers across repeated resumes.

Normal quit removed every cached level and left a clean FAT volume with
192,000 B free. Booting a captured volume containing 48 cached levels but no
checkpoint started a fresh game and removed all 48 orphaned levels; its FAT
image was clean.

The ordinary build is 1,549,780 B firmware, 136,988 B static SRAM and 112,868 B
heap, with the same 20,480 B stack reserve. It passes archive, layout, FAT and
image-bound checks. Remaining branches, endgame with this accumulated history
and a complete ascension remain unverified. Physical soft reboot remains
untested.

## Historical combined-file format (`0x03060780`, 14 October)

The earlier many-level save succeeded, but restoring it exhausted the volume.
The following measurements describe that earlier implementation, not the new
single-checkpoint format. They motivated retaining level files in place.

Built through the artifact script from the pinned clean NetHack source and
console `e87f81612d35a9f35054ea565fcbef549c49591f`, using Pico SDK 2.3.1,
ARM GCC 15.3.1/newlib 4.6, Ninja and QEMU-generated ILP32 data. No LTO.

| Build | Firmware bytes | Static SRAM | Heap budget | Stack reserve |
|---|---:|---:|---:|---:|
| Ordinary hardware reboot | 1,549,812 | 137,076 | 112,780 | 20,480 |
| Final diagnostic hardware reboot | 1,549,636 | 137,132 | 112,724 | 20,480 |
| Optional soft reboot | 1,552,432 | 137,992 | 111,864 | 20,480 |

The ordinary candidate booted on the Pico to its normal USB date/time prompt
with a fresh data volume. This was the earlier startup smoke check.

Builds passed target-inflater/zlib checks for all 138 archive entries, data
layout comparisons, FAT/image checks, and flash/SRAM bounds. Tests compared
all operands and strings in all 120 special-level programs, exercised nested
compact room records, and reclaimed leaked pooled slots across 100 simulated
soft sessions while retaining a platform allocation. The actual save/restore
codec adapters also passed 2,240 native cases with address/undefined-behavior
sanitizers. Soft reboot's physical USB lifecycle was not tested here; its
per-allocation tracking costs additional SRAM. Hardware reboot is recommended.

Pico tests used the combined changes together:

- Fixed-seed Ludios generation peaked at 86,272 B of allocated heap.
- Adding 30 equipped soldiers produced 177 monsters and 891 objects. Save and
  restore passed; restored allocation peak was 93,824 B.
- Adding a bag with 150 separate rings produced 1,042 objects. Save peaked at
  105,312 B; restore preserved both entity counts and peaked at 104,296 B.
- Sixteen mapped travel probes recorded 9,008 B of stack use.
- Traversal through Castle and Gehennom reached Sanctum while carrying the
  extra inventory. A pager skipped depth 29; the other requested depths 2–48
  had confirmed transition samples. Level 1 was also reloaded through the menu.
- Revisiting Ludios retained 48 overview records and the 150 test rings.
  Entity totals changed with elapsed game simulation; this was not an identical
  frozen-world comparison. Live heap reached 107,820 B. Fragmentation left
  2,476 B of growth space, with 2,428 B in allocator free blocks and a largest
  free block of 1,412 B.
- A separate recovery of the smaller inventory-heavy fixture generated Earth,
  Air, Fire, Water and Astral successfully while retaining the 150 carried rings.
  This was a separate session; it did not retain the 48-record history.
- Saving that accumulated state succeeded at a 108,996 B allocation peak. Its
  save file was 156,130 B, leaving 39,424 B on the volume.
- Restoring it failed: `cannot write 512 bytes to file #4`, with zero volume
  space remaining. Allocated heap peaked at 105,768 B, with no failed SRAM
  allocation and an intact stack guard. FAT inspection confirmed that the
  combined save coexisted with the recreated Ludios file and levels 1–9;
  level 10 could not be written. The pre-test volume was preserved.

The failure is temporary flash duplication during restore: the combined save
remains allocated while its constituent level files are reconstructed. SRAM
optimizations do not remove that duplication. Smaller room/overview records
help, but their omitted zero fields already compressed well. The small crowded
save was 24,520 B versus 24,786 B for an earlier same-count fixture; differing
message history means this is an observed size comparison, not a pure format A/B.
The inventory-heavy small save was 25,735 B and restored successfully.

The current single-checkpoint implementation addresses that duplication in
code. Its many-level hardware save/restore and cached-level reruns passed as
described above.
Increasing the stack reserve or undoing SRAM reductions would not address
the measured storage failure.

The maximum recorded stack use across these historical hardware tests was
12,904 B, leaving 7,576 B of the 20 KiB reserve (6,552 B above the monitored
guard). Every sampled guard remained intact and no SRAM allocation failed.

These wizard scenarios are allocation and storage stress tests, not a complete
ascension or coverage of every gameplay path. A 1,024 B painted stack guard
provides detection at sampled points, not hardware protection.


## Vendored Tamp revision

Tamp is pinned to upstream 2.5.0/main commit
`12e2e6aa49619cc7391c00229d30d258e2b564da` and used under its MIT option.
All seven C source/header files are byte-identical to those used for the
hardware measurements above. The license/provenance update therefore leaves
the codec, firmware and SRAM measurements unchanged. The updated vendored copy
passed all 2,240 native sanitizer codec cases.
