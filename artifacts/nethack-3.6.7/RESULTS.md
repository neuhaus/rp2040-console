# Validation status — 15 October 2026

**Current format `0x03060781`: clean diagnostic build and regression tests pass;
many-level hardware save/restore validation is pending.**

The single-checkpoint implementation retains compressed level files, uses a
current-level/global companion, verifies a size/CRC32 manifest and publishes a
completed temporary companion by rename. Missing, corrupted, truncated and
duplicate manifest entries, orphaned temporary cleanup and file publication are
covered by sanitizer tests against the actual implementation.

The current diagnostic build is **1,549,716 B firmware, 137,044 B static SRAM,
112,812 B heap and 20,480 B stack reserve**. All 138 data entries, host/target
layouts and image bounds pass. All 120 operand programs, 100 pooled sessions,
compact room tests and 2,240 codec cases pass. These results do not establish
that the new format fixes the observed flash problem on hardware yet.

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
code. Its many-level hardware rerun remains outstanding; see RESUME.md.
Increasing the stack reserve or undoing SRAM reductions would not address
the measured storage failure.

The maximum recorded stack use across the packaged hardware tests was
12,904 B, leaving 7,576 B of the 20 KiB reserve (6,552 B above the monitored
guard). Every sampled guard remained intact and no SRAM allocation failed.

These wizard scenarios are allocation and storage stress tests, not a complete
ascension or coverage of every gameplay path. A 1,024 B painted stack guard
provides detection at sampled points, not hardware protection.
