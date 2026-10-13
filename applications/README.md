# Application build recipes

Each directory contains a README and a build entry point. Upstream application
sources remain separate from this repository; configuration patches and platform
adapters live with the recipe. Build outputs belong under the ignored `build/`
directory. These scripts build images; they do not flash a device.

| Application | Entry point | Source |
| --- | --- | --- |
| [Hello](hello/README.md) | `sh applications/hello/build.sh` | Included minimal terminal example |
| [restoHack](restohack/README.md) | Manual CMake manifest; existing reference instructions | Hack 1.0.3 restoration |
| [NetHack 3.1.3](nethack-3.1.3/README.md) | `python3 applications/nethack-3.1.3/build.py` | Pinned official historical commit |
| [NetHack 3.6.7](nethack-3.6.7/README.md) | `python3 applications/nethack-3.6.7/build.py` | Pinned official release; Tamp-compressed data |

## Common prerequisites

The tested platform is Linux, Pico SDK 2.3.1 and ARM GCC 14.2.1/newlib 4.5.0.
On Debian/Ubuntu, install the tools (package versions depend on the distribution):

```sh
sudo apt install git build-essential cmake ninja-build python3 \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  dosfstools mtools libusb-1.0-0-dev pkg-config
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --depth 1 lib/tinyusb
export PICO_SDK_PATH="$(realpath pico-sdk)"
```

The SDK builds picotool when needed. Network access is required for that first
build. An existing executable can be supplied with `PICOTOOL` for data-volume
conversion. Application READMEs list additional dependencies.

Firmware and FAT data are separate UF2 files. Flash both to a standard 2 MB Pico
using BOOTSEL, entering BOOTSEL again for the second file. Reflashing a data volume
replaces its saves and scores. Use an 80x24 USB serial terminal. Hardware testing
is separate from successful compilation and image checks.

## Adding an application

Create `applications/<name>/README.md` and a build script. Record the upstream
repository, exact revision, license, extra dependencies, build command, generated
artifacts and hardware validation status. Keep upstream source checkouts intact;
prepare a private source tree in the build directory if patches are required.
Resolve platform paths relative to the recipe or an explicit CMake variable.
Do not depend on `/workspace`, a particular user's home, or cloud-only tools.

Use `rp2040_console_game()` for the manifest and the platform memory/volume checks.
Document any required compatibility options, flash layout and heap/stack budget.
For binary game data, verify host/ARM layouts with `tools/check-layout.py` before
building the volume. Test the recipe from a clean build directory and keep
downloaded sources and binaries out of this repository. Add the recipe to the
table above; do not advertise untested applications as working ports.
