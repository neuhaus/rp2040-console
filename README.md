# rp2040-console

A bare-metal platform layer for terminal programs on the Raspberry Pi Pico
(RP2040). It provides USB console I/O, termcap/termios compatibility, files,
environment variables and a clock. Application code and read-only tables execute
from flash using XIP, leaving SRAM for mutable data, heap and stack.

The platform was developed against Hack 1.0.3 (restoHack). Application-specific
configuration lives in a CMake manifest and separate port files, so compatible
programs can retain their original C sources.

## Build an application

On Debian, install the ARM compiler, newlib, native build tools and FAT utilities:

```sh
sudo apt update
sudo apt install git ca-certificates build-essential cmake ninja-build python3 \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-dev \
  libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  dosfstools mtools libusb-1.0-0-dev pkg-config
```

For NetHack's 32-bit data generators, install these additional packages on
**x86-64 Debian**:

```sh
sudo apt install gcc-multilib libc6-dev-i386 bison flex patch
```

Optional tools: `screen` for the USB terminal and `qemu-user` for the ARM/newlib
soft-reboot regression test. On other host architectures, follow the
[NetHack recipe](applications/nethack-3.1.3/README.md) for a 32-bit compiler and
runner instead of `gcc-multilib`.

Install Pico SDK 2.3.1 in a directory of your choice and initialize TinyUSB:

```sh
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --depth 1 lib/tinyusb
export PICO_SDK_PATH="$(realpath pico-sdk)"
```

Set `PICO_SDK_PATH` in each new shell. The SDK builds picotool automatically;
the first build needs network access. Then run from this repository:

```sh
# Minimal included terminal example
sh applications/hello/build.sh

# NetHack 3.1.3; downloads the pinned upstream source when --source is omitted
python3 applications/nethack-3.1.3/build.py
# Or use an existing upstream Git checkout
python3 applications/nethack-3.1.3/build.py --source /path/to/nethack

# NetHack 3.6.7; same host requirements, data volume is Tamp-compressed
python3 applications/nethack-3.6.7/build.py
```

NetHack also needs a 32-bit host compiler, Bison and Flex. See the
[NetHack recipe](applications/nethack-3.1.3/README.md) for dependencies, source
revision, memory layout and optional soft reboot.

Build outputs are kept under `build/`. Each application produces a firmware UF2
and a separate FAT data-volume UF2. The build checks flash and SRAM bounds and
verifies the data volume before converting it to UF2.

## Flash and use

1. Connect a standard Pico with **2 MB flash** while holding BOOTSEL.
2. Copy the application's firmware UF2 to its USB mass-storage drive.
3. Enter BOOTSEL again and copy its data-volume UF2. Either flashing order works.
4. Open the USB serial port in an 80x24 VT100/xterm-compatible terminal.

For Hello, the files are `build/hello/hello.uf2` and
`build/hello/hello-volume.uf2`. Other recipes document their output paths.
Reflashing the data volume replaces existing saves and scores. FAT writes are
not power-loss safe; save or quit before disconnecting the board.

On Linux the serial port is usually `/dev/ttyACM0`; on macOS it is
`/dev/cu.usbmodem*`. Program exit uses a hardware reboot by default, which drops
the USB connection. For a terminal that reconnects after reboot:

```sh
while true; do screen /dev/ttyACM0; sleep 1; done
```

The optional `SOFT_REBOOT` manifest flag restarts supported single-threaded C
applications while keeping the USB runtime alive. Its supported profile and
cleanup behavior are described in [INTERNALS.md](INTERNALS.md#optional-soft-reboot).
Physical USB continuity still requires validation on a Pico.

## Add an application

Create a directory under `applications/` containing a README, build entry point
and application-specific manifest or port files. Use
[Hello](applications/hello/README.md) as a small example and follow the
[recipe conventions](applications/README.md#adding-an-application).

`rp2040_console_game()` declares sources, data files, environment and compatibility
options. The full [manifest reference](INTERNALS.md#application-manifest) covers
legacy time values, symbol conflicts, host data generators and memory budgets.

The platform targets terminal programs with modest RAM requirements. It does
not provide a full ncurses implementation, subprocesses, threads, dynamic loading
or a Unix terminal driver. See [scope and limits](INTERNALS.md#scope) before
porting an application.

## Development reference

[INTERNALS.md](INTERNALS.md) explains startup, console routing, compatibility
headers, filesystem, memory layout, soft reboot and regression checks.
Application builds and image checks have been exercised in the cloud; physical
USB behavior, flash writes and long-game heap/stack peaks need board testing.

## Licence

3-clause BSD. Applications retain their own upstream licenses; each recipe
records its source and distribution requirements.
