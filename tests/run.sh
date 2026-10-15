#!/bin/sh
set -eu
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Iinclude tests/time32.c src/time32.c -o "$work/time32"
"$work/time32"
cc -std=c11 -Wall -Wextra -Werror -DRP2040_CONSOLE_ROWS=24 -DRP2040_CONSOLE_COLS=80 -Itests/support -Iinclude tests/termios.c src/termios_emul.c -o "$work/termios"
"$work/termios"
if [ -n "${PICO_SDK_PATH:-}" ]; then
    python3 tests/test-fileio-paths.py
fi
python3 tests/test_tools.py
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/termcap.c src/termcap_shim.c -o "$work/termcap"
"$work/termcap"
