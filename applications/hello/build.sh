#!/bin/sh
set -eu
recipe=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build=${1:-"$recipe/../../build/hello"}
cmake -S "$recipe" -B "$build" -G Ninja -DPICO_BOARD=pico
cmake --build "$build" --parallel 4
cmake --build "$build" --target volume
