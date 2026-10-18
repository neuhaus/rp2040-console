# hack 1.0.3 (via restoHack)

restoHack is a version of hack 1.0.3 adapted for modern C compilers. 
It can run on your Pi Pico (rp2040) with 2MB flash.
It seeds its mazes using the hardware entropy generator, saves the games in flash
and drives the screen through termcap.
The game sources go in untouched.

## Getting the source code

Start by cloning the three needed repositories (chances are, you have the first one already):
```sh
git clone https://github.com/neuhaus/rp2040-console.git
git clone https://github.com/raspberrypi/pico-sdk.git
git clone https://github.com/Critlist/restoHack.git
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

1. either hold *bootsel* while you plugin the usb port, or<br>hold both *reset* and *bootsel*, let go of *reset* first and then let go of *bootsel*. 
2. You will see a new drive connected to your PC. On mac it shows up as `/Volumes/RPI-PI2`
3. Copy the first uf2 file to that drive. The Pico will reboot.
4. Repeat steps 1-3 with the second uf2 file.

## Play!

On Mac, you can use `screen /dev/cu.usbmodem*1`

To quit screen, press ctrl+a then press shift+control+option+ `\\` (backslash) and confirm with "y".

Enjoy!

