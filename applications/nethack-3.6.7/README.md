# NetHack 3.6.7 on RP2040

Play NetHack on a standard Pico with 2 MiB flash over USB serial. No SD card is
needed.

## Build

Install the [console prerequisites](../../README.md#build-an-application), plus
Bash, Bison, Flex and patch. Set `PICO_SDK_PATH` to your Pico SDK 2.3.1 directory
and put the ARM toolchain, dosfstools and mtools on `PATH`.

Run from the `rp2040-console` repository root. On Apple Silicon, install QEMU
with `qemu-system-arm` and run:

```sh
applications/nethack-3.6.7/build.sh --qemu-generators --jobs 4
```

On x86-64 Linux, install `gcc-multilib` and `libc6-dev-i386`, then run:

```sh
applications/nethack-3.6.7/build.sh --jobs 4
```

The build downloads NetHack 3.6.7. Add `--source /path/to/NetHack` to use an
existing Git checkout, which is left unchanged. Add `--build-dir /path/to/output`
to change the output directory. `build.sh --help` lists the available options.

## Customize

Use `--nethack-options` for comma-separated startup settings:

```sh
applications/nethack-3.6.7/build.sh --qemu-generators \
  --nethack-options 'symset:DECgraphics,number_pad:-1' \
  --banner 'Welcome to NetHack'
```

`DECgraphics` enables DEC line drawing. `number_pad:-1` swaps the vi movement
keys `y` and `z` for QWERTZ keyboards. Color is enabled by default; `!color`
disables it. Omit `--nethack-options` for defaults. `--banner ''` hides the
startup banner. Use `O` in the game to change gameplay options.

## Flash and play

The default output directory is `build/nethack-3.6.7/hardware/`.

1. Hold BOOTSEL while connecting the Pico by USB.
2. Copy `nethack.uf2` to the `RPI-RP2` drive.
3. Reconnect with BOOTSEL held and copy `nethack-volume.uf2` to the drive.
4. Open USB serial at 115200 baud in an **80×24 VT100/xterm-compatible terminal**.
   The port is usually `/dev/cu.usbmodem*` on macOS or `/dev/ttyACM0` on Linux.

Flash both files for the initial installation. Flashing the data volume replaces
saved games and scores. When the program exits, reconnect the terminal after
the Pico reboots.

## Saved games

Only one saved game is supported.

See [INTERNALS.md](INTERNALS.md) for source customization, regression tests and
implementation details, and [RESULTS.md](RESULTS.md) for measurements.
