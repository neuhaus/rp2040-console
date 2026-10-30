# Rogue 9.8.6

Rogue 9.8.6 (Tim Stoehr's Rogue clone, ported to modern compilers by Mark
Willson) for a Raspberry Pi Pico (RP2040) with 2 MB flash. The game is cloned
from <https://github.com/mpwillson/rogue> during the first build. It may be
redistributed and modified, but not sold or used for profit; see the notice
at the top of each source file.

How the port works and how it was validated: [INTERNALS.md](INTERNALS.md) and
[RESULTS.md](RESULTS.md).

## Build

Install the [common prerequisites](../README.md#common-prerequisites) and set
`PICO_SDK_PATH`. Then, from the repository root:

```sh
sh applications/rogue/build.sh
```

The first build needs network access to clone Rogue. The firmware is
`build/rogue/rogue.uf2`, and the data volume is `build/rogue/rogue-volume.uf2`.
To build into another directory, pass it as the first argument.

## Flash

This overwrites whatever is on the Pico.

1. Hold BOOTSEL while plugging in the USB cable. The board appears as a drive
   (`RPI-RP2`).
2. Copy `rogue.uf2` to it. The board reboots.
3. Repeat with `rogue-volume.uf2`.

Reflashing the volume deletes the saved game and the scores.

## Play

Open the USB serial port in an 80x24 terminal, for example
`screen /dev/cu.usbmodem*` on macOS or `screen /dev/ttyACM0` on Linux. Press
Enter at the date prompt, then type your name at "Who are you?". Enter alone
keeps the name shown in brackets, the last one used.

- `?` lists the commands. Arrow keys move as well as `hjklyubn`.
- `S` saves: press Enter to accept `rogue.save`. The board then restarts and
  resumes the saved game on the next boot, for the same name.
- `Q` quits. Finished games are recorded in the score file.

Save or quit before unplugging the board. Writes to flash are not safe
against power loss.
