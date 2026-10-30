#!/bin/sh
# Usage: build.sh [build-dir] [extra cmake args...]
# Rogue is cloned into the build directory on the first configure; pass
# -DROGUE_SRC=/path/to/rogue to build a local checkout instead.
set -eu
recipe=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build=${1:-"$recipe/../../build/rogue"}
[ $# -ge 1 ] && shift
cmake -S "$recipe" -B "$build" -G Ninja -DPICO_BOARD=pico \
      -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$build" --parallel 4
cmake --build "$build" --target volume
