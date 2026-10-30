# Rogue port internals

## Source

The game sources are compiled unmodified. On the first configure,
`FetchContent` clones <https://github.com/mpwillson/rogue> at the pinned
master commit `c0386a6ed2800f7769a76c4821a5397f165b8b0e`. Three CMake cache
variables override this:

| Variable | Effect |
| --- | --- |
| `ROGUE_SRC` | build a local checkout (the directory holding `rogue.h`) instead of cloning |
| `ROGUE_GIT_REPOSITORY` | clone another repository |
| `ROGUE_GIT_TAG` | build another revision |

```sh
sh applications/rogue/build.sh "$PWD/build/rogue" -DROGUE_SRC=/path/to/rogue
```

## Manifest

`CMakeLists.txt` compiles Rogue with its own termcap-driven curses
(`RCURSES`). The volume holds:

- `rogue.instr`, the `?` help screen, converted to LF line endings
- `rogue.termcap`, the xterm entry this recipe ships
- an empty `rogue.scores`

The volume uses the default 512 KB at flash offset `0x180000`.

The environment sets:

- `TERM=xterm` and `TERMCAP=/rogue.termcap`, so Rogue's curses reads the
  entry from the volume.
- `LOGNAME=rogue` and `HOME=/`. `md_gln()` dereferences `getpwuid()` without
  checking for NULL. Every score is recorded under `rogue`, the name a Unix
  login would have supplied; `ROGUEOPTS=name=...` sets another display name.
- `ROGUEOPTS=file=rogue.save`, which makes `rogue.save` the save prompt's
  default. The `ROGUE_EXTRA_OPTIONS` cache variable (empty by default) adds
  more comma-separated entries after it, e.g.
  `-DROGUE_EXTRA_OPTIONS=name=Sven,nojump`.

`RESTORE_ARG rogue.save` passes the save to Rogue as `argv[1]` whenever the
file exists at boot. That is Rogue's only way to restore. Rogue deletes the
file after restoring a normal game, but keeps it for a wizard-mode game.

`FLOATING_POINT` is required: monster damage is scaled by a `double` in
`hit.c`, and `^A` averages in doubles.

## Platform changes

The port needed these changes to rp2040-console. None required editing
Rogue's sources.

| Change | Why |
| --- | --- |
| `select()` on the console, with one byte of input pushback | Rogue's `getch()` uses it to tell an arrow-key sequence from a lone Escape. It did not link before. |
| `gettimeofday()` reads the session clock | The SDK's version counted from 1970 at boot, decades away from `time()` and file times. |
| `stat()` reports Unix seconds | It used to report an order-preserving encoding that compared only with itself. Rogue compares a save's mtime with the time stored inside it ("file has been touched"). The `.mtimes` table format moved to `MTM4`; older tables are discarded. |
| `getpwuid()` returns an entry when `LOGNAME` is set | This is opt-in, so programs without `LOGNAME` see no change. |
| `RESTORE_ARG` manifest option | Passes a save file as `argv[1]` only when it exists. |
| `FLOATING_POINT` manifest option | The default "none" float/double implementation links cleanly but panics (`double support is disabled`) at the first floating-point operation. Rogue hit this the first time a monster struck the player. |

## Soft reboot

