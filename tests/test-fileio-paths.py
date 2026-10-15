#!/usr/bin/env python3
"""Check shared cwd/pathconf wrappers using the SDK's FatFs declarations."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = os.environ.get('PICO_SDK_PATH')
if not sdk:
    raise SystemExit('Set PICO_SDK_PATH to run the fileio path tests')
fatfs = Path(os.environ.get('FATFS_DIR', str(Path(sdk) / 'lib/tinyusb/lib/fatfs/source')))
with tempfile.TemporaryDirectory() as name:
    work = Path(name)
    (work / 'pico').mkdir()
    (work / 'pico/stdlib.h').write_text('/* No Pico functions used by these wrappers. */\n')
    command = shlex.split(os.environ.get('CC', 'cc'))
    command += ['-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-function', '-ffunction-sections', '-fdata-sections',
                '-I' + str(work), '-I' + str(root / 'tests/support'),
                '-I' + str(root / 'include'), '-I' + str(fatfs),
                str(root / 'tests/fileio-paths.c'),
                '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections',
                '-o', str(work / 'paths')]
    subprocess.run(command, check=True)
    subprocess.run([str(work / 'paths')], check=True)
print('Shared getcwd/fpathconf tests passed')
