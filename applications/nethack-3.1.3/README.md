# NetHack 3.1.3 on a 2 MB Pico

Source: [Vanilla-NetHack/NetHack](https://github.com/Vanilla-NetHack/NetHack),
commit `df89f069705251b66c74322ed01b3d30d670979e`.
The recipe applies configuration changes in a private generated source tree and
adds the CMake manifest, compatibility header, RNG adapter and layout probes.
Upstream game C files and the supplied Git checkout remain unchanged. Keep the
upstream `dat/license` with distributions; it is included in the FAT image.

## Build

Install the [common prerequisites](../README.md) plus the 32-bit host compiler
and parser generators. On an x86-64 Debian/Ubuntu host:

```sh
sudo apt install gcc-multilib libc6-dev-i386 bison flex patch
git clone https://github.com/Vanilla-NetHack/NetHack.git nethack
git -C nethack checkout df89f069705251b66c74322ed01b3d30d670979e
python3 applications/nethack-3.1.3/build.py --source ./nethack
```

Run the last command from the console repository root with `PICO_SDK_PATH` set.
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
actual stack peaks are unmeasured. The manifest uses `LEGACY_C`, `LEGACY_TIME32`
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

Both firmware modes and image checks are tested in the cloud. The underlying
platform restart passed 60 ARM/newlib sessions; the separately hosted game was
tested for new game/save/restore. This recipe builds ARM images and does not
run that hosted gameplay test. Physical Pico boot, USB continuity, flash writes,
full game sessions and peak RAM usage remain unverified.