`-DROGUE_SOFT_REBOOT=ON` builds with the platform's `SOFT_REBOOT`. Quitting,
saving or winning then restarts the game in place: USB stays connected, and
the platform resets Rogue's globals and frees what it allocated. Because
`RESTORE_ARG` is checked at the start of every session, a save made in one
session resumes in the next. Soft reboot is off by default, matching the
other recipes. Its hardware validation is in [RESULTS.md](RESULTS.md#soft-reboot).

```sh
sh applications/rogue/build.sh "$PWD/build/rogue-soft" -DROGUE_SOFT_REBOOT=ON
```

## Memory layout

`HEAP_END 0x2003a000` with `STACK_RESERVE 32768` gives a 185 KB heap and a
32 KB stack reserve. The platform default leaves 8 KB of stack. Rogue keeps
screen-sized buffers on the stack in its help, score and save code, and its
heap use is a few KB. See [RESULTS.md](RESULTS.md) for measured peaks.

Rogue does not keep levels. Each staircase discards the current level and
generates a new one, so memory depends on a single level and the pack, not
on how deep the player has been. Freed objects go to Rogue's own free list.

## Diagnostic firmware

`-DROGUE_DIAG=ON` adds `port/diag.c` and links with `-Wl,--wrap=rgetchar`.
Every key the game reads, from commands and prompts alike, passes through the
wrapper, which samples memory before and after the read:

- **Heap:** the arena's remaining growth (`__HeapLimit - _sbrk(0)`) and
  `mallinfo()` bytes in use, kept as running peaks.
- **Stack:** the reserve is painted on the first call, and the deepest
  disturbed byte is found by scanning upward.

Two keys are consumed by the wrapper and never reach the game:

| Key | Output |
| --- | --- |
| `^]` (0x1d) | a `DIAG ... END` line: level, wizard flag, object/monster/pack counts, Amulet, HP, heap and stack figures |
| `^^` (0x1e) | `MAP ... MAPEND`: the player's position, every cell's `dungeon[][]` flags, and each floor object's position and type |

The diagnostic firmware is for validation only and must not be shipped.

## Validation script

`tests/ascension.py` erases the board. It flashes the diagnostic firmware and
a fresh volume, then drives the game over USB serial. `tests/term.py` provides
the serial session and a VT100 screen model that covers what Rogue's curses
emits.

Wizard mode (`^W`, password `bathtub`) is used for travel, magic mapping and
object creation. In wizard mode, `>` and `<` skip their staircase and Amulet
checks, so wizard mode is switched off for every staircase command whose
ordinary behaviour is being tested. Walks use the `^^` map and follow
`can_move()`'s rules: no diagonal moves through doors or past missing
corners, and no traps. They avoid monsters when another route exists.

The run, in order:

1. A refused `>` off the stairs, then ordinary descents from level 1 to 4.
2. A refused `<` without the Amulet. Potions of raise level then strengthen
   the character.
3. Wizard descent to level 26. The test walks to the generated Amulet, picks
   it up, and takes the real stairs down. Wizard descent continues to level 99.
4. The pack is filled until the game reports it full.
5. Checkpoint: save, image the FAT region, `fsck.fat -n`, reboot, automatic
   restore, and comparison of the DIAG state.
6. Wizard ascent to level 3, a refused `<` off the stairs, and an ordinary
   `<` up the stairs with the Amulet to level 2. A wizard `<` reaches level 1,
   and the test walks onto the staircase.
7. Checkpoint again, this time restored on the ordinary firmware. Wizard mode
   is switched off, and the ordinary `<` with the Amulet wins the game.
8. A fresh ordinary game is quit, and the score file is checked on the
   imaged volume.

```sh
sh applications/rogue/build.sh "$PWD/build/rogue"
sh applications/rogue/build.sh "$PWD/build/rogue-diag" -DROGUE_DIAG=ON
python3 applications/rogue/tests/ascension.py --diag build/rogue-diag \
    --ordinary build/rogue --picotool /path/to/usb-enabled/picotool \
    --out build/rogue-ascension
```

`tests/soft_reboot.py` checks a `ROGUE_DIAG` + `ROGUE_SOFT_REBOOT` build. It
keeps one serial connection open across 30 sessions. Each session switches on
wizard mode and goes down a few levels, then either saves or quits. The test
requires that the connection survives every exit and that each session
restores the save or starts fresh with reset globals. It also checks that
the heap does not grow from session to session.

```sh
sh applications/rogue/build.sh "$PWD/build/rogue-soft-diag" \
    -DROGUE_SOFT_REBOOT=ON -DROGUE_DIAG=ON
python3 applications/rogue/tests/soft_reboot.py --build build/rogue-soft-diag \
    --picotool /path/to/usb-enabled/picotool --out build/rogue-soft-test
```

The scripts need a picotool built with libusb. The copy the SDK builds for
UF2 conversion may lack USB support; on macOS, configure picotool's source
with Homebrew's `libusb` installed. It also needs `fsck.fat` and mtools.
Outputs are written to `--out`: raw serial logs, `results.json` with every
sample, and the FAT images.
