# hack 1.0.3 (via restoHack)

restoHack is a version of hack 1.0.3 adapted for modern C compilers. 
It can run on your Pi Pico (rp2040) with 2MB flash.
It seeds its mazes using the hardware entropy generator, saves the games in flash
and drives the screen through termcap.
The game sources go in untouched.

## Getting the source code

Prerequsites: ARM GCC/newlib, a native C compiler, CMake, Python, `dosfstools`, and `mtools`.
On Linux: 
```bash
apt-get install git ca-certificates build-essential cmake ninja-build python3 \
  gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi screen \
  libstdc++-arm-none-eabi-newlib  dosfstools mtools libusb-1.0-0-dev pkg-config
```
On Mac: 
```sh
xcode-select --install
brew install git cmake ninja python dosfstools mtools libusb pkgconf
brew install --cask gcc-arm-embedded
export PATH="$(brew --prefix dosfstools)/sbin:$PATH"
```
On Window, instal WSL2 (Ubuntu): In powershell `wsl --install -d Ubuntu`, then follow the Linux instructions.

Start by cloning the three needed repositories (chances are, you have the first one already):
```sh
git clone https://github.com/neuhaus/rp2040-console.git
git clone https://github.com/Critlist/restoHack.git
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --depth 1 lib/tinyusb
```
You will be cross compiling the game for use on the rp2040.
Copy the `CMakeLists.txt` and `gen-onames.cmake` files from this directory to
`restoHack/pico-bare/` (a directory you have to create first).

## Building the UF2 flash files

```sh
cd restoHack/pico-bare
cmake -S . -B build \
      -DGAME_CONSOLE=../../rp2040-console \
      -DPICO_SDK_PATH=../../pico-sdk \
      -DPICO_BOARD=pico -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hack      # -> build/hack.uf2       (the program)
cmake --build build --target volume    # -> build/hack-volume.uf2 (the FAT volume)
```

If everything goes well, you have `hack.uf2` and `hack-volume.uf2` in your build folder now. 

Congrats!

### Flashing

This will overwrite whatever is currently on your Pico!

1. Hold *bootsel* while you plugin the usb port, or<br>if you rp2040 has a reset button hold both *reset* and *bootsel*, let go of *reset* first and then let go of *bootsel*. 
2. You will see a new drive connected to your PC. On mac it shows up as `/Volumes/RPI-RP2`
3. Copy the first uf2 file to that drive. The Pico will reboot.
4. Repeat steps 1-3 with the second uf2 file.

## Play!

On Mac, you can use `screen /dev/cu.usbmodem*1`

To quit screen, press ctrl+a then press shift+control+option+ `\\` (backslash) and confirm with "y".

Enjoy!

