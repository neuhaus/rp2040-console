# NetHack 3.1.3 on a 2 MB Pico

Source: [Vanilla-NetHack/NetHack](https://github.com/Vanilla-NetHack/NetHack),
commit `df89f069705251b66c74322ed01b3d30d670979e`.
The recipe applies configuration and storage patches in a private generated
source tree and adds the CMake manifest, compatibility header, RNG adapter and
layout probes. The supplied upstream Git checkout remains unchanged. Keep the
upstream `dat/license` with distributions; it is included in the FAT image.

## Build

Install the [common prerequisites](../../README.md#build-an-application) plus the 32-bit host compiler
and parser generators. On an x86-64 Debian/Ubuntu host:

```sh
sudo apt install gcc-multilib libc6-dev-i386 bison flex patch
python3 applications/nethack-3.1.3/build.py
# Or reuse an existing upstream Git checkout:
python3 applications/nethack-3.1.3/build.py --source /path/to/nethack
```

On Apple Silicon (or another host without a compatible 32-bit compiler), install
`qemu-system-arm` and use the included ARM/newlib semihosted generators:

```sh
python3 applications/nethack-3.1.3/build.py --qemu-generators
```

Run the build commands from the console repository root with `PICO_SDK_PATH` set.
Without `--source`, the script clones upstream into its build directory. It always
archives the pinned commit, regardless of the checkout's current branch or edits.
`--build-dir /absolute/path` selects a different private output directory;
`--jobs 4` controls ARM build parallelism. A nonempty unrelated output directory
is rejected. The recipe owns and regenerates its `source/` tree; do not edit it.
Edit this recipe's `port/` files instead.

The default host command is `gcc -m32`. The datagenerators must use 32-bit `long`
and compatible structure layouts; a native 64-bit build is insufficient. On
other hosts, supply a suitable compiler/wrapper and runner, for example:

```sh
export HOST_CC=i686-linux-gnu-gcc
export HOST_CFLAGS=''
export HOST_RUNNER='qemu-i386 -L /path/to/i386/sysroot'
python3 applications/nethack-3.1.3/build.py --source ./nethack
```

`HOST_CC`, `HOST_CFLAGS` and `HOST_RUNNER` are shell-style argument lists, parsed
without shell execution. `HOST_LD_LIBRARY_PATH` optionally sets library lookup
for executed host generators. The ARM compiler remains `arm-none-eabi-gcc`.

## Outputs and checks

Default outputs are under `build/nethack-3.1.3/hardware/`:

- `nethack.uf2`: XIP firmware below flash offset `0x100000`.
- `nethack-volume.uf2`: 1 MB FAT volume at `0x100000`, ending at 2 MB.
- `nethack.elf`, `nethack.bin` and `nethack-volume.img`: inspection artifacts.

The build generates all game data, checks 77 host/ARM sizes and offsets, validates
firmware/heap/stack bounds, checks FAT with fsck and verifies UF2 addresses and
family IDs. `HEAP_END=0x2003c000` leaves 24 KB above the heap for stack/SDK scratch;
measured hardware peaks and limits are recorded in [RESULTS.md](RESULTS.md). The manifest uses `LEGACY_C`, `LEGACY_TIME32`
and `RENAMES panic=nh_panic`.
The configuration enables `NO_FILE_LINKS` with `LOCKDIR="/"`, so NetHack
creates lock files exclusively instead of requesting hard links unsupported by
FAT. The image check rejects a compiled lock implementation that calls `link()`.
DEC font selection uses ESC(0/ESC(B, without SO/SI control characters. In
particular, SI is Ctrl-O, which screen's serial line discipline may consume as
VDISCARD; hardware logs showed missing SI bytes and unsolicited newline input.
Startup uses a terminal reset to clear a character shift left by older firmware.

For optional application restart while preserving the USB runtime:

```sh
python3 applications/nethack-3.1.3/build.py --source ./nethack --soft-reboot
```

Outputs then go to `build/nethack-3.1.3/soft/` with the same filenames and flash
layout. Hardware reboot is the default. Read the [supported single-threaded C profile](../../INTERNALS.md#optional-soft-reboot)
before enabling soft reboot.

Both restart modes pass the local ARM build and image checks. Physical soft
restart remains unverified. The hardware workload and its limits are recorded
in [RESULTS.md](RESULTS.md).

## Storage format

Levels, bones and saves retain NetHack's RLE/zero-run encoding and then use
framed Tamp compression. Each frame holds at most 1 KiB of input and has a
CRC32; an end marker detects truncated streams. All codec writes are checked.
The independent encoder and decoder use fixed SRAM buffers and no heap.
Tamp 2.5.0 is shared with the 3.6.7 recipe; retain its
[MIT license and provenance](../nethack-3.6.7/port/tamp/UPSTREAM.md).

The original port could silently restore empty level files when it ran out of
flash while retaining both the combined save and reconstructed dungeon. This
format reduces both copies enough for the measured workload and rejects short
writes instead of reporting a successful restore of missing levels.

**Older saves and bones are incompatible. Flash both images and start a new
game when upgrading.** The save still contains the whole dungeon and restore
still needs room for the save plus reconstructed levels. Storage use remains
bounded by the 1 MiB FAT volume; unusual object populations can exceed the
measured workload. Save before disconnecting; FAT writes are not power-loss safe.

## Regression and hardware tests

The codec test compiles the actual patched save/restore adapters with native
AddressSanitizer and UndefinedBehaviorSanitizer. Use a generated source tree
from a completed build:

```sh
python3 applications/nethack-3.1.3/tests/test-codec.py \
  --source build/nethack-3.1.3/source
```

Build a separate maximum-branch wizard image for destructive board testing:

```sh
python3 applications/nethack-3.1.3/build.py --qemu-generators \
  --sram-diagnostics --build-dir build/ascension
```

Flash both diagnostic images, close other serial terminals, set `PICOTOOL` if
needed, and run each stage in order from the console repository root:

```sh
python3 applications/nethack-3.1.3/tests/hardware.py tour --out build/results
python3 applications/nethack-3.1.3/tests/hardware.py checkpoint --out build/results
python3 applications/nethack-3.1.3/tests/hardware.py revisit --out build/results
python3 applications/nethack-3.1.3/tests/hardware.py planes --out build/results
```

The stages carry 150 test objects, visit all 77 non-endgame levels, restore and
verify every cached level, revisit/map the full dungeon, then checkpoint on each
of the five planes. Logs, FAT snapshots and before/after counts are retained.
The final Astral checkpoint places the character at a co-aligned high altar for
the ordinary `#offer` command. Forced wizard travel tests storage and execution;
it does not replace a naturally played combat ascension.

Diagnostics are disabled in ordinary builds. Without `--build-dir`, diagnostic
outputs use `build/nethack-3.1.3-diagnostic/`. Do not combine diagnostics with
`--soft-reboot`. Diagnostic command names automatically complete in 3.1.3;
the driver sends their shortest unambiguous prefixes.
