# Frotz

[Frotz](https://gitlab.com/DavidGriffith/frotz) is an interpreter for
Infocom and other Z-machine interactive-fiction games. This recipe builds
Frotz's *dumb* terminal front end on the rp2040-console platform so that games
can be played over the USB serial console.

## Source and licence

Frotz is **GPL-2.0-or-later**. Building it into an image is distribution, and
whatever you flash is covered by that licence, unlike the BSD-licensed platform.
The recipe does not bundle a game: a story file is somebody else's copyright and
this recipe cannot grant a right to it. Supply your own freely distributable or
purchased story file.

The fetched checkout is used exactly as cloned - the recipe never edits it. The
port itself lives entirely in this repository (the platform gained the `ARGS`
manifest option and a POSIX `basename()` for it, and the generated
`defs.h`/`hash.h` are written into the build tree instead of the checkout). The
default branch does carry the dumb-interface fixes described in
[Limitations](#limitations); those are separate upstream-bound commits, not port
patches.

Validated against upstream Git HEAD `042d7bca` (`2.55-31-g042d7bc`,
`VERSION 2.56pre`) with three dumb-interface fix commits on top.

## Getting Frotz

The build clones Frotz for you, into `build/frotz/upstream`, and leaves it
untouched. Until the dumb-interface fixes (see [Limitations](#limitations)) are
merged, it fetches the `dumb-fixes` branch of the `sn_/frotz` fork, which carries
all of them. The fixes are offered upstream as three independent branches, each
based on `master` - `dumb-fix-interior-free`, `core-read-timeout` and
`dumb-no-float` - and `dumb-fixes` is a merge of the three; `--branch` builds any
of them, `--repo` points the clone elsewhere, and `--source` builds an existing
checkout instead. `--pull` fast-forwards the managed checkout to the branch tip
first.

### Getting a story file

The [Interactive Fiction Archive](https://ifarchive.org/if-archive/games/zcode/)
has many story files, free ones included. Small version 3 games fit the memory
budget comfortably; version 5 and later titles are larger and may not (see
[Memory](#memory)). Tell the build which one to install with `--story`.

## Building

Install the [common prerequisites](../README.md), set `PICO_SDK_PATH`, and name
the story file you want on the volume:

```sh
applications/frotz/build.py --story /path/to/game.z3
```

Without `--story` the interpreter is built alone and the image boots to Frotz's
usage text; a volume can still be produced, but it has no game on it.

Other options: `--extra-args "-u 4 -f ansi"` compiles extra frotz arguments in
before the story file, `--heap-end` and `--stack-reserve` size the memory,
`--build-dir DIR` and `--jobs N` are the usual knobs, and `--source DIR` builds
an existing checkout. `applications/frotz/build.py --help` lists them all.

Outputs: `build/frotz/frotz.uf2` and `build/frotz/frotz-volume.uf2`.

## Flashing and playing

Flash both UF2 files with BOOTSEL as usual, then open the serial port at 80x24
(`screen /dev/ttyACM0`). Type commands at the `>` prompt and press Enter. The
story file is loaded by name from the root of the volume, and saves, transcripts
and command files are written there.

Because the dumb front end reads a whole line for every keypress, single-key
prompts and menus also need Enter.

## Memory

Frotz loads the whole story file into RAM and keeps a full copy of its dynamic
memory plus a 1.5x difference buffer for the undo feature. The build prints an
estimate at configure time:

```
-- Frotz story: v5, 137752 B, dynamic 17864 B; fixed working set about 207374 B
```

and the platform prints the heap it actually has. The two have to agree, with
room left for undo blocks, which grow during play. The defaults
(`FROTZ_HEAP_END 0x2003c000`, `FROTZ_STACK_RESERVE 24576`) leave about 200 KB of
heap. Version 3 games of the classic 80–90 KB size fit comfortably; a 138 KB
version 5 game sits close to the limit. Raise the heap with `--heap-end` (and
lower `--stack-reserve`, checking the reserve the build enforces), or reduce
space with `--extra-args "-u 4"`.

## Limitations

- The dumb front end, not curses. There is no ncurses on this platform, so
  screen-oriented version 6 games and the curses interface are out of scope.
- Blorb support is compiled out (`NO_BLORB`) because it drags in double-precision
  arithmetic the platform builds without. Give it a plain story file, not a
  `.zblorb`.
- The upstream dumb interface has bugs that building `master` would leave in
  place; they are fixed on three branches based on `master`, which this recipe
  fetches merged as `dumb-fixes`:
  - `os_read_file_name()` frees a pointer into a stack buffer on the auxiliary
    save/restore path (V5+ `@save`/`@restore` with operands), corrupting the heap
    (`dumb-fix-interior-free`).
  - `z_read()` passes an uninitialized timeout to the dumb front end, so its
    timed-input path runs on a stale value (`core-read-timeout`).
  - That front end's speed factor was a `float`, parsed with `atof()` and printed
    with `%g`; with floating point deliberately disabled on this platform it
    panics. A fixed-point factor removes the last of it (`dumb-no-float`).
  See [Getting Frotz](#getting-frotz).
- Hardware validation is pending: image construction and linking are checked,
  but no Pico has run this image yet.
