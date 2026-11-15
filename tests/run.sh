#!/bin/sh
set -eu
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Optional extra flags for the host C checks, e.g.
#   HOST_TEST_CFLAGS="-fsanitize=address,undefined" sh tests/run.sh
host_cflags=${HOST_TEST_CFLAGS:-}
cc -std=c11 $host_cflags -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Iinclude tests/time32.c src/time32.c -o "$work/time32"
"$work/time32"
cc -std=c11 $host_cflags -Wall -Wextra -Werror -DRP2040_CONSOLE_ROWS=24 -DRP2040_CONSOLE_COLS=80 -Itests/support -Iinclude tests/termios.c src/termios_emul.c -o "$work/termios"
"$work/termios"
cc -std=c11 $host_cflags -Wall -Wextra -Werror -DRP2040_CONSOLE_ROWS=24 -DRP2040_CONSOLE_COLS=80 -Itests/support -Iinclude -Isrc tests/console-input.c src/console.c src/termios_emul.c -o "$work/console-input"
"$work/console-input"
if [ -n "${PICO_SDK_PATH:-}" ]; then
    python3 tests/test-fileio-paths.py
fi
python3 tests/test_tools.py
cc -std=c11 $host_cflags -Wall -Wextra -Werror -Iinclude tests/termcap.c src/termcap_shim.c -o "$work/termcap"
"$work/termcap"
