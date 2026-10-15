#!/usr/bin/env bash
# Build the RP2040 NetHack image; Python handles data packing and image checks.
set -eu
export PYTHONDONTWRITEBYTECODE=1
here=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
recipe_args=()
qemu_generators=0
for arg in "$@"; do
    case "$arg" in
        --qemu-generators) qemu_generators=1 ;;
        -h|--help)
            printf '%s\n' 'Additional build.sh option:' \
                '  --qemu-generators  Run 32-bit ARM data generators under QEMU.' ''
            recipe_args+=("$arg") ;;
        *) recipe_args+=("$arg") ;;
    esac
done
if [ "$qemu_generators" -eq 1 ]; then
    # The backend parses these as argument lists, without shell evaluation.
    HOST_CC=$(python3 -c 'import shlex,sys; print(shlex.join([sys.executable,sys.argv[1]]))' \
        "$here/port/host/host32_cc.py")
    HOST_RUNNER=$(python3 -c 'import shlex,sys; print(shlex.join([sys.executable,sys.argv[1]]))' \
        "$here/port/host/host32_run.py")
    export HOST_CC HOST_RUNNER
    export HOST_CFLAGS='' HOST_NM=arm-none-eabi-nm
fi
exec python3 "$here/port/build.py" "${recipe_args[@]+"${recipe_args[@]}"}"
