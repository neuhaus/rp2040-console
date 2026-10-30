# Raspberry Pi Pico validation — 30 October 2026

**A Rogue character went from level 1 to the bottom of the dungeon (level
99) and back to the surface with the Amulet of Yendor, a full pack, and two
save/restore checkpoints, then won through the ordinary release firmware.**
Every one of the 228 checks passed. In a separate test, 30 soft-reboot
sessions saved, restored and quit on one USB connection without a reset.

The board is an RP2040 with 2 MB flash, connected over USB CDC to macOS.
Tools: Pico SDK 2.3.1, Arm GNU Toolchain 15.3.Rel2 (GCC 15.3.1/newlib), and
a picotool built with libusb. Rogue is upstream `mpwillson/rogue` at
`c0386a6`. Raw serial logs, `results.json` and FAT images are under
`build/rogue-ascension/` and `build/rogue-soft-test/` in this workspace. The
method is described in [INTERNALS.md](INTERNALS.md#validation-script).

## Bugs found on hardware

The first runs turned up four faults. All were fixed in rp2040-console, not
in Rogue:

| Symptom | Cause | Fix |
| --- | --- | --- |
| `*** PANIC *** double support is disabled` the first time a monster hit | the platform linked the SDK's "none" double implementation; Rogue scales damage by a `double` | `FLOATING_POINT` manifest flag |
| every restore: `sorry, file has been touched` | `stat()` reported a folded date encoding and `gettimeofday()` counted from 1970 at boot, so neither was comparable with the time Rogue stores in the save | both now report Unix seconds on the session clock |
| restore impossible | Rogue restores only from `argv[1]` | `RESTORE_ARG` |
| link error: `select` | Rogue's curses uses it to distinguish arrow keys | console `select()` |

## Firmware

| Build | Firmware | Static SRAM | Heap budget | Stack reserve |
| --- | ---: | ---: | ---: | ---: |
| Ordinary | 248,288 B | 53,116 B | 184,452 B | 32,768 B |
| Diagnostic (`ROGUE_DIAG`) | 249,400 B | 53,136 B | 184,432 B | 32,768 B |
| Soft reboot | 257,032 B | 54,512 B | 183,056 B | 32,768 B |

The firmware uses 16% of the 1.5 MB below the data volume.

## Ascension run

Levels 1–4 were left by walking to the stairs and using the ordinary `>`.
Wizard `>` went from 4 to 26. On level 26 the generated Amulet was walked to
and picked up, and level 26 was left by the real stairs. Wizard `>` then went
from 27 to 99, plus one more `>` at 99, which regenerates the bottom level.
After the deep checkpoint, wizard `<` climbed to level 3, an ordinary `<`
reached level 2 on the stairs, and a wizard `<` reached level 1.

Memory was sampled on every level: 200 samples in all.

| Measure | Peak | Budget |
| --- | ---: | ---: |
| Heap in use (`mallinfo`) | 4,176 B | 184,452 B |
| Heap arena grown | 8,304 B | 184,452 B |
| Least heap left to grow | 176,128 B | — |
| Stack depth | 6,504 B | 32,768 B |
| Floor objects on one level | 21 (level 55) | — |
| Monsters on one level | 24 (level 17) | — |

The stack peak includes the save and score paths. Because Rogue discards each level, these numbers do not grow with
depth: level 99 needed no more than level 30.

### Staircase commands (wizard mode off)

| Check | Result |
| --- | --- |
| `>` off the stairs on level 1 | refused: "I see no way down" |
| `>` on the stairs, levels 1→2→3→4 and 26→27 | descended |
| `<` on the stairs without the Amulet (level 4) | refused: "Your way is magically blocked" |
| `<` off the stairs with the Amulet (level 3) | refused: "I see no way up" |
| `<` on the stairs with the Amulet (level 3) | reached level 2 |
| `<` on the stairs with the Amulet (level 1, ordinary firmware) | won the game |

### Checkpoints

| Checkpoint | Level | Pack | Save file | Volume free | FAT | Restore |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| deep | 99 | 24 entries, full, with Amulet | 9,567 B | 491,008 B | clean | identical (DIAG) |
| surface | 1, on the stairs | 24 entries, with Amulet | 9,119 B | 491,520 B | clean | identical (ordinary firmware: level, HP, Amulet) |

The game reported `pack full` at its limit of 24, which counts stacked
items; in an earlier run the same limit was reached with 23 entries.

### Win and scores

On the ordinary firmware, after switching off the restored wizard flag, the
ordinary `<` showed the "YOU WON" banner and the Fighters' Guild text. It
then sold the pack (the Amulet of Yendor for 5,000), showed the score list,
and exited to the restart prompt. The score list was empty, as Rogue intends:
a game that used wizard mode is score-only. Rogue also keeps the save file
after restoring a wizard-mode game.

A fresh ordinary game on a fresh volume was then quit. The imaged volume's
`rogue.scores`, decoded with Rogue's cipher, reads
`rogue: quit on level 1`. Rogue opens this file with the mode `"r+w"`, which
works on this platform. All five FAT images pass `fsck.fat -n`.

## Soft reboot

The `ROGUE_DIAG` + `ROGUE_SOFT_REBOOT` firmware ran 30 sessions with one serial
connection held open throughout. Each session switched on wizard mode and went
down 2–8 levels. Every third session then saved, with wizard mode switched off
first so that the restore deletes the save. The other sessions quit.

| Check | Result |
| --- | --- |
| USB connection after each exit | the same device for all 30 sessions |
| sessions that followed a save | 10 of 10 restored at the saved level, pack and HP |
| sessions that followed a quit | 20 of 20 fresh: level 1, max level 1, wizard off |
| least heap left to grow, session 1 → 30 | 180,224 B → 176,128 B |
| heap arena, session 1 → 30 | 2,808 B → 6,904 B |

All 61 checks passed. The arena grew once, by 4 KB, between the first and
second session, the first save, and was then flat for the remaining 28
sessions; an earlier run was flat from the first session. The stack peaked
at 4,624 B.

Every boot and every soft-reboot session asked "Who are you?" (`ASK_ENV`).
The tests pressed Enter to keep the remembered name, which a restore
requires. A separate check on the soft-reboot firmware typed a new name: the
quit was scored under it, and the next session offered it as the default.

## Limits

- **Not a natural ascension.** Wizard mode supplied travel and objects. The
  character was strengthened with potions of raise level, and fights were
  incidental.
- **One run, one seed sequence, fresh volume.** Each level is generated from
  scratch, so the per-level figures cover what was generated, not every
  possible level.
- **Peak measurement.** Stack is measured by a painted reserve, checked on
  every keypress. A deeper transient between keypresses would still be
  caught, because paint once disturbed stays disturbed.
- **Not tested:** power loss during a write; Hack or the NetHack ports on
  hardware with the changed platform. restoHack was compiled and linked
  against it, and the console host tests and ARM integration fixture pass.

## Artifact checksums (SHA-256)

- `rogue.uf2` (ordinary, used for the win): `2e493c48fc1f561b6010186810d5d1c34f0010ecb0d1ead34b9a9a5cfbfb98df`
- `rogue-volume.uf2`: `c211ae30d8e092e10ef303630f9960ea9233fa2bebc348a1ca2a5530c227734a`
- diagnostic `rogue.uf2`: `74031bbce4cfd788df9fd017fd7ffb68ab8873233b91f66767a882ddb4396dcd`
- soft-reboot `rogue.uf2`: `ca3ca12a139e0ef4118c0f3874256618152d999067b2050cd95df5b13765ba18`
- soft-reboot diagnostic `rogue.uf2`: `2a2b244747bb8c82009ad0d72ed68cb04cfc3a7f2c9b82f516a3c537966017ca`
