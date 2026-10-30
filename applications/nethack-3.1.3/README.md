# NetHack 3.1.3 on RP2040

Play NetHack on a Pico or RP2040-Zero with 2 MiB flash over USB serial.

## Build

Install the [console prerequisites](../../README.md#build-an-application), plus
Bison, Flex and patch. Set `PICO_SDK_PATH` to your Pico SDK 2.3.1 directory and
put the ARM toolchain, dosfstools and mtools on `PATH`.

Run all build commands from the `rp2040-console` repository root.

On Apple Silicon, install QEMU with `qemu-system-arm`, then run:

```sh
python3 applications/nethack-3.1.3/build.py --qemu-generators
```

On x86-64 Debian/Ubuntu, install the 32-bit host compiler and build:

```sh
sudo apt install gcc-multilib libc6-dev-i386 bison flex patch
python3 applications/nethack-3.1.3/build.py
```

The build downloads the NetHack source. Add `--source /path/to/nethack` to use
an existing Git checkout, which is left unchanged. Use `--build-dir /path/to/output`
to change the output directory or `--jobs 2` to limit parallel compilation.
`python3 applications/nethack-3.1.3/build.py --help` lists all options.

## Flash

The default output directory is `build/nethack-3.1.3/hardware/`.

1. Hold BOOTSEL while connecting the board by USB.
2. Copy `nethack.uf2` to the `RPI-RP2` drive.
3. Reconnect with BOOTSEL held and copy `nethack-volume.uf2` to the drive.
4. Open USB serial at 115200 baud in an **80×24 VT100/xterm-compatible terminal**.
   The port is usually `/dev/cu.usbmodem*` on macOS or `/dev/ttyACM0` on Linux.

Flash both files for the initial installation. Flashing the data volume replaces
saved games and scores. **Older saves and bones are incompatible with this
port's current storage format; flash both images and start a new game when
upgrading from an earlier format.**

## Play and save

At startup, set the date and time or press Enter to accept the offered value,
then enter a character name. To resume a saved game, enter the same name.

Use `?` for help and the command list, `O` to change game options, and `S` to
save and exit. Wait for saving to finish before disconnecting the board. A
resumed normal game consumes its save; save again before disconnecting.

After exiting, press a key at `Press any key for a new game...`. The board
reboots; reconnect the serial terminal to continue.

See [INTERNALS.md](INTERNALS.md) for port implementation, advanced build settings
and regression tests, and [RESULTS.md](RESULTS.md) for hardware measurements and
the ascension validation.
